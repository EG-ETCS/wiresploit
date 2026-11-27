"""Unit tests for web/args.py. Run: python3 -m unittest tests/test_web_args.py"""

import datetime
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web"))

import args  # noqa: E402


class TestParsing(unittest.TestCase):
    def test_si(self):
        self.assertEqual(args.parse_si("10M"), 10e6)
        self.assertEqual(args.parse_si("115.2k"), 115200)
        self.assertEqual(args.parse_si(" 4000 "), 4000)
        self.assertEqual(args.parse_si("4G"), 4e9)
        for bad in ("", "10T", "-5", "5kk", "M", None, "1e6"):
            with self.assertRaises(args.InvalidInput):
                args.parse_si(bad)

    def test_duration(self):
        self.assertEqual(args.parse_duration("10s"), 10)
        self.assertEqual(args.parse_duration("10"), 10)
        self.assertAlmostEqual(args.parse_duration("500ms"), 0.5)
        self.assertEqual(args.parse_duration("1.5m"), 90)
        for bad in ("0s", "", "5h", "-1s", "s", "1 s"):
            with self.assertRaises(args.InvalidInput):
                args.parse_duration(bad)

    def test_capture_names(self):
        stamp = datetime.datetime(2026, 9, 19, 14, 5, 6)
        self.assertEqual(args.new_capture_name(stamp), "cap-20260919-140506.sr")
        self.assertEqual(args.safe_capture_name("cap-1.sr"), "cap-1.sr")
        for bad in ("../etc/passwd.sr", "a/b.sr", "cap.txt", ".sr", "", "cap.sr\n", None):
            with self.assertRaises(args.InvalidInput):
                args.safe_capture_name(bad)


class TestCaptureCommand(unittest.TestCase):
    def base_form(self, **kw):
        form = {"rate": "2M", "channels": "8"}
        form.update(kw)
        return form

    def test_minimal_runs_until_stopped(self):
        """No duration: the capture runs until Stop."""
        argv, warnings = args.capture_command(self.base_form(), pi5la="/bin/pi5la",
                                              out_path="/c/x.sr")
        self.assertEqual(argv, ["/bin/pi5la", "--rate", "2M", "--channels", "8",
                                "--base-gpio", "2", "--out", "/c/x.sr",
                                "--max-size", "4G"])
        self.assertNotIn("--duration", argv)
        self.assertEqual(warnings, [])

    def test_all_options(self):
        argv, _ = args.capture_command(self.base_form(
            duration="60s", silence_timeout="1s", trigger="falling", trigger_channel="3",
            dma_buf="131072", base_gpio="4", channels="4", max_size="500M"))
        self.assertEqual(argv[argv.index("--duration") + 1], "60s")
        self.assertEqual(argv[argv.index("--silence-timeout") + 1], "1s")
        self.assertEqual(argv[argv.index("--trigger") + 1], "falling:3")
        self.assertEqual(argv[argv.index("--dma-buf") + 1], "131072")
        self.assertEqual(argv[argv.index("--base-gpio") + 1], "4")
        self.assertEqual(argv[argv.index("--max-size") + 1], "500M")
        self.assertNotIn("--idle-stop", argv)

    def test_monitor_mode(self):
        argv, _ = args.capture_command(self.base_form(monitor=True),
                                       out_path="/c/mon-20260921-101500")
        self.assertIn("--monitor", argv)
        self.assertNotIn("--out", argv)
        self.assertEqual(argv[argv.index("--monitor") + 1], "/c/mon-20260921-101500")
        # Burst edges get their documented defaults.
        self.assertEqual(argv[argv.index("--gap") + 1], "100ms")
        self.assertEqual(argv[argv.index("--pre") + 1], "10ms")
        self.assertEqual(argv[argv.index("--post") + 1], "50ms")

        argv, _ = args.capture_command(self.base_form(monitor="true", gap="1s", pre="5ms",
                                                      post="200ms"))
        self.assertEqual(argv[argv.index("--gap") + 1], "1s")
        self.assertEqual(argv[argv.index("--post") + 1], "200ms")

    def test_rejects_bad_input(self):
        cases = [
            {"channels": "3"},                       # not 1/2/4/8
            {"channels": "0"},
            {"rate": "400M"},                        # above the PIO clock
            {"rate": "1"},                           # below the slowest divider
            {"rate": "2M; rm -rf /"},                # shell metacharacters
            {"duration": "0s"},
            {"duration": "later"},
            {"silence_timeout": "0s"},
            {"monitor": True, "gap": "soon"},
            {"max_size": "big"},
            {"base_gpio": "27"},                     # 27 + 8 channels is past GPIO 27
            {"base_gpio": "-1"},
            {"trigger": "sideways"},
            {"trigger": "rising", "trigger_channel": "9"},
            {"channels": "4", "trigger": "rising", "trigger_channel": "5"},
            {"dma_buf": "3"},                        # not a multiple of 4
            {"dma_buf": "134217728"},                # past the CMA pool
        ]
        for extra in cases:
            with self.assertRaises(args.InvalidInput, msg=extra):
                args.capture_command(self.base_form(**extra))

    def test_throughput_warning(self):
        self.assertIsNone(args.throughput_warning(50e6, 8))
        self.assertIsNotNone(args.throughput_warning(66.6e6, 8))
        _, warnings = args.capture_command(self.base_form(rate="100M", duration="1s"))
        self.assertTrue(any("50 MB/s" in w for w in warnings))

    def test_disk_warning(self):
        """Continuous recording is bounded by SD card speed, monitor mode is not."""
        _, warnings = args.capture_command(self.base_form(rate="50M"))
        self.assertTrue(any("SD card" in w for w in warnings))
        _, warnings = args.capture_command(self.base_form(rate="50M", monitor=True))
        self.assertFalse(any("SD card" in w for w in warnings))
        _, warnings = args.capture_command(self.base_form(rate="2M"))
        self.assertFalse(any("SD card" in w for w in warnings))


