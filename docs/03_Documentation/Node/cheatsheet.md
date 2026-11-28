# Cheatsheet

Day-to-day commands for pi5la on a Raspberry Pi 5. `<pi-host>` is your Pi's
hostname or IP address, and `<install_dir>` is the folder pi5la is installed in
on the Pi. Put your own values in your gitignored `AGENTS.local.md`, not in
shared files.

!!! danger "3.3 V only"
    Pi 5 GPIO is **not 5 V tolerant**. Overvoltage destroys RP1, which is
    soldered to the board, so the Pi is lost. External targets need a
    **74LVC245 buffer, series resistors and a shared ground**. The only safe
    bare-wire test is a loopback jumper, for example GPIO 20 (pin 38) → GPIO 2
    (pin 3). Never connect pin 1 (3.3 V) or pins 2 and 4 (5 V) to a GPIO.

Pins: channels 0–7 = GPIO 2–9 = pins 3, 5, 7, 29, 31, 26, 24, 21. Generator =
GPIO 20 = pin 38. UART TX = GPIO 14 = pin 8. GND = pins 6, 9, 14, 20, 25, 30,
34, 39. Full table: [capture guide](capture_guide.md#channels).

---

## Build, deploy, set up

On your laptop, in the dev container:

```bash
node/scripts/test.sh                 # unit + integration tests
node/scripts/build_pi.sh             # cross-compile into node/capture/build-arm64/
node/scripts/deploy_to_pi.sh         # rsync the install folder to <pi-host>
node/scripts/deploy_to_pi.sh --dry-run
```

On the Pi:

```bash
<install_dir>/scripts/setup_pi.sh            # packages, permissions, node.conf
<install_dir>/scripts/setup_pi.sh --check    # only report what is missing
```

## Connect

```bash
ssh <pi-host>            # set up key auth first: ssh-copy-id <pi-host>
```

`sudo` on the Pi asks for your account password when needed. pi5la itself never
needs `sudo`: your user must be in the `gpio` group.

---

## CLI

Channel *k* = GPIO `base + k`; the default base is GPIO 2, so ch0 = GPIO 2 …
ch7 = GPIO 9.

```bash
cd <install_dir>/bin

# One capture until Ctrl-C
./pi5la --rate 2M --out capture.sr

# Stop automatically once the line goes quiet (keeps a 10 ms lead-in)
./pi5la --rate 2M --out capture.sr --silence-timeout 1s

# Bounded captures
./pi5la --rate 2M --out capture.sr --duration 200ms
./pi5la --rate 2M --out capture.sr --samples 5M

# Loopback self-test (drives GPIO 20; jumper GPIO 20 → GPIO 2 to see it)
./pi5la --rate 2M --out selftest.sr --duration 200ms --generator 1M

# Monitor mode: one .sr per burst, idle skipped (details: monitor mode page)
./pi5la --rate 2M --monitor <install_dir>/captures/mon1 --gap 100ms --pre 10ms --post 50ms
./pi5la --rate 2M --monitor <install_dir>/captures/mon1 --dma-buf 262144   # about 130 ms burst latency

# Wait for a trigger on a channel before sampling
./pi5la --rate 2M --out capture.sr --trigger rising:0

# Fewer channels or a different base pin
./pi5la --rate 10M --out capture.sr --channels 1 --base-gpio 2

# Live stream into a decoder (refuses to write to a terminal: always pipe it)
./pi5la --rate 2M --stream | sigrok-cli -I binary:numchannels=8:samplerate=2000000 -i - ...
```

### Key flags

| Flag | Meaning |
|---|---|
| `--rate RATE` | Sample rate, `k` or `M` suffix (8-channel sustained ceiling: 50 MSa/s) |
| `--out FILE.sr` | Write one sigrok session file |
| `--monitor DIR` | Record only while the bus is active, one `.sr` per burst |
| `--silence-timeout T` | Stop after the line has been quiet for T (`500ms`, `2s`, `1m`) |
| `--gap` / `--pre` / `--post T` | Burst end silence / lead-in / tail (monitor mode) |
| `--duration T` / `--samples N` | Optional stop caps |
| `--channels N` | 1, 2, 4 or 8 (default 8) |
| `--base-gpio G` | First capture GPIO (pins are consecutive) |
| `--trigger T:N` | `rising`, `falling`, `high` or `low` on channel N: wait before sampling |
| `--generator FREQ` | Square wave on GPIO 20 while capturing (loopback) |
| `--stream` | Write samples to stdout as they arrive (pipe into a decoder) |
| `--raw FILE --mode naive` | M2 baseline; needs `--samples` or `--duration` |

**Exit codes:** `0` OK, `1` error, `2` dropped samples. A healthy run reports
`stall events: 0`.

---

## Testing UART capture (loopback: GPIO 14 pin 8 → GPIO 2 pin 3)

The Pi's own UART TX (GPIO 14, pin 8) can be the test signal for channel 0
(GPIO 2). A bare jumper carries nothing: the Pi must transmit, and the GPIO UART
must be enabled.

```bash
# 1. Enable the GPIO UART now (no reboot; lost on reboot)
sudo dtoverlay uart0                    # GPIO 14 = TXD0, GPIO 15 = RXD0, creates /dev/ttyAMA0
stty -F /dev/ttyAMA0 115200 raw -echo

# 2. Generate test traffic (leave running in one terminal)
n=0; while sleep 1; do n=$((n+1)); printf 'pi5la test message %d\r\n' "$n" > /dev/ttyAMA0; done
#    or once:  printf 'hello\r\n' > /dev/ttyAMA0

# 3a. Watch it live
<install_dir>/bin/uart-listen                                   # channel 0 = GPIO 2, 115200, 2M
# 3b. ...or capture and decode a file
<install_dir>/bin/pi5la --rate 2M --out /tmp/u.sr --channels 1 --silence-timeout 500ms
sigrok-cli -i /tmp/u.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx
```

To keep the UART across reboots, let `setup_pi.sh` enable it, or add
`dtparam=uart0=on` to `/boot/firmware/config.txt` and reboot.

**In the test page, Live decode:** protocol UART, channel 0 (= GPIO 2), baud
115200, rate 2M, then Start, then make sure traffic is flowing. In the live view
sigrok names channels `0..7`, so the decoder uses `rx=0`; in a saved `.sr` it is
`rx=GPIO2`.

Live decode runs sigrok's Python decoder: keep the rate modest, about 16–20× the
baud rate (2M for 115200). Too high and the pipe falls behind, and pi5la stops
with a DMA ring overflow.

**A real external device:** wire the target's TX to GPIO 2 and share GND.
**3.3 V only**: anything at 5 V needs a 74LVC245 buffer and series resistors,
or it destroys RP1.

## Checking the wiring

Wrong jumper pins have caused most lost test sessions. Check before every
loopback test:

```bash
# Generator loopback (pin 38 → a channel): the jumpered column must
# alternate together with the last column (GPIO 20).
<install_dir>/bin/pi5la --generator 1 --generator-only &
for i in 1 2 3 4 5 6; do pinctrl lev 2-9,20; sleep 0.5; done
kill %1

# UART loopback (pin 8 → pin 3): a pull-up makes GPIO 2 read high either way,
# so check against real traffic. Send NULs at 300 baud and poll both pins;
# the two columns must move together.
stty -F /dev/ttyAMA0 300 raw -echo
head -c 200 /dev/zero > /dev/ttyAMA0 &
for i in 1 2 3 4 5 6; do pinctrl lev 2,14; sleep 0.1; done
```

- Pin 8 is the 4th hole of the even (outer) row counted from the USB-C end. The
  1st and 2nd holes of that row are 5 V: never use them.
- A capture of all `0x7f7f7f7f` words means no signal reached any channel:
  check the jumper.
- After a reboot, `pinctrl lev 2` prints `-` until pi5la has run once.

## Loopback self-tests

`<install_dir>/bin/test_pi.sh` runs the whole list below and prints PASS or FAIL
for each, after asking you to confirm the wiring. By hand:

```bash
cd <install_dir>/bin
./fdebugtest                                                     # stall flag check, no wiring
./bitprobe --verify 0 --generator 100k --rate 10M --samples 16M  # jumper pin 38 → pin 3
./ratesweep --out ratesweep.csv                                  # same jumper
```

---

## Capture test page

A local testing tool only, with **no authentication**: keep it on a trusted
bench network, or bind it to `127.0.0.1` and use a tunnel.

```bash
# On the Pi: start it detached, so it survives the ssh session
cd <install_dir> && setsid python3 web_ui/app.py >captures/web_ui.log 2>&1 </dev/null &

# Is it up? (from your laptop)
curl -s -o /dev/null -w '%{http_code}\n' http://<pi-host>:9090/

# Log and process
ssh <pi-host> 'pgrep -af web_ui/app.py; tail -f <install_dir>/captures/web_ui.log'

# Stop it
ssh <pi-host> 'pkill -f web_ui/app.py'

# Localhost only, through a tunnel
python3 <install_dir>/web_ui/app.py --host 127.0.0.1      # on the Pi
ssh -N -L 9090:127.0.0.1:9090 <pi-host>                   # on your laptop, then open http://localhost:9090
```

Bind address and port default to `WEB_UI_HOST` and `WEB_UI_PORT` in
`node.conf`.

**Start at boot (optional):** create a systemd user unit, for example
`pi5la-web.service` in your user's `systemd/user/` config folder, with
`ExecStart=/usr/bin/python3 <install_dir>/web_ui/app.py`. Then:

```bash
systemctl --user enable --now pi5la-web
sudo loginctl enable-linger "$USER"      # start at boot without a login
```

---

## Decode a capture

```bash
# UART on ch0 (GPIO 2) at 115200
sigrok-cli -i capture.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx

# Inspect a .sr
sigrok-cli -i capture.sr --show

# Live UART listener (copied next to pi5la; needs sigrok-cli)
<install_dir>/bin/uart-listen --rate 2M --baud 115200
```

## Elasticsearch export (optional)

```bash
# Credentials: from node.conf, or exported (the environment wins); never on the command line
export ES_USER ES_PASSWORD           # or ES_API_KEY
# --es-url and --sigrok default to ES_URL and SIGROK_CLI in node.conf
<install_dir>/bin/sr2es capture.sr --baud 115200                 # -> capture.sr.ndjson
<install_dir>/bin/sr2es capture.sr --es-url https://es.example:9200
<install_dir>/bin/sr2es --ship <install_dir>/captures/mon1       # ship what is not sent yet
```
