"""
Input validation and argv building for the pi5la web UI.

Everything here is pure: no Flask, no subprocess, no filesystem. The web app
turns form fields into argument lists through these functions, so nothing a
browser sends is ever handed to a shell. Unit tests live in
tests/test_web_args.py.

The rules mirror pi5la's own parsers: parse_si / parse_duration in src/util.c
and src/idle.c, and the option checks in src/main.c.
"""

import datetime
import re

MAX_GPIO = 28              # CAPTURE_GPIO_COUNT
VALID_CHANNELS = (1, 2, 4, 8)
PIO_CLOCK_HZ = 200e6
MIN_RATE_HZ = PIO_CLOCK_HZ / 65535.999   # largest divider
MAX_DMA_BUF = 8 << 20      # 4 x 8 MiB is the largest ring the 64 MB CMA pool allows
RELIABLE_BYTES_PER_S = 50e6  # see REPORT.md: 50 MB/s packed ran clean 15/15

CAPTURE_NAME_RE = re.compile(r"^[A-Za-z0-9._-]+\.sr$")
MONITOR_DIR_RE = re.compile(r"^mon-\d{8}-\d{6}$")
DEFAULTS = {"gap": "100ms", "pre": "10ms", "post": "50ms", "max_size": "4G"}
TRIGGERS = ("rising", "falling", "high", "low")
PROTOCOLS = ("uart", "i2c", "spi")          # sigrok decoders, for the live/decode views
SHIP_PROTOCOLS = ("uart", "raw")            # what sr2es can ship; see tools/sr2es.py
ES_URL_RE = re.compile(r"^https?://[^\s/]+(/.*)?$")
INDEX_NAME_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")   # Elasticsearch index rules


class InvalidInput(ValueError):
    """Raised with a message meant to be shown in the page."""


def _fail(what, value):
    raise InvalidInput("invalid %s: %r" % (what, value))


def parse_si(text, what="rate"):
    """Number with an optional k/M/G suffix, as src/util.c parse_si does."""
    if not isinstance(text, str):
        _fail(what, text)
    s = text.strip()
    mult = 1.0
    if s.endswith("k"):
        mult, s = 1e3, s[:-1]
    elif s.endswith("M"):
        mult, s = 1e6, s[:-1]
    elif s.endswith("G"):
        mult, s = 1e9, s[:-1]
    if not re.fullmatch(r"\d+(\.\d+)?", s):
        _fail(what, text)
    return float(s) * mult


def parse_duration(text, what="duration"):
    """Seconds from '500ms', '10s', '2m', or a bare number of seconds."""
    if not isinstance(text, str):
        _fail(what, text)
    s = text.strip()
    mult = 1.0
    if s.endswith("ms"):
        mult, s = 1e-3, s[:-2]
    elif s.endswith("m"):
        mult, s = 60.0, s[:-1]
    elif s.endswith("s"):
        s = s[:-1]
    if not re.fullmatch(r"\d+(\.\d+)?", s):
        _fail(what, text)
    value = float(s) * mult
    if value <= 0:
        _fail(what, text)
    return value


def _flag(value):
    """Checkbox-ish value from JSON: true, "true", "on", 1."""
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in ("true", "on", "1", "yes")


def _int(value, what, lo, hi):
    try:
        n = int(str(value).strip())
    except (TypeError, ValueError):
        _fail(what, value)
    if n < lo or n > hi:
        raise InvalidInput("%s must be between %d and %d, got %d" % (what, lo, hi, n))
    return n


def new_capture_name(now=None):
    """Server-side name, so no client string ever becomes a path."""
    now = now or datetime.datetime.now()
    return now.strftime("cap-%Y%m%d-%H%M%S.sr")


def new_monitor_dir(now=None):
    """Directory for one monitor session: burst-NNNN.sr files plus index.json."""
    now = now or datetime.datetime.now()
    return now.strftime("mon-%Y%m%d-%H%M%S")


def safe_capture_name(name):
    """A plain *.sr basename, with no path separators or traversal."""
    if not isinstance(name, str) or not CAPTURE_NAME_RE.fullmatch(name) or name.startswith("."):
        _fail("capture name", name)
    return name