class TestMonitorStopConditions(unittest.TestCase):
    """
    Monitor mode is meant to sit and watch. A silence timeout or a duration
    quietly ends the whole session, so the page says so before starting rather
    than leaving someone wondering why the recording stopped.
    """

    def test_silence_timeout_is_called_out(self):
        _, warnings = args.capture_command(
            {"rate": "2M", "monitor": "true", "silence_timeout": "1s"},
            "/bin/pi5la", "/c/mon-1")
        self.assertTrue(any("stop by itself after 1s of silence" in w for w in warnings))

    def test_duration_is_called_out(self):
        _, warnings = args.capture_command(
            {"rate": "2M", "monitor": "true", "duration": "30s"}, "/bin/pi5la", "/c/mon-1")
        self.assertTrue(any("stop by itself after 30s" in w for w in warnings))

    def test_plain_monitor_says_nothing(self):
        _, warnings = args.capture_command({"rate": "2M", "monitor": "true"},
                                           "/bin/pi5la", "/c/mon-1")
        self.assertEqual(warnings, [])

    def test_continuous_mode_is_not_warned_about(self):
        """A one-shot recording is exactly what a silence timeout is for."""
        _, warnings = args.capture_command(
            {"rate": "2M", "silence_timeout": "1s"}, "/bin/pi5la", "/c/x.sr")
        self.assertEqual(warnings, [])


class TestShipping(unittest.TestCase):
    """
    Live Elasticsearch export. The rules mirror src/main.c, so the page can
    reject a bad combination before a capture is started.
    """

    def form(self, **kw):
        f = {"rate": "2M", "channels": "8", "monitor": "true", "ship": "true",
             "es_url": "http://es.example:9200"}
        f.update(kw)
        return f

    def ship_args(self, **kw):
        argv, warnings = args.capture_command(self.form(**kw), "/bin/pi5la", "/c/mon-1")
        return argv[argv.index("--es-url"):], warnings

    def test_off_by_default(self):
        argv, _ = args.capture_command({"rate": "2M", "monitor": "true"},
                                       "/bin/pi5la", "/c/mon-1")
        self.assertNotIn("--es-url", argv)
        self.assertNotIn("--protocol", argv)

    def test_uart_forwards_the_decode_settings(self):
        argv, warnings = self.ship_args(ship_protocol="uart", decode_channel="3",
                                        baud="9600", es_index="wiresploit-poc1",
                                        uart_type="RS-232")
        self.assertEqual(argv, ["--es-url", "http://es.example:9200",
                                "--protocol", "uart", "--decode-channel", "3",
                                "--baud", "9600", "--es-index", "wiresploit-poc1",
                                "--uart-type", "RS-232"])
        # http:// to a remote host is worth saying out loud, but is not an error.
        self.assertTrue(any("clear text" in w for w in warnings))

    def test_raw_sends_no_decode_settings(self):
        """Nothing is decoded, so a channel and a baud rate would be meaningless."""
        argv, warnings = self.ship_args(ship_protocol="raw")
        self.assertEqual(argv, ["--es-url", "http://es.example:9200", "--protocol", "raw"])
        self.assertTrue(any("keeps the .sr files" in w for w in warnings))

    def test_raw_says_baud_is_ignored(self):
        _, warnings = self.ship_args(ship_protocol="raw", baud="115200")
        self.assertTrue(any("baud rate is ignored" in w for w in warnings))

    def test_needs_monitor_mode(self):
        """Shipping sends one burst at a time, so there have to be bursts."""
        with self.assertRaises(args.InvalidInput) as cm:
            args.capture_command(self.form(monitor="false"), "/bin/pi5la", "/c/x.sr")
        self.assertIn("monitor", str(cm.exception))

    def test_needs_a_url(self):
        with self.assertRaises(args.InvalidInput):
            args.capture_command(self.form(es_url=""), "/bin/pi5la", "/c/mon-1")

    def test_rejects_bad_input(self):
        for kw in ({"es_url": "ftp://es.example"},
                   {"es_url": "es.example:9200"},
                   {"ship_protocol": "i2c"},          # sigrok knows it, sr2es does not
                   {"ship_protocol": "../../etc"},
                   {"es_index": "Wiresploit"},        # uppercase is not a legal index name
                   {"es_index": "a b"},
                   {"uart_type": "bad;type"},
                   {"baud": "0"},
                   {"decode_channel": "9"}):
            with self.assertRaises(args.InvalidInput, msg=repr(kw)):
                args.capture_command(self.form(**kw), "/bin/pi5la", "/c/mon-1")

    def test_decode_channel_must_be_captured(self):
        with self.assertRaises(args.InvalidInput) as cm:
            args.capture_command(self.form(channels="2", decode_channel="4"),
                                 "/bin/pi5la", "/c/mon-1")
        self.assertIn("decode channel", str(cm.exception))

    def test_warns_about_plain_http_and_insecure_tls(self):
        _, warnings = self.ship_args()
        self.assertTrue(any("clear text" in w for w in warnings))
        _, local = self.ship_args(es_url="http://127.0.0.1:9999")
        self.assertFalse(any("clear text" in w for w in local))
        _, insecure = self.ship_args(es_url="https://es.example:9200", insecure="true")
        self.assertTrue(any("certificate" in w for w in insecure))

    def test_no_credential_ever_reaches_the_command_line(self):
        """
        pi5la's shipper reads ES_API_KEY / ES_USER / ES_PASSWORD from the
        environment. A form field would put a password in the job log, the POST
        body and browser history, so nothing here may forward one.
        """
        argv, _ = args.capture_command(
            self.form(es_password="hunter2", password="hunter2", es_user="analyst",
                      es_api_key="secretkey"),
            "/bin/pi5la", "/c/mon-1")
        joined = " ".join(argv)
        for secret in ("hunter2", "analyst", "secretkey"):
            self.assertNotIn(secret, joined)


