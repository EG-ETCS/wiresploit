#!/usr/bin/env python3
"""
Turn captures into Elasticsearch documents for the wiresploit-poc1 index.

    capture (.sr, or a monitor session directory)
        -> decode with sigrok-cli, or none at all for --protocol raw
        -> events grouped into messages by idle gap
        -> one document per message, written as NDJSON
        -> optional _bulk upload

The protocol is declared by whoever ran the capture, never guessed here: pi5la
records it in each burst's sidecar, and --protocol says it for a capture made
some other way. Adding I2C or SPI means adding a PROTOCOLS entry, not rewriting
this. Only "uart" decodes today; "raw" ships a burst undecoded, so a bus nothing
understands still gets timestamped and stored.

Document fields:

    timestamp     date_nanos, when the message's first bit was sampled
    event_type    "onboard"
    protocol      "UART", "RAW", ...
    duration      nanoseconds from the first bit to the last
    length_bytes  bytes in the message
    payload_hex   the message's bytes, lowercase hex
    psf           flat: a common core (channel, gpio, file, burst, samples,
                  clock state, first_edge_time_ns) plus the declared protocol's
                  own fields (baud_rate and friends for UART, edges and
                  active_channels for raw)

Absolute time comes from the sidecar pi5la writes beside every capture
(<capture>.sr.meta.json, or burst-NNNN.sr.meta.json in a monitor session), which
carries integer nanoseconds so a timestamp is not rounded to the ~240 ns a
double can hold at today's epoch values.

    --ship DIR  is how pi5la hands over a burst the instant it closes: it takes
    whatever in DIR has not been shipped yet, decodes and uploads it, deletes
    what was confirmed, and leaves the rest for the next sweep to retry.

Credentials are never read from the command line: set ES_API_KEY, or ES_USER
and ES_PASSWORD, in the environment.

Examples:

    sr2es.py capture.sr --baud 115200
    sr2es.py ~/captures/mon-test --channel 0 --es-url https://es.example:9200
    sr2es.py --ship ~/captures/mon-test --es-url https://es.example:9200
"""

import argparse
import datetime
import glob
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

INDEX = "wiresploit-poc1"
PROTOCOL = "UART"            # uppercase, like the "TCP" documents in the index
EVENT_TYPE = "onboard"       # what the index's existing UART documents use
DEFAULT_UART_TYPE = "TTL-3V3"   # what the 74LVC245 buffer feeds the Pi
MAX_PAYLOAD_HEX = 64 * 1024      # 32 kB of bytes; longer messages are truncated
MAX_TEXT = 2000                  # psf.text stays well under the 8191 ignore_above
DEFAULT_GAP_BYTES = 10.0         # idle byte-times that end a message
STALE_LOCK_S = 600               # a lock older than this was left by a dead shipper
SHIPPED_LOG = "shipped.ndjson"   # audit trail of what actually reached the cluster

# psf keys every document carries, whatever the protocol. A protocol's own
# fields sit beside these, flat, so nothing here may be shadowed.
CORE_FIELDS = (
    "channel", "gpio", "port", "file", "burst", "byte_count", "text", "error",
    "start_sample", "end_sample", "samplerate_hz", "gap_before_s",
    "first_edge_time_ns", "clock_synchronised", "clock_max_error_us",
    "clock_drift_ns", "time_source", "errors", "payload_truncated",
)

# Sidecar keys passed straight into psf. Everything else in a sidecar is either
# used directly (rates, samples) or is provenance the document does not need.
PASSTHROUGH_FIELDS = (
    "burst", "gap_before_s", "first_edge_time_ns",
    "clock_synchronised", "clock_max_error_us", "clock_drift_ns",
)

# sigrok-cli --protocol-decoder-samplenum prints either
#   "1234-5678 uart-1: 6a"            (0.7: no class name)
#   "1234-5678 uart-1: rx-data: 6a"   (with the class)
# so the class is optional.
ANNOTATION_RE = re.compile(
    r"^(?P<start>\d+)-(?P<end>\d+)\s+(?P<decoder>[\w-]+):\s+"
    r"(?:(?P<cls>[a-z][\w-]*):\s+)?(?P<text>.*)$")
