"""Integration tests: sr2es and sigrok-cli on the committed sample captures.

Run: python3 -m pytest node/capture/tests/integration
 or: python3 node/capture/tests/integration/test_decode.py

Each case runs the real exporter/sr2es.py command line on a file in samples/
and compares the documents it writes with expected/<sample>.docs.json. No
Elasticsearch is involved: --es-url is given empty, so sr2es only writes the
NDJSON file.

TestUartSamples needs sigrok-cli and is skipped without it. TestRawSamples
never runs a decoder, so it always runs. ctest runs the two classes as
separate tests (test_decode_raw, test_decode_uart), so a skipped decoder
never hides a failure in the raw case.

After an intended change to the document shape, rewrite the expected files
with PI5LA_UPDATE_EXPECTED=1 and review the diff before committing it.
"""

import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLES_DIR = os.path.join(HERE, "samples")
EXPECTED_DIR = os.path.join(HERE, "expected")
NODE_DIR = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SR2ES = os.path.join(NODE_DIR, "exporter", "sr2es.py")
INDEX = "wiresploit-poc1"

SIGROK_CLI = shutil.which("sigrok-cli")
NO_SIGROK = "sigrok-cli not found on PATH (sudo apt install sigrok-cli)"
UPDATE_EXPECTED = os.environ.get("PI5LA_UPDATE_EXPECTED") == "1"


def run_sr2es(sample, out_path):
    """Run the sr2es command line on one sample; never uploads."""
    argv = [
        sys.executable, SR2ES, os.path.join(SAMPLES_DIR, sample),
        "--out", out_path,
        "--es-url=",                        # empty: write the NDJSON, upload nothing
        "--sigrok", SIGROK_CLI or "sigrok-cli",
    ]
    return subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          universal_newlines=True, timeout=300)


def read_ndjson(path):
    """Bulk NDJSON -> (action lines, documents)."""
    with open(path) as f:
        lines = [json.loads(line) for line in f if line.strip()]
    return lines[0::2], lines[1::2]


def load_sidecar(sample):
    with open(os.path.join(SAMPLES_DIR, sample + ".meta.json")) as f:
        return json.load(f)


def iso_from_ns(nanos):
    whole, rest = divmod(nanos, 1000000000)
    stamp = datetime.datetime.fromtimestamp(whole, datetime.timezone.utc)
    return "%s.%09dZ" % (stamp.strftime("%Y-%m-%dT%H:%M:%S"), rest)


class SampleTestCase(unittest.TestCase):
    """Runs sr2es into a temporary folder and compares with expected/."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="test_decode.")
        self.out = os.path.join(self.tmp, "out.ndjson")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def decode(self, sample):
        done = run_sr2es(sample, self.out)
        self.assertEqual(done.returncode, 0, "sr2es failed:\n" + done.stderr)
        actions, docs = read_ndjson(self.out)
        self.assertEqual(actions, [{"create": {"_index": INDEX}}] * len(docs))
        return docs

    def assert_matches_expected(self, sample, docs):
        path = os.path.join(EXPECTED_DIR, sample[:-len(".sr")] + ".docs.json")
        if UPDATE_EXPECTED:
            with open(path, "w") as f:
                json.dump(docs, f, indent=2, sort_keys=True)
                f.write("\n")
        with open(path) as f:
            expected = json.load(f)
        self.assertEqual(docs, expected)


class TestUartSamples(SampleTestCase):
    @classmethod
    def setUpClass(cls):
        if not SIGROK_CLI:
            raise unittest.SkipTest(NO_SIGROK)

    def test_uart_hello_decodes_to_one_hello_document(self):
        docs = self.decode("uart_hello.sr")
        self.assertEqual(len(docs), 1)
        doc = docs[0]
        # Checked against the sample's definition, not only against expected/.
        self.assertEqual(doc["protocol"], "UART")
        self.assertEqual(doc["payload_hex"], b"hello".hex())
        self.assertEqual(doc["length_bytes"], 5)
        self.assertEqual(doc["psf"]["text"], "hello")
        self.assertEqual(doc["psf"]["baud_rate"], 115200)
        self.assertFalse(doc["psf"]["framing_error"])
        self.assertEqual(doc["psf"]["file"], "uart_hello.sr")
        self.assert_matches_expected("uart_hello.sr", docs)

    def test_all_idle_decodes_to_no_document_and_fails(self):
        done = run_sr2es("all_idle.sr", self.out)
        self.assertEqual(done.returncode, 1)
        self.assertIn("no messages decoded", done.stderr)
        self.assertFalse(os.path.exists(self.out))


class TestRawSamples(SampleTestCase):
    def test_two_bursts_raw_gives_one_document_at_the_first_edge(self):
        docs = self.decode("two_bursts.sr")
        self.assertEqual(len(docs), 1)
        doc = docs[0]
        meta = load_sidecar("two_bursts.sr")
        self.assertEqual(doc["protocol"], "RAW")
        self.assertEqual(doc["timestamp"], iso_from_ns(meta["first_edge_time_ns"]))
        self.assertEqual(doc["duration"], meta["samples"] * 1000)    # 1 MSa/s
        self.assertEqual(doc["psf"]["edges"], 4)
        self.assertEqual(doc["psf"]["active_channels"], [0])
        self.assert_matches_expected("two_bursts.sr", docs)


if __name__ == "__main__":
    unittest.main(verbosity=2)