def safe_capture_ref(ref):
    """
    A capture the browser may name: either a plain "<file>.sr" or one burst
    inside a monitor session, "mon-YYYYmmdd-HHMMSS/<file>.sr". Anything else,
    including separators or traversal, is rejected.
    """
    if not isinstance(ref, str):
        _fail("capture name", ref)
    parts = ref.split("/")
    if len(parts) == 1:
        return safe_capture_name(parts[0])
    if len(parts) == 2 and MONITOR_DIR_RE.fullmatch(parts[0]):
        return "%s/%s" % (parts[0], safe_capture_name(parts[1]))
    _fail("capture name", ref)


def throughput_warning(rate_hz, channels):
    """Warn above the packed data rate that has been proven reliable."""
    packed = rate_hz * channels / 8.0
    if packed <= RELIABLE_BYTES_PER_S:
        return None
    return (
        "%.0f MB/s of packed data is above the 50 MB/s that ran clean 15/15 on this Pi; "
        "samples may be dropped (see REPORT.md)." % (packed / 1e6)
    )


def capture_command(form, pi5la="pi5la", out_path="capture.sr"):
    """
    Build the pi5la argv for a capture.

    The capture runs until stopped unless a cap is given, so `duration` is
    optional. form keys: rate (required), channels, base_gpio, duration,
    silence_timeout, monitor (bool), gap, pre, post, max_size, trigger,
    trigger_channel, dma_buf. `out_path` is the .sr file, or the session
    directory when monitoring. Returns (argv, warnings).
    """
    rate_hz = parse_si(form.get("rate", ""), "rate")
    if rate_hz < MIN_RATE_HZ or rate_hz > PIO_CLOCK_HZ:
        raise InvalidInput(
            "rate must be between %.0f Hz and 200 MHz, got %.0f Hz" % (MIN_RATE_HZ, rate_hz))

    channels = _int(form.get("channels", 8), "channels", 1, 8)
    if channels not in VALID_CHANNELS:
        raise InvalidInput("channels must be 1, 2, 4 or 8, got %d" % channels)

    base = _int(form.get("base_gpio", 2), "base GPIO", 0, MAX_GPIO - 1)
    if base + channels > MAX_GPIO:
        raise InvalidInput(
            "GPIO %d..%d is past the last PIO GPIO (%d)" % (base, base + channels - 1, MAX_GPIO - 1))

    monitor = _flag(form.get("monitor"))
    argv = [
        pi5la,
        "--rate", form["rate"].strip(),
        "--channels", str(channels),
        "--base-gpio", str(base),
        "--monitor" if monitor else "--out", out_path,
    ]

    duration_s = 0.0
    duration = form.get("duration", "")
    if duration and duration.strip():
        duration_s = parse_duration(duration, "duration")
        argv += ["--duration", duration.strip()]

    silence = form.get("silence_timeout", "")
    if silence and silence.strip():
        parse_duration(silence, "silence timeout")   # pi5la parses it again
        argv += ["--silence-timeout", silence.strip()]

    if monitor:
        for key, flag in (("gap", "--gap"), ("pre", "--pre"), ("post", "--post")):
            value = form.get(key, "") or DEFAULTS[key]
            parse_duration(value, key)
            argv += [flag, value.strip()]

    max_size = form.get("max_size", "") or DEFAULTS["max_size"]
    if parse_si(max_size, "max size") <= 0:
        _fail("max size", max_size)
    argv += ["--max-size", max_size.strip()]

    trigger = form.get("trigger", "")
    if trigger and trigger.strip() and trigger != "none":
        if trigger not in TRIGGERS:
            _fail("trigger", trigger)
        ch = _int(form.get("trigger_channel", 0), "trigger channel", 0, 7)
        if ch >= channels:
            raise InvalidInput("trigger channel %d is not below channels %d" % (ch, channels))
        argv += ["--trigger", "%s:%d" % (trigger, ch)]

    dma_buf = form.get("dma_buf", "")
    if dma_buf and str(dma_buf).strip():
        n = _int(dma_buf, "DMA buffer", 4, MAX_DMA_BUF)
        if n % 4:
            raise InvalidInput("DMA buffer must be a multiple of 4, got %d" % n)
        argv += ["--dma-buf", str(n)]

    warnings = []
    if monitor and silence and silence.strip():
        warnings.append(
            "monitor mode will stop by itself after %s of silence; clear the silence "
            "timeout to keep recording until you press Stop." % silence.strip())
    if monitor and duration and duration.strip():
        warnings.append(
            "monitor mode will stop by itself after %s; clear the max duration to keep "
            "recording until you press Stop." % duration.strip())
    argv += _ship_args(form, monitor, channels, warnings)
    warning = throughput_warning(rate_hz, channels)
    if warning:
        warnings.append(warning)
    disk_rate = rate_hz * channels / 8.0
    if not monitor and disk_rate > 20e6:
        warnings.append(
            "continuous recording writes %.0f MB/s to the SD card, which it may not "
            "sustain; use monitor mode or a lower rate." % (disk_rate / 1e6))
    if duration_s and not monitor and duration_s * disk_rate > 4e9:
        warnings.append("this capture would write over 4 GB; --max-size will stop it first.")
    return argv, warnings


