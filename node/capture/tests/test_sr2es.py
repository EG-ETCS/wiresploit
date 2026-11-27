"""Unit tests for tools/sr2es.py. Run: python3 -m unittest tests/test_sr2es.py"""

import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))

import sr2es  # noqa: E402


class Opts:
    """Stand-in for the parsed command line."""
    protocol = "uart"
    baud = 115200
    channel = 0
    base_gpio = 2
    gap_bytes = 10.0
    uart_type = "TTL-3V3"
    port = None
    index = sr2es.INDEX
    es_url = None
    insecure = False
    sigrok = "sigrok-cli"
    current_file = "/captures/cap.sr"


class TestParsing(unittest.TestCase):
    def test_annotation_lines_with_class(self):
        lines = [
            "sigrok-cli 0.7.2",                       # header, ignored
            "100-1000 uart-1: rx-data: 68",
            "1100-2000 uart-1: rx-data: 65",
            "not an annotation",
            "2100-3000 uart-1: rx-packet-error: frame error",
        ]
        events = sr2es.parse_annotations(lines)
        self.assertEqual(len(events), 3)
        self.assertEqual(events[0], {"start": 100, "end": 1000, "byte": 0x68,
                                     "error": False, "text": "68"})
        self.assertTrue(events[2]["error"])

    def test_annotation_lines_without_class(self):
        """sigrok 0.7 on the Pi prints no class name: '20018-20157 uart-1: 68'."""
        lines = [
            "20018-20157 uart-1: 68",
            "20191-20330 uart-1: 6C",                 # uppercase hex
            "20365-20504 uart-1: frame error",
        ]
        events = sr2es.parse_annotations(lines)
        self.assertEqual(len(events), 3)
        self.assertEqual(events[0]["byte"], 0x68)
        self.assertEqual(events[0]["start"], 20018)
        self.assertEqual(events[1]["byte"], 0x6C)
        self.assertTrue(events[2]["error"])
        self.assertIsNone(events[2]["byte"])

    def test_out_of_order_lines_are_sorted(self):
        events = sr2es.parse_annotations(["500-600 uart-1: rx-data: 02",
                                          "100-200 uart-1: rx-data: 01"])
        self.assertEqual([e["byte"] for e in events], [1, 2])

    def test_grouping_by_gap(self):
        events = [
            {"start": 0, "end": 100, "byte": 1, "error": False, "text": "01"},
            {"start": 150, "end": 250, "byte": 2, "error": False, "text": "02"},
            {"start": 5000, "end": 5100, "byte": 3, "error": False, "text": "03"},
        ]
        messages = sr2es.group_messages(events, gap_samples=1000)
        self.assertEqual([len(m) for m in messages], [2, 1])
        # A larger gap threshold keeps everything together.
        self.assertEqual(len(sr2es.group_messages(events, gap_samples=10000)), 1)