HEX_RE = re.compile(r"\b[0-9A-Fa-f]{2}\b")
ERROR_TEXT_RE = re.compile(r"error|break|parity|framing", re.I)


class Error(Exception):
    pass


# --------------------------------------------------------------------------
# capture metadata


def meta_start_ns(meta, fallback_key="start_time"):
    """
    A capture's start, in integer nanoseconds.

    pi5la writes start_time_ns for exactly this reason: a double holds only
    ~240 ns at today's epoch values, which is coarser than a sample at 50 MSa/s
    and coarser than a bit on a fast bus. The float is still read for captures
    made before the sidecar carried nanoseconds.
    """
    if meta.get("start_time_ns") is not None:
        return int(meta["start_time_ns"])
    return epoch_nanos(float(meta[fallback_key]))


def psf_from_meta(meta):
    """The sidecar fields that belong in a document, as psf keys."""
    extra = {k: meta[k] for k in PASSTHROUGH_FIELDS if meta.get(k) is not None}
    for key in ("protocol", "decode_channel", "baud", "edges", "active_channels",
                "samples"):
        if meta.get(key) is not None:
            extra["_" + key] = meta[key]       # consumed below, not emitted as-is
    return extra


def load_sidecar(sr_path):
    """burst-NNNN.sr / capture.sr -> its .meta.json, or None if there is none."""
    try:
        with open(sr_path + ".meta.json") as f:
            return json.load(f)
    except OSError:
        return None
    except ValueError as e:
        raise Error("%s.meta.json is malformed: %s" % (sr_path, e))


def load_meta(path):
    """
    Where a capture starts in wall-clock time and how fast it was sampled.

    Returns a list of (sr_path, start_ns, samplerate_hz, psf_extra); one entry
    for a .sr file, one per burst for a monitor session directory.

    A monitor session prefers each burst's own sidecar over index.json: the
    sidecar is written once, after that burst's .sr is final, whereas index.json
    is rewritten on every burst and so can be read mid-write.
    """
    if os.path.isdir(path):
        items = []
        for sr_path in sorted(glob.glob(os.path.join(path, "burst-*.sr"))):
            meta = load_sidecar(sr_path)
            if meta:
                items.append((sr_path, meta_start_ns(meta),
                              float(meta["samplerate_hz"]), psf_from_meta(meta)))
        if items:
            return items

        # No sidecars: an older session, described only by index.json.
        index_path = os.path.join(path, "index.json")
        try:
            with open(index_path) as f:
                index = json.load(f)
        except OSError as e:
            raise Error("cannot read %s: %s" % (index_path, e))
        rate = float(index["samplerate_hz"])
        for burst in index.get("bursts", []):
            items.append((
                os.path.join(path, burst["file"]),
                meta_start_ns(burst),
                rate,
                psf_from_meta(dict(burst, burst=burst["file"])),
            ))
        if not items:
            raise Error("%s has no bursts" % path)
        return items

    meta = load_sidecar(path)
    if meta:
        try:
            return [(path, meta_start_ns(meta), float(meta["samplerate_hz"]),
                     psf_from_meta(meta))]
        except (KeyError, ValueError) as e:
            raise Error("%s.meta.json is malformed: %s" % (path, e))

    # No sidecar (an older capture): fall back to the file's modification time
    # and ask sigrok for the rate. The timestamps are then approximate.
    rate = samplerate_of(path)
    sys.stderr.write("warning: no %s.meta.json, using the file's mtime; timestamps are "
                     "approximate\n" % os.path.basename(path))
    return [(path, epoch_nanos(os.path.getmtime(path)), rate, {"time_source": "mtime"})]


def samplerate_of(path, sigrok="sigrok-cli"):
    """Sample rate from the .sr metadata, via sigrok-cli -i FILE --show."""
    try:
        out = subprocess.run([sigrok, "-i", path, "--show"], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=60).stdout.decode(
                                 "utf-8", "replace")
    except (OSError, subprocess.SubprocessError) as e:
        raise Error("cannot run %s: %s" % (sigrok, e))
    m = re.search(r"samplerate:\s*([\d.]+)\s*(k|M|G)?Hz", out, re.I)
    if not m:
        raise Error("cannot read the sample rate from %s" % path)
    scale = {"k": 1e3, "M": 1e6, "G": 1e9}.get(m.group(2) or "", 1.0)
    return float(m.group(1)) * scale