def _ship_args(form, monitor, channels, warnings):
    """
    Live Elasticsearch export flags, or nothing when it is switched off.

    Mirrors the checks in src/main.c so the page reports a bad combination
    before a capture is started rather than after pi5la refuses it.

    Credentials are deliberately absent: pi5la's shipper reads ES_API_KEY, or
    ES_USER and ES_PASSWORD, from the environment it inherits from this server.
    A password travelling through a form field would end up in POST bodies,
    browser history and the job log.
    """
    if not _flag(form.get("ship")):
        return []

    es_url = str(form.get("es_url", "")).strip()
    if not es_url:
        raise InvalidInput("give an Elasticsearch URL, or turn off sending to it")
    if not ES_URL_RE.fullmatch(es_url):
        raise InvalidInput("Elasticsearch URL must look like http://host:9200, got %r"
                           % es_url)
    if not monitor:
        raise InvalidInput(
            "sending to Elasticsearch ships one burst at a time, so it needs monitor mode")

    protocol = str(form.get("ship_protocol", "uart")).strip() or "uart"
    if protocol not in SHIP_PROTOCOLS:
        _fail("protocol", protocol)

    argv = ["--es-url", es_url, "--protocol", protocol]

    if protocol == "raw":
        if str(form.get("baud", "")).strip():
            warnings.append("baud rate is ignored with the raw protocol: nothing is decoded.")
        warnings.append(
            "raw ships one document per burst and keeps the .sr files, because a raw "
            "document carries no payload.")
    else:
        channel = _int(form.get("decode_channel", 0), "decode channel", 0, 7)
        if channel >= channels:
            raise InvalidInput("decode channel %d is not below channels %d"
                               % (channel, channels))
        baud = _int(form.get("baud", 115200), "baud rate", 50, 20000000)
        argv += ["--decode-channel", str(channel), "--baud", str(baud)]

    index = str(form.get("es_index", "")).strip()
    if index:
        if not INDEX_NAME_RE.fullmatch(index) or len(index) > 255:
            _fail("index name", index)
        argv += ["--es-index", index]

    uart_type = str(form.get("uart_type", "")).strip()
    if uart_type:
        if not re.fullmatch(r"[A-Za-z0-9 ._+/-]{1,40}", uart_type):
            _fail("UART type", uart_type)
        argv += ["--uart-type", uart_type]

    if _flag(form.get("insecure")):
        argv.append("--insecure")
        warnings.append("TLS certificate checks are off for this upload.")

    if es_url.startswith("http://") and not es_url.startswith("http://127.0.0.1"):
        warnings.append(
            "http:// sends the documents and the credentials in clear text over the network.")
    return argv