class TestDocuments(unittest.TestCase):
    def message(self, payload, start=1000, bit_samples=100):
        events = []
        pos = start
        for b in payload:
            events.append({"start": pos, "end": pos + 10 * bit_samples, "byte": b,
                           "error": False, "text": "%02x" % b})
            pos += 10 * bit_samples
        return events

    def test_fields_match_the_mapping(self):
        # 2 MSa/s, message starts 1000 samples (0.5 ms) into the capture.
        doc = sr2es.build_document(self.message(b"hi"), start_ns=1000000000 * 1000000000,
                                   rate_hz=2e6, opts=Opts(), extra_psf={})
        self.assertEqual(sorted(doc), ["duration", "event_type", "length_bytes",
                                       "payload_hex", "protocol", "psf", "timestamp"])
        self.assertEqual(doc["protocol"], "UART")     # uppercase, like "TCP"
        self.assertEqual(doc["event_type"], "onboard")   # as the index already uses
        self.assertEqual(doc["payload_hex"], "6869")
        self.assertEqual(doc["length_bytes"], 2)
        # 2 bytes x 10 bits x 100 samples = 2000 samples = 1 ms = 1e6 ns
        self.assertEqual(doc["duration"], 1000000)
        self.assertEqual(doc["timestamp"], "2001-09-09T01:46:40.000500000Z")

    def test_psf_is_flat(self):
        """
        psf carries a common core and the protocol's own fields side by side,
        with no psd, capture or clock sub-object to reach through.
        """
        doc = sr2es.build_document(self.message(b"ok\r\n"), 10 ** 18, 2e6, Opts(),
                                   {"burst": "burst-0001.sr", "gap_before_s": 2.95,
                                    "first_edge_time_ns": 1789986306522345678,
                                    "clock_synchronised": True,
                                    "clock_max_error_us": 1234,
                                    "clock_drift_ns": -41200})
        psf = doc["psf"]
        self.assertNotIn("psd", psf)
        self.assertNotIn("capture", psf)
        self.assertNotIn("clock", psf)
        for value in psf.values():
            self.assertNotIsInstance(value, dict)

        # UART's own fields.
        self.assertEqual(psf["baud_rate"], 115200)      # not "baud"
        self.assertEqual(psf["uart_type"], "TTL-3V3")
        self.assertEqual(psf["data_bits"], 8)
        self.assertEqual(psf["parity"], "none")
        self.assertEqual(psf["stop_bits"], 1)
        self.assertIs(psf["framing_error"], False)

        # The common core, flat beside them.
        self.assertEqual(psf["port"], "ch0")
        self.assertEqual(psf["channel"], 0)
        self.assertEqual(psf["gpio"], 2)
        self.assertEqual(psf["byte_count"], 4)
        self.assertEqual(psf["text"], "ok\\r\\n")
        self.assertEqual(psf["file"], "cap.sr")
        self.assertEqual(psf["burst"], "burst-0001.sr")
        self.assertEqual(psf["gap_before_s"], 2.95)
        self.assertEqual(psf["samplerate_hz"], 2000000)
        self.assertEqual(psf["first_edge_time_ns"], 1789986306522345678)
        self.assertIs(psf["clock_synchronised"], True)
        self.assertEqual(psf["clock_max_error_us"], 1234)
        self.assertEqual(psf["clock_drift_ns"], -41200)

    def test_protocol_fields_never_shadow_the_core(self):
        """psf being flat means a careless new protocol could overwrite a core
        field; the table asserts it cannot, so check that guard holds."""
        for name, spec in sr2es.PROTOCOLS.items():
            clash = set(spec.fields(Opts(), {})) & set(sr2es.CORE_FIELDS)
            self.assertEqual(clash, set(), "%s shadows %s" % (name, clash))

    def test_raw_document_needs_no_decoder(self):
        """An unknown bus still gets a timestamped document, from the sidecar."""
        opts = Opts()
        opts.protocol = "raw"
        doc = sr2es.build_raw_document(
            10 ** 18, 2e6, opts,
            {"burst": "burst-0002.sr", "first_edge_time_ns": 1789986306522345678,
             "_edges": 214, "_active_channels": [0, 3]},
            samples=121552)
        self.assertEqual(doc["protocol"], "RAW")
        self.assertEqual(doc["length_bytes"], 0)
        self.assertNotIn("payload_hex", doc)
        # Stamped at the first edge, not at the pre-roll the file starts with.
        self.assertEqual(doc["timestamp"], sr2es.iso_nanos(1789986306522345678))
        self.assertEqual(doc["psf"]["edges"], 214)
        self.assertEqual(doc["psf"]["active_channels"], [0, 3])
        # No UART field leaks into a bus that has none.
        for key in ("baud_rate", "data_bits", "parity", "stop_bits", "uart_type"):
            self.assertNotIn(key, doc["psf"])

    def test_errors_are_listed_in_psf(self):
        message = self.message(b"x")
        message.append({"start": 3000, "end": 4000, "byte": None, "error": True,
                        "text": "frame error"})
        doc = sr2es.build_document(message, 10 ** 18, 2e6, Opts(), {})
        self.assertEqual(doc["event_type"], "onboard")
        self.assertIs(doc["psf"]["framing_error"], True)
        self.assertIs(doc["psf"]["error"], True)
        self.assertEqual(doc["psf"]["errors"], ["frame error"])
        self.assertEqual(doc["length_bytes"], 1)

    def test_long_payloads_are_truncated(self):
        doc = sr2es.build_document(self.message(b"A" * 40000), 10 ** 18, 2e6, Opts(), {})
        self.assertEqual(len(doc["payload_hex"]), sr2es.MAX_PAYLOAD_HEX)
        self.assertTrue(doc["psf"]["payload_truncated"])
        self.assertLessEqual(len(doc["psf"]["text"]), sr2es.MAX_TEXT)
        self.assertEqual(doc["length_bytes"], 40000)     # the true length is kept

    def test_iso_nanos(self):
        self.assertEqual(sr2es.iso_nanos(0), "1970-01-01T00:00:00.000000000Z")
        self.assertEqual(sr2es.iso_nanos(1000000001), "1970-01-01T00:00:01.000000001Z")
        self.assertEqual(sr2es.epoch_nanos(1.5), 1500000000)
        # A double cannot hold nanoseconds at epoch scale, so the timestamp is
        # built from integers: this must be exact, not 36 ns out.
        self.assertEqual(sr2es.epoch_nanos(1789986306.5) + 500000,
                         1789986306500500000)

    def test_printable(self):
        self.assertEqual(sr2es.printable(b"ab\r\n\x00\xff"), "ab\\r\\n\\x00\\xff")