# --------------------------------------------------------------------------
# decoding


_ANNOTATION_CACHE = {}
# Error-ish classes worth keeping, if this sigrok build has them. Names differ
# between versions, so they are matched against what the decoder reports.
ERROR_CLASSES = ("rx-packet-error", "rx-parity-err", "rx-break", "error", "warning")


def uart_classes(sigrok="sigrok-cli"):
    """
    Annotation classes this sigrok's UART decoder actually has.

    Class names vary between versions (0.7's uart has no rx-packet-error, for
    example), and naming a missing one makes sigrok-cli fail outright, so ask
    it rather than assume.
    """
    if sigrok in _ANNOTATION_CACHE:
        return _ANNOTATION_CACHE[sigrok]
    try:
        out = subprocess.run([sigrok, "-P", "uart", "--show"], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT,
                             timeout=60).stdout.decode("utf-8", "replace")
    except (OSError, subprocess.SubprocessError) as e:
        raise Error("cannot run %s: %s" % (sigrok, e))
    available = set(re.findall(r"^\s*-?\s*([a-z0-9][a-z0-9-]*):", out, re.M))
    wanted = ["rx-data"] + [c for c in ERROR_CLASSES if c in available]
    if "rx-data" not in available:
        wanted = ["rx-data"]          # trust the default name and let sigrok complain
    _ANNOTATION_CACHE[sigrok] = wanted
    return wanted


def decode_uart(path, channel, baud, base_gpio=2, sigrok="sigrok-cli"):
    """Run sigrok-cli and return its annotation lines."""
    argv = [
        sigrok, "-i", path,
        "-P", "uart:rx=GPIO%d:baudrate=%d:format=hex" % (base_gpio + channel, baud),
        "-A", "uart=" + ":".join(uart_classes(sigrok)),
        "--protocol-decoder-samplenum",
    ]
    try:
        done = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=600)
    except (OSError, subprocess.SubprocessError) as e:
        raise Error("cannot run %s: %s" % (sigrok, e))
    if done.returncode != 0:
        raise Error("sigrok-cli failed: %s" %
                    done.stderr.decode("utf-8", "replace").strip()[:400])
    return done.stdout.decode("utf-8", "replace").splitlines()


def parse_annotations(lines):
    """
    Annotation lines -> [{start, end, byte, error}], in sample order.

    Unparsable lines are skipped: sigrok prints headers and other classes too.
    """
    events = []
    for line in lines:
        m = ANNOTATION_RE.match(line.strip())
        if not m:
            continue
        text = m.group("text").strip()
        cls = m.group("cls") or ""
        hexes = HEX_RE.findall(text)
        # Without a class name, an error shows only in the text ("frame error").
        is_error = bool(ERROR_TEXT_RE.search(cls)) or (not hexes and
                                                       bool(ERROR_TEXT_RE.search(text)))
        if not hexes and not is_error:
            continue
        events.append({
            "start": int(m.group("start")),
            "end": int(m.group("end")),
            "byte": int(hexes[0], 16) if hexes else None,
            "error": is_error,
            "text": text,
        })
    events.sort(key=lambda e: e["start"])
    return events


def group_messages(events, gap_samples):
    """Split the byte stream wherever the line is idle for longer than gap_samples."""
    messages = []
    current = []
    for event in events:
        if current and event["start"] - current[-1]["end"] > gap_samples:
            messages.append(current)
            current = []
        current.append(event)
    if current:
        messages.append(current)
    return messages


# --------------------------------------------------------------------------
# documents


def epoch_nanos(epoch_seconds):
    """
    Seconds since the epoch -> integer nanoseconds.

    A double holds only ~240 ns of resolution at 2026 epoch values, which is
    coarser than one sample at 50 MSa/s, so everything downstream works in
    integers. The sidecar carries microseconds, so that is the real limit on
    the capture's start time; sample offsets stay exact.
    """
    return int(round(epoch_seconds * 1e6)) * 1000