def live_commands(form, pi5la="pi5la", sigrok="sigrok-cli"):
    """
    Build the two argv lists for live decoding: pi5la --stream piped into
    sigrok-cli. Same pipeline as tools/uart-listen.sh, without the shell.

    In the binary input, channels are named 0..7, not GPIO2..GPIO9.
    """
    rate_hz = parse_si(form.get("rate", ""), "rate")
    if rate_hz < MIN_RATE_HZ or rate_hz > PIO_CLOCK_HZ:
        raise InvalidInput("rate must be between %.0f Hz and 200 MHz" % MIN_RATE_HZ)
    channels = _int(form.get("channels", 8), "channels", 1, 8)
    if channels not in VALID_CHANNELS:
        raise InvalidInput("channels must be 1, 2, 4 or 8, got %d" % channels)
    base = _int(form.get("base_gpio", 2), "base GPIO", 0, MAX_GPIO - 1)
    if base + channels > MAX_GPIO:
        raise InvalidInput("GPIO %d..%d is past the last PIO GPIO" % (base, base + channels - 1))

    capture = [
        pi5la,
        "--rate", form["rate"].strip(),
        "--channels", str(channels),
        "--base-gpio", str(base),
        "--stream",
    ]
    decoder = _decoder_spec(form, channels, name=lambda ch: str(ch))
    decode = [
        sigrok,
        "-I", "binary:numchannels=%d:samplerate=%d" % (channels, round(rate_hz)),
        "-i", "-",
        "-P", decoder,
    ] + _annotation_args(form.get("protocol", "uart"))
    warnings = []
    warning = throughput_warning(rate_hz, channels)
    if warning:
        warnings.append(warning)
    warnings.append(
        "sigrok's decoders run in Python: above a few MSa/s the pipe falls behind and the "
        "DMA ring overflows.")
    return capture, decode, warnings


def decode_command(path, form, channels=8, base_gpio=2, sigrok="sigrok-cli"):
    """argv to decode a saved .sr file. Channels there are named GPIO<n>."""
    decoder = _decoder_spec(form, channels, name=lambda ch: "GPIO%d" % (base_gpio + ch))
    return [sigrok, "-i", path, "-P", decoder] + _annotation_args(form.get("protocol", "uart"))


def _channel(form, key, channels, required=True, default=None):
    value = form.get(key, "")
    if value is None or str(value).strip() == "":
        if required:
            raise InvalidInput("%s channel is required" % key)
        return default
    ch = _int(value, "%s channel" % key, 0, 7)
    if ch >= channels:
        raise InvalidInput("%s channel %d is not below channels %d" % (key, ch, channels))
    return ch


def _decoder_spec(form, channels, name):
    """sigrok -P argument for the chosen protocol, e.g. uart:rx=GPIO2:baudrate=115200."""
    protocol = form.get("protocol", "uart")
    if protocol not in PROTOCOLS:
        _fail("protocol", protocol)

    if protocol == "uart":
        rx = _channel(form, "rx", channels)
        baud = _int(form.get("baudrate", 115200), "baud rate", 50, 20000000)
        return "uart:rx=%s:baudrate=%d" % (name(rx), baud)

    if protocol == "i2c":
        scl = _channel(form, "scl", channels)
        sda = _channel(form, "sda", channels)
        if scl == sda:
            raise InvalidInput("SCL and SDA must be different channels")
        return "i2c:scl=%s:sda=%s" % (name(scl), name(sda))

    clk = _channel(form, "clk", channels)
    spec = "spi:clk=%s" % name(clk)
    used = [clk]
    for key in ("mosi", "miso", "cs"):
        ch = _channel(form, key, channels, required=False)
        if ch is None:
            continue
        if ch in used:
            raise InvalidInput("SPI channels must all be different")
        used.append(ch)
        spec += ":%s=%s" % (key, name(ch))
    if len(used) == 1:
        raise InvalidInput("SPI needs MOSI or MISO as well as CLK")
    return spec


def _annotation_args(protocol):
    if protocol == "uart":
        return ["-B", "uart=rx"]
    if protocol == "i2c":
        return ["-A", "i2c"]
    return ["-A", "spi"]