class TestCaptureRefs(unittest.TestCase):
    def test_monitor_dir_name(self):
        stamp = datetime.datetime(2026, 9, 21, 10, 15, 0)
        self.assertEqual(args.new_monitor_dir(stamp), "mon-20260921-101500")

    def test_refs(self):
        self.assertEqual(args.safe_capture_ref("cap-1.sr"), "cap-1.sr")
        self.assertEqual(args.safe_capture_ref("mon-20260921-101500/burst-0001.sr"),
                         "mon-20260921-101500/burst-0001.sr")
        for bad in ("../x.sr", "mon-20260921-101500/../../etc/x.sr", "other/burst-1.sr",
                    "mon-bad/burst-1.sr", "a/b/c.sr", "mon-20260921-101500/x.txt", None):
            with self.assertRaises(args.InvalidInput, msg=bad):
                args.safe_capture_ref(bad)


class TestDecoders(unittest.TestCase):
    def test_uart_saved_file(self):
        argv = args.decode_command("/c/cap.sr", {"protocol": "uart", "rx": "0",
                                                 "baudrate": "115200"})
        self.assertEqual(argv, ["sigrok-cli", "-i", "/c/cap.sr", "-P",
                                "uart:rx=GPIO2:baudrate=115200", "-B", "uart=rx"])

    def test_i2c_and_spi(self):
        i2c = args.decode_command("/c/a.sr", {"protocol": "i2c", "scl": "0", "sda": "1"})
        self.assertIn("i2c:scl=GPIO2:sda=GPIO3", i2c)
        spi = args.decode_command("/c/a.sr", {"protocol": "spi", "clk": "0", "mosi": "1",
                                              "cs": "3"})
        self.assertIn("spi:clk=GPIO2:mosi=GPIO3:cs=GPIO5", spi)

    def test_live_uses_numeric_channel_names(self):
        capture, decode, warnings = args.live_commands(
            {"rate": "2M", "channels": "8", "protocol": "uart", "rx": "0",
             "baudrate": "115200"}, pi5la="/bin/pi5la")
        self.assertEqual(capture, ["/bin/pi5la", "--rate", "2M", "--channels", "8",
                                   "--base-gpio", "2", "--stream"])
        self.assertIn("binary:numchannels=8:samplerate=2000000", decode)
        self.assertIn("uart:rx=0:baudrate=115200", decode)
        self.assertTrue(any("decoders run in Python" in w for w in warnings))

    def test_rejects_bad_decoders(self):
        cases = [
            {"protocol": "morse", "rx": "0"},
            {"protocol": "uart"},                                  # no rx
            {"protocol": "uart", "rx": "8"},                       # out of range
            {"protocol": "uart", "rx": "0", "baudrate": "0"},
            {"protocol": "i2c", "scl": "0", "sda": "0"},           # same channel
            {"protocol": "i2c", "scl": "0"},                       # no sda
            {"protocol": "spi", "clk": "0"},                       # no data channel
            {"protocol": "spi", "clk": "0", "mosi": "0"},          # duplicate channel
        ]
        for form in cases:
            with self.assertRaises(args.InvalidInput, msg=form):
                args.decode_command("/c/a.sr", form)


if __name__ == "__main__":
    unittest.main()