def iso_nanos(nanos):
    """date_nanos wants an ISO-8601 string; keep all nine digits."""
    whole, rest = divmod(int(nanos), 1000000000)
    stamp = datetime.datetime.fromtimestamp(whole, datetime.timezone.utc)
    return "%s.%09dZ" % (stamp.strftime("%Y-%m-%dT%H:%M:%S"), rest)


def printable(payload):
    out = []
    for b in payload:
        if b == 0x0a:
            out.append("\\n")
        elif b == 0x0d:
            out.append("\\r")
        elif b == 0x09:
            out.append("\\t")
        elif 0x20 <= b < 0x7f:
            out.append(chr(b))
        else:
            out.append("\\x%02x" % b)
    return "".join(out)[:MAX_TEXT]


def core_psf(opts, extra):
    """The psf keys every document carries, whatever is on the bus."""
    psf = {
        "channel": opts.channel,
        "gpio": opts.base_gpio + opts.channel,
        "port": opts.port or ("ch%d" % opts.channel),
        "file": os.path.basename(opts.current_file),
    }
    psf.update({k: v for k, v in extra.items()
                if not k.startswith("_") and v is not None})
    return psf


def uart_fields(opts, extra):
    """UART's own psf fields, as that bus describes itself."""
    return {
        "uart_type": opts.uart_type,
        "baud_rate": extra.get("_baud") or opts.baud,
        "data_bits": 8,
        "parity": "none",
        "stop_bits": 1,
    }


# --------------------------------------------------------------------------
# protocols
#
# The bus is declared before a capture runs and recorded with every burst, so
# nothing here has to guess. Adding I2C or SPI means adding an entry: a decoder
# command line, the annotation classes to ask for, how to group events into
# messages, and the psf fields that bus describes itself with.


class ProtocolSpec:
    def __init__(self, name, decode=None, group=None, fields=None):
        self.name = name                # uppercase, the document's "protocol"
        self.decode = decode            # (path, opts) -> annotation lines; None = raw
        self.group = group              # (events, opts, rate_hz) -> [[event, ...]]
        self.fields = fields or (lambda opts, extra: {})


def _uart_decode(path, opts):
    return decode_uart(path, opts.channel, opts.baud, opts.base_gpio, opts.sigrok)


def _uart_group(events, opts, rate_hz):
    gap_samples = opts.gap_bytes * 10.0 / opts.baud * rate_hz   # 10 bits per byte
    return group_messages(events, gap_samples)


def _raw_fields(opts, extra):
    fields = {}
    if extra.get("_edges") is not None:
        fields["edges"] = extra["_edges"]
    if extra.get("_active_channels") is not None:
        fields["active_channels"] = extra["_active_channels"]
    return fields


PROTOCOLS = {
    "uart": ProtocolSpec("UART", decode=_uart_decode, group=_uart_group,
                         fields=uart_fields),
    "raw": ProtocolSpec("RAW", fields=_raw_fields),
}

# psf is flat, so a protocol field that shadowed a core one would silently
# replace it. Fail at import instead of in production.
for _name, _spec in PROTOCOLS.items():
    class _Probe:
        channel = 0
        base_gpio = 2
        baud = 115200
        port = None
        uart_type = DEFAULT_UART_TYPE
    _clash = set(_spec.fields(_Probe(), {})) & set(CORE_FIELDS)
    assert not _clash, "protocol %s shadows core psf field(s): %s" % (_name, _clash)


def build_document(message, start_ns, rate_hz, opts, extra_psf, spec=None):
    """One decoded message -> one document."""
    spec = spec or PROTOCOLS[opts.protocol]
    first, last = message[0], message[-1]
    payload = bytes(e["byte"] for e in message if e["byte"] is not None)
    errors = [e["text"] for e in message if e["error"]]

    hex_payload = payload.hex()
    truncated = len(hex_payload) > MAX_PAYLOAD_HEX
    if truncated:
        hex_payload = hex_payload[:MAX_PAYLOAD_HEX]

    # psf is flat: the common core, then the declared protocol's own fields
    # beside it. PROTOCOLS guarantees the two never collide.
    psf = core_psf(opts, extra_psf)
    psf.update({
        "byte_count": len(payload),
        "text": printable(payload),
        "error": bool(errors),
        "start_sample": first["start"],
        "end_sample": last["end"],
        "samplerate_hz": int(rate_hz),
    })
    if errors:
        psf["errors"] = errors[:20]
    if truncated:
        psf["payload_truncated"] = True
    psf.update(spec.fields(opts, extra_psf))
    if spec.name == "UART":
        psf["framing_error"] = bool(errors)

    return {
        "timestamp": iso_nanos(start_ns + int(round(first["start"] * 1e9 / rate_hz))),
        "event_type": EVENT_TYPE,
        "protocol": spec.name,
        "duration": int(round((last["end"] - first["start"]) / rate_hz * 1e9)),
        "length_bytes": len(payload),
        "payload_hex": hex_payload,
        "psf": psf,
    }