class TestBulkFile(unittest.TestCase):
    def test_ndjson_is_action_then_document(self):
        docs = [sr2es.build_document([{"start": 0, "end": 10, "byte": 0x41,
                                       "error": False, "text": "41"}],
                                     1e9, 2e6, Opts(), {})]
        with tempfile.TemporaryDirectory() as d:
            path = sr2es.write_ndjson(docs, os.path.join(d, "out.ndjson"))
            with open(path) as f:
                lines = f.read().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertEqual(json.loads(lines[0]), {"create": {"_index": "wiresploit-poc1"}})
        self.assertEqual(json.loads(lines[1])["payload_hex"], "41")


class TestMetadata(unittest.TestCase):
    def test_sidecar_is_used(self):
        with tempfile.TemporaryDirectory() as d:
            sr = os.path.join(d, "cap.sr")
            with open(sr, "wb") as f:
                f.write(b"not really a zip")
            with open(sr + ".meta.json", "w") as f:
                json.dump({"samplerate_hz": 2000000, "start_time": 1789986306.5,
                           "start_sample": 0, "samples": 100}, f)
            items = sr2es.load_meta(sr)
        self.assertEqual(len(items), 1)
        path, start_ns, rate, extra = items[0]
        self.assertEqual(path, sr)
        self.assertEqual(start_ns, 1789986306500000000)
        self.assertEqual(rate, 2000000.0)

    def test_integer_nanoseconds_win_over_the_float(self):
        """
        A double holds only ~240 ns at epoch scale, so start_time_ns is what
        makes a stamp exact. It must be preferred whenever it is there.
        """
        exact = 1789986306512345678
        with tempfile.TemporaryDirectory() as d:
            sr = os.path.join(d, "cap.sr")
            open(sr, "wb").close()
            with open(sr + ".meta.json", "w") as f:
                json.dump({"samplerate_hz": 2000000, "start_time": 1789986306.512345,
                           "start_time_ns": exact, "samples": 100}, f)
            items = sr2es.load_meta(sr)
        self.assertEqual(items[0][1], exact)

    def test_monitor_session_prefers_per_burst_sidecars(self):
        """
        index.json is rewritten on every burst, so a sweep can read it
        mid-write; each burst's own sidecar is written once and never touched.
        """
        with tempfile.TemporaryDirectory() as d:
            for i, name in enumerate(("burst-0001.sr", "burst-0002.sr")):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")
                with open(os.path.join(d, name + ".meta.json"), "w") as f:
                    json.dump({"samplerate_hz": 2000000, "channels": 8,
                               "burst": name, "samples": 100,
                               "start_time_ns": 100000000000 + i * 3000000000,
                               "first_edge_time_ns": 100010000000 + i * 3000000000,
                               "gap_before_s": 2.9 + i, "protocol": "uart",
                               "decode_channel": 0, "baud": 115200,
                               "clock_synchronised": True}, f)
            items = sr2es.load_meta(d)
        self.assertEqual(len(items), 2)
        self.assertEqual(items[1][1], 103000000000)
        self.assertEqual(items[1][3]["burst"], "burst-0002.sr")
        self.assertEqual(items[1][3]["first_edge_time_ns"], 103010000000)
        self.assertIs(items[1][3]["clock_synchronised"], True)
        self.assertEqual(items[1][3]["_protocol"], "uart")

    def test_monitor_session_falls_back_to_the_index(self):
        """Sessions recorded before sidecars existed still have to work."""
        with tempfile.TemporaryDirectory() as d:
            for name in ("burst-0001.sr", "burst-0002.sr"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")
            with open(os.path.join(d, "index.json"), "w") as f:
                json.dump({"samplerate_hz": 2000000, "channels": 8, "bursts": [
                    {"file": "burst-0001.sr", "start_time": 100.0, "gap_before_s": 2.9},
                    {"file": "burst-0002.sr", "start_time": 103.0, "gap_before_s": 3.0},
                ]}, f)
            items = sr2es.load_meta(d)
        self.assertEqual(len(items), 2)
        self.assertEqual(items[1][1], 103000000000)
        self.assertEqual(items[1][3]["burst"], "burst-0002.sr")

    def test_missing_index_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(sr2es.Error):
                sr2es.load_meta(d)


class TestShipping(unittest.TestCase):
    """
    --ship uses the directory itself as its work queue: what is on disk says
    what still has to go. These cover each of the three states it tells apart.
    """

    def burst(self, d, n, sr=True, sidecar=True, ndjson=False, **meta):
        name = os.path.join(d, "burst-%04d.sr" % n)
        if sr:
            with open(name, "wb") as f:
                f.write(b"x")
        if sidecar:
            body = {"samplerate_hz": 2000000, "channels": 8, "samples": 100,
                    "start_time_ns": 10 ** 18, "protocol": "uart"}
            body.update(meta)
            with open(name + ".meta.json", "w") as f:
                json.dump(body, f)
        if ndjson:
            with open(name + ".ndjson", "w") as f:
                f.write('{"create": {"_index": "i"}}\n{"a": 1}\n')
        return name

    def test_pending_finds_unshipped_bursts_in_order(self):
        with tempfile.TemporaryDirectory() as d:
            self.burst(d, 2)
            self.burst(d, 1)
            work = sr2es.pending_bursts(d)
        self.assertEqual([os.path.basename(w["sr"]) for w in work],
                         ["burst-0001.sr", "burst-0002.sr"])
        self.assertTrue(all(w["decode"] for w in work))

    def test_a_burst_still_being_written_is_skipped(self):
        """No sidecar means the .sr is not final yet: never decode it."""
        with tempfile.TemporaryDirectory() as d:
            self.burst(d, 1, sidecar=False)
            self.assertEqual(sr2es.pending_bursts(d), [])

    def test_a_shipped_burst_is_skipped(self):
        with tempfile.TemporaryDirectory() as d:
            self.burst(d, 1, shipped=True)
            self.assertEqual(sr2es.pending_bursts(d), [])

    def test_a_failed_upload_is_retried_without_decoding_again(self):
        """.ndjson but no .sr: the decode worked, only the upload did not."""
        with tempfile.TemporaryDirectory() as d:
            self.burst(d, 1, sr=False, sidecar=False, ndjson=True)
            work = sr2es.pending_bursts(d)
        self.assertEqual(len(work), 1)
        self.assertFalse(work[0]["decode"])

    def test_an_existing_ndjson_is_not_decoded_twice(self):
        """Re-running sigrok over a burst already decoded buys nothing."""
        with tempfile.TemporaryDirectory() as d:
            self.burst(d, 1)
            self.assertTrue(sr2es.pending_bursts(d)[0]["decode"])
            self.burst(d, 1, ndjson=True)
            self.assertFalse(sr2es.pending_bursts(d)[0]["decode"])

    def test_ndjson_is_written_atomically(self):
        """A shipper killed mid-write must not leave half a bulk file that the
        next sweep would upload as though it were whole."""
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "out.ndjson")
            docs = [sr2es.build_document([{"start": 0, "end": 10, "byte": 0x41,
                                           "error": False, "text": "41"}],
                                         10 ** 18, 2e6, Opts(), {})]
            sr2es.write_ndjson(docs, path)
            self.assertFalse(os.path.exists(path + ".tmp"))
            self.assertEqual(sr2es.count_ndjson(path), 1)

    def test_lock_excludes_a_second_shipper(self):
        with tempfile.TemporaryDirectory() as d:
            sr = self.burst(d, 1)
            first = sr2es.take_lock(sr)
            self.assertIsNotNone(first)
            self.assertIsNone(sr2es.take_lock(sr))     # someone else has it
            os.unlink(first)
            self.assertIsNotNone(sr2es.take_lock(sr))  # released

    def test_a_stale_lock_is_taken_over(self):
        """A shipper killed mid-flight must not strand its burst forever."""
        with tempfile.TemporaryDirectory() as d:
            sr = self.burst(d, 1)
            lock = sr2es.take_lock(sr)
            old = time.time() - sr2es.STALE_LOCK_S - 60
            os.utime(lock, (old, old))
            self.assertIsNotNone(sr2es.take_lock(sr))

    def test_a_confirmed_upload_deletes_the_samples(self):
        with tempfile.TemporaryDirectory() as d:
            sr = self.burst(d, 1, ndjson=True)
            sr2es.mark_shipped(sr, docs=3, keep_sr=False)
            self.assertFalse(os.path.exists(sr))
            self.assertFalse(os.path.exists(sr + ".ndjson"))
            # The sidecar is the tombstone: it says the burst existed.
            meta = sr2es.load_sidecar(sr)
        self.assertTrue(meta["shipped"])
        self.assertEqual(meta["shipped_docs"], 3)

    def test_a_raw_burst_keeps_its_samples(self):
        """A raw document carries no payload, so its .sr is the only copy."""
        with tempfile.TemporaryDirectory() as d:
            sr = self.burst(d, 1, ndjson=True, protocol="raw")
            sr2es.mark_shipped(sr, docs=1, keep_sr=True)
            self.assertTrue(os.path.exists(sr))
            self.assertFalse(os.path.exists(sr + ".ndjson"))
            self.assertEqual(sr2es.pending_bursts(d), [])   # still not re-shipped

    def test_the_sidecar_decides_the_protocol(self):
        """The flag is a default; whoever captured the burst knew the bus."""
        opts = Opts()
        opts.protocol = "uart"
        with tempfile.TemporaryDirectory() as d:
            sr = self.burst(d, 1, protocol="raw")
            self.assertEqual(sr2es.declared_protocol(sr, opts), "raw")

    def test_shipped_log_records_what_went(self):
        with tempfile.TemporaryDirectory() as d:
            sr = self.burst(d, 1)
            sr2es.log_shipped(d, sr, 2, 7)
            with open(os.path.join(d, sr2es.SHIPPED_LOG)) as f:
                line = json.loads(f.read().strip())
        self.assertEqual(line["burst"], "burst-0001.sr")
        self.assertEqual(line["documents"], 2)


class TestAuth(unittest.TestCase):
    def test_api_key_and_basic(self):
        saved = {k: os.environ.pop(k, None) for k in ("ES_API_KEY", "ES_USER", "ES_PASSWORD")}
        try:
            self.assertIsNone(sr2es.auth_header())
            os.environ["ES_USER"], os.environ["ES_PASSWORD"] = "u", "p"
            self.assertEqual(sr2es.auth_header(), "Basic dTpw")
            os.environ["ES_API_KEY"] = "abc123"
            self.assertEqual(sr2es.auth_header(), "ApiKey abc123")   # API key wins
        finally:
            for k, v in saved.items():
                os.environ.pop(k, None)
                if v is not None:
                    os.environ[k] = v


if __name__ == "__main__":
    unittest.main()