def build_raw_document(start_ns, rate_hz, opts, extra_psf, samples):
    """
    One burst -> one document, with nothing decoded.

    This is what makes an unknown bus useful: when it started, how long it ran,
    how much it switched and on which lines. Everything here comes from the
    sidecar pi5la already wrote, so no decoder runs at all.
    """
    spec = PROTOCOLS["raw"]
    psf = core_psf(opts, extra_psf)
    psf.update({
        "byte_count": 0,
        "text": "",
        "error": False,
        "start_sample": 0,
        "end_sample": int(samples),
        "samplerate_hz": int(rate_hz),
    })
    psf.update(spec.fields(opts, extra_psf))

    # An undecoded burst is timestamped at its first edge when one was recorded:
    # the pre-roll means the file itself starts earlier than the traffic.
    first_edge = extra_psf.get("first_edge_time_ns")
    return {
        "timestamp": iso_nanos(int(first_edge) if first_edge else start_ns),
        "event_type": EVENT_TYPE,
        "protocol": spec.name,
        "duration": int(round(samples / rate_hz * 1e9)),
        "length_bytes": 0,
        "psf": psf,
    }


def documents_for(path, opts):
    """Every document a capture yields, in burst then sample order."""
    docs = []
    flag_channel = opts.channel
    for sr_path, start_ns, rate_hz, extra in load_meta(path):
        opts.current_file = sr_path
        opts.channel = flag_channel     # one burst's sidecar must not set the next's

        # A burst records the protocol it was captured under. Trust that over
        # the command line: the flag may just be a sweep's default, while the
        # sidecar was written by whoever knew what was on the wire.
        declared = extra.get("_protocol") or opts.protocol
        if declared not in PROTOCOLS:
            sys.stderr.write("%s: unknown protocol %r, shipping it raw\n"
                             % (sr_path, declared))
            declared = "raw"
        if declared != opts.protocol:
            sys.stderr.write("%s: captured as %s, not %s; using %s\n"
                             % (os.path.basename(sr_path), declared, opts.protocol,
                                declared))
        spec = PROTOCOLS[declared]
        if extra.get("_decode_channel") is not None:
            opts.channel = int(extra["_decode_channel"])

        if spec.decode is None:
            samples = extra.get("_samples") or 0
            docs.append(build_raw_document(start_ns, rate_hz, opts, extra, samples))
            continue

        events = parse_annotations(spec.decode(sr_path, opts))
        if not events:
            sys.stderr.write("%s: nothing decoded (check --channel and --baud)\n" % sr_path)
            continue
        for message in spec.group(events, opts, rate_hz):
            docs.append(build_document(message, start_ns, rate_hz, opts, extra, spec))
    return docs


def write_ndjson(docs, path, index=INDEX):
    """
    Bulk format: an action line, then the document, for each doc.

    Written through a temporary file, so a shipper killed mid-write never
    leaves a half-file that the next sweep would upload as if it were whole.
    """
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        for doc in docs:
            f.write(json.dumps({"create": {"_index": index}}) + "\n")
            f.write(json.dumps(doc) + "\n")
    os.replace(tmp, path)
    return path


# --------------------------------------------------------------------------
# upload


def auth_header():
    """Credentials come from the environment, never the command line."""
    api_key = os.environ.get("ES_API_KEY")
    if api_key:
        return "ApiKey " + api_key
    user, password = os.environ.get("ES_USER"), os.environ.get("ES_PASSWORD")
    if user and password:
        import base64
        return "Basic " + base64.b64encode(("%s:%s" % (user, password)).encode()).decode()
    return None


def bulk_upload(ndjson_path, es_url, index=INDEX, timeout=120, insecure=False):
    url = es_url.rstrip("/") + "/_bulk"
    with open(ndjson_path, "rb") as f:
        body = f.read()
    request = urllib.request.Request(url, data=body, method="POST")
    request.add_header("Content-Type", "application/x-ndjson")
    auth = auth_header()
    if auth:
        request.add_header("Authorization", auth)
    context = None
    if insecure and url.startswith("https"):
        import ssl
        context = ssl._create_unverified_context()
    try:
        with urllib.request.urlopen(request, timeout=timeout, context=context) as response:
            result = json.loads(response.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        raise Error("%s returned %d: %s" % (url, e.code,
                                            e.read().decode("utf-8", "replace")[:400]))
    except (urllib.error.URLError, OSError, ValueError) as e:
        raise Error("cannot reach %s: %s" % (url, e))

    failed = [item for item in result.get("items", [])
              if item.get("create", {}).get("status", 200) >= 300]
    return result, failed


# --------------------------------------------------------------------------
# shipping a live monitor session
#
# pi5la spawns this the moment a burst closes, so traffic reaches the cluster
# while the capture is still running rather than after it is killed. There is no
# queue: what is on disk is the work list, which means a crash, a dead cluster
# or a shipper that was still busy all resolve the same way -- the next sweep
# finds the burst and tries again.


def burst_index(sr_path):
    m = re.search(r"burst-(\d+)\.sr$", sr_path)
    return int(m.group(1)) if m else 0


def pending_bursts(directory):
    """
    What in this directory still needs shipping, oldest first.

    Three states, told apart by what is on disk:
      .sr present, sidecar not marked shipped -> decode it and upload
      .ndjson but no .sr                      -> an upload that failed; retry it
      sidecar marked shipped                  -> done, skip
    """
    work = []
    seen = set()
    for sr_path in glob.glob(os.path.join(directory, "burst-*.sr")):
        meta = load_sidecar(sr_path)
        if meta is None:
            continue            # still being written: no sidecar means not final
        seen.add(sr_path)
        if meta.get("shipped"):
            continue
        # A .ndjson beside an unshipped .sr is a decode that already succeeded
        # and an upload that did not. It is written atomically, so if it is
        # there it is complete: re-running sigrok on it would buy nothing.
        ndjson = sr_path + ".ndjson"
        work.append({"sr": sr_path, "ndjson": ndjson,
                     "decode": not os.path.exists(ndjson)})
    for nd in glob.glob(os.path.join(directory, "burst-*.sr.ndjson")):
        sr_path = nd[:-len(".ndjson")]
        if sr_path in seen or os.path.exists(sr_path):
            continue            # the .sr case above already covers it
        work.append({"sr": sr_path, "ndjson": nd, "decode": False})
    work.sort(key=lambda w: burst_index(w["sr"]))
    return work


def take_lock(sr_path):
    """
    Claim a burst, so two overlapping shippers cannot send it twice.

    A lock left behind by a killed shipper would otherwise strand its burst
    forever, so one older than STALE_LOCK_S is taken over.
    """
    lock = sr_path + ".lock"
    try:
        fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
    except FileExistsError:
        try:
            if time.time() - os.path.getmtime(lock) < STALE_LOCK_S:
                return None
            os.unlink(lock)
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
        except OSError:
            return None
    except OSError:
        return None
    os.write(fd, b"%d\n" % os.getpid())
    os.close(fd)
    return lock


def mark_shipped(sr_path, docs, keep_sr):
    """
    Record that a burst reached the cluster, and clean up what it no longer needs.

    The sidecar is the tombstone and always stays: it is what says the burst
    existed and was shipped, even once its samples are gone.
    """
    meta = load_sidecar(sr_path) or {}
    meta["shipped"] = True
    meta["shipped_docs"] = docs
    meta["shipped_at"] = iso_nanos(epoch_nanos(time.time()))
    tmp = sr_path + ".meta.json.tmp"
    with open(tmp, "w") as f:
        json.dump(meta, f, indent=2)
        f.write("\n")
    os.replace(tmp, sr_path + ".meta.json")

    gone = [sr_path + ".ndjson"]
    if not keep_sr:
        # The payload is in the documents now, so the samples need not be kept.
        # A raw burst is the exception: its document carries no payload, and
        # deleting the .sr would throw away the only copy of the waveform.
        gone.append(sr_path)
    for path in gone:
        try:
            os.unlink(path)
        except OSError:
            pass


def log_shipped(directory, sr_path, docs, took):
    line = {
        "burst": os.path.basename(sr_path),
        "documents": docs,
        "took_ms": took,
        "at": iso_nanos(epoch_nanos(time.time())),
    }
    try:
        with open(os.path.join(directory, SHIPPED_LOG), "a") as f:
            f.write(json.dumps(line) + "\n")
    except OSError as e:
        sys.stderr.write("sr2es: cannot append to %s: %s\n" % (SHIPPED_LOG, e))


def ship_directory(directory, opts):
    """
    Ship everything outstanding in a monitor directory. Returns an exit code.

    Whatever fails is left exactly as it was, so the next burst's shipper picks
    it up: a cluster that comes back finds its backlog waiting.
    """
    if not opts.es_url:
        sys.stderr.write("sr2es: --ship needs --es-url\n")
        return 1
    if not auth_header():
        sys.stderr.write("sr2es: no credentials; set ES_API_KEY, or ES_USER and "
                         "ES_PASSWORD\n")
        return 1

    failures = 0
    for work in pending_bursts(directory):
        sr_path = work["sr"]
        lock = take_lock(sr_path)
        if not lock:
            continue            # another shipper has it
        try:
            docs = None
            if work["decode"]:
                opts.current_file = sr_path
                try:
                    docs = documents_for(sr_path, opts)
                except Error as e:
                    sys.stderr.write("sr2es: %s: %s\n" % (os.path.basename(sr_path), e))
                    failures += 1
                    continue
                if not docs:
                    # Nothing on the wire this decoder understood. Saying so once
                    # and marking it shipped beats retrying it on every burst.
                    sys.stderr.write("sr2es: %s: nothing decoded; not retrying\n"
                                     % os.path.basename(sr_path))
                    mark_shipped(sr_path, 0, keep_sr=True)
                    continue
                write_ndjson(docs, work["ndjson"], opts.index)

            try:
                result, failed = bulk_upload(work["ndjson"], opts.es_url, opts.index,
                                             insecure=opts.insecure)
            except Error as e:
                sys.stderr.write("sr2es: %s: %s\n" % (os.path.basename(sr_path), e))
                sys.stderr.write("sr2es: %s is kept for the next sweep\n"
                                 % os.path.basename(work["ndjson"]))
                failures += 1
                continue
            if failed:
                sys.stderr.write("sr2es: %s: %d document(s) rejected; first: %s\n"
                                 % (os.path.basename(sr_path), len(failed),
                                    json.dumps(failed[0]["create"].get("error", {}))[:300]))
                failures += 1
                continue

            n = len(docs) if docs is not None else count_ndjson(work["ndjson"])
            keep = PROTOCOLS[declared_protocol(sr_path, opts)].decode is None
            mark_shipped(sr_path, n, keep_sr=keep)
            log_shipped(directory, sr_path, n, result.get("took", 0))
            print("shipped %s: %d document(s)" % (os.path.basename(sr_path), n))
        finally:
            try:
                os.unlink(lock)
            except OSError:
                pass
    return 1 if failures else 0


def declared_protocol(sr_path, opts):
    """What this burst was captured as; the sidecar knows, the flag guesses."""
    meta = load_sidecar(sr_path) or {}
    name = meta.get("protocol") or opts.protocol
    return name if name in PROTOCOLS else "raw"


def count_ndjson(path):
    """Documents in a bulk file: one action line and one document each."""
    try:
        with open(path) as f:
            return sum(1 for _ in f) // 2
    except OSError:
        return 0


# --------------------------------------------------------------------------


def main():
    p = argparse.ArgumentParser(
        description="Decode a pi5la capture into Elasticsearch documents",
        epilog="Credentials come from ES_API_KEY, or ES_USER and ES_PASSWORD.")
    p.add_argument("capture", nargs="?", help=".sr file, or a monitor session directory")
    p.add_argument("--ship", metavar="DIR",
                   help="ship everything not yet shipped in a live monitor directory, "
                        "deleting what the cluster confirms; this is what pi5la runs "
                        "as each burst closes")
    p.add_argument("--protocol", default="uart", choices=sorted(PROTOCOLS),
                   help="what is on the bus (default uart); a burst's own sidecar "
                        "overrides this")
    p.add_argument("--baud", type=int, default=115200, help="UART baud rate (default 115200)")
    p.add_argument("--channel", type=int, default=0, help="capture channel, 0-7 (default 0)")
    p.add_argument("--base-gpio", type=int, default=2, dest="base_gpio",
                   help="GPIO of channel 0 (default 2)")
    p.add_argument("--gap-bytes", type=float, default=DEFAULT_GAP_BYTES, dest="gap_bytes",
                   help="idle byte-times that end a message (default %g)" % DEFAULT_GAP_BYTES)
    p.add_argument("--uart-type", default=DEFAULT_UART_TYPE, dest="uart_type",
                   help="psf.uart_type, e.g. TTL-3V3, RS-232, RS-485 (default %s)"
                        % DEFAULT_UART_TYPE)
    p.add_argument("--port", help="psf.port label for the probed line (default chN)")
    p.add_argument("--out", help="NDJSON output path (default: next to the capture)")
    p.add_argument("--index", default=INDEX, help="index name (default %s)" % INDEX)
    p.add_argument("--es-url", dest="es_url",
                   help="Elasticsearch base URL; without it, only the NDJSON is written")
    p.add_argument("--insecure", action="store_true",
                   help="skip TLS certificate checks (self-signed clusters)")
    p.add_argument("--sigrok", default="sigrok-cli", help="sigrok-cli command")
    p.add_argument("--print", action="store_true", dest="print_docs",
                   help="also print the documents")
    opts = p.parse_args()

    if not opts.capture and not opts.ship:
        p.error("give a capture, or --ship DIR")
    if opts.capture and opts.ship:
        p.error("give a capture or --ship DIR, not both")
    opts.current_file = opts.capture or opts.ship

    if not 0 <= opts.channel <= 7:
        p.error("--channel must be 0-7")
    if opts.baud <= 0:
        p.error("--baud must be positive")

    if opts.ship:
        return ship_directory(opts.ship, opts)

    try:
        docs = documents_for(opts.capture, opts)
    except Error as e:
        sys.stderr.write("sr2es: %s\n" % e)
        return 1

    if not docs:
        sys.stderr.write("sr2es: no messages decoded\n")
        return 1

    out_path = opts.out or (opts.capture.rstrip("/") + ".ndjson")
    write_ndjson(docs, out_path, opts.index)
    total_bytes = sum(d["length_bytes"] for d in docs)
    print("%d message(s), %d byte(s) -> %s" % (len(docs), total_bytes, out_path))
    if opts.print_docs:
        for doc in docs:
            print(json.dumps(doc, indent=2))

    if not opts.es_url:
        print("no --es-url: upload it yourself, e.g.")
        print("  curl -H 'Content-Type: application/x-ndjson' --data-binary @%s "
              "<url>/_bulk" % out_path)
        return 0

    if not auth_header():
        sys.stderr.write("sr2es: no credentials; set ES_API_KEY, or ES_USER and "
                         "ES_PASSWORD\n")
        return 1
    try:
        result, failed = bulk_upload(out_path, opts.es_url, opts.index,
                                     insecure=opts.insecure)
    except Error as e:
        sys.stderr.write("sr2es: %s\n" % e)
        sys.stderr.write("sr2es: %s is kept; re-send it when the cluster is reachable\n"
                         % out_path)
        return 1

    if failed:
        sys.stderr.write("sr2es: %d of %d documents were rejected; first error: %s\n"
                         % (len(failed), len(docs),
                            json.dumps(failed[0]["create"].get("error", {}))[:300]))
        return 1
    print("uploaded %d document(s) to %s/%s in %s ms" %
          (len(docs), opts.es_url.rstrip("/"), opts.index, result.get("took", "?")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
