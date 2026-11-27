# pi5la on `mopi` — command cheatsheet

Raspberry Pi 5 logic analyzer, GPIO 2–9. Built at `~/pi5la/build` on `mopi`.
Workstation repo `/home/elansari/wiresploit/poc/pi5la` is the source of truth.

> ⚠️ **3.3 V only — Pi 5 GPIO is NOT 5 V-tolerant.** Overvoltage kills the RP1
> (soldered → dead board). External targets need a 74LVC245 buffer + series
> resistors + shared ground. The only safe bare-wire test is a loopback jumper
> (e.g. GPIO 20 → GPIO 2).

---

## Connect

```bash
ssh <pi-host>            # set up key auth from your workstation (ssh-copy-id)
# sudo on the Pi will prompt for your account password when needed
```

---

## Web UI

Currently **running** on `mopi`, port **9090**, bound to all interfaces.
Open from the workstation:

```
http://mopi:9090          http://mopi.local:9090          http://100.127.156.126:9090
```

⚠️ **No authentication** — anyone who can reach the Pi can start captures and
download files. Keep it on the trusted LAN / Tailscale only.

**What the page does:** set up a capture (continuous or monitor), rate, channels,
base GPIO, silence timeout, caps, trigger, with UART/I²C/SPI presets → Start/Stop
(Stop = SIGINT, partial file is kept) → live output streams in → list, download,
delete, or decode saved `.sr` files. Monitor mode can live-ship each burst to
Elasticsearch (credentials come from the server's env, never the browser).

### Manage the web UI process

```bash
# is it up?
curl -s -o /dev/null -w '%{http_code}\n' http://mopi:9090/
ssh mopi 'pgrep -af app.py; tail -f ~/pi5la-web.log'      # log

# stop it
ssh mopi 'pkill -f "app.py.*9090"'

# start it again (detached, survives the ssh session)
ssh mopi 'cd ~/pi5la/web && setsid ~/pi5la-ui-venv/bin/python app.py \
    --pi5la ~/pi5la/build/pi5la --captures ~/captures \
    --host 0.0.0.0 --port 9090 >~/pi5la-web.log 2>&1 </dev/null &'
```

**Auto-start on boot (optional, needs `sudo` — do it yourself):** a user unit is
already written at `~/.config/systemd/user/pi5la-web.service`. Enable it with:

```bash
ssh -t mopi 'systemctl --user enable --now pi5la-web && \
             sudo loginctl enable-linger ansari'    # linger = start at boot w/o login
```

Localhost-only + SSH tunnel instead of exposing it:

```bash
# on mopi start with --host 127.0.0.1, then from the workstation:
ssh -N -L 9090:127.0.0.1:9090 mopi   # open http://localhost:9090
```

---

## CLI (`~/pi5la/build/pi5la`)

Runs as a normal user (no `sudo`; `ansari` is in the `gpio` group).
Channel *k* = GPIO `base+k`; default base = GPIO 2, so ch0=GPIO2 … ch7=GPIO9.

```bash
cd ~/pi5la/build

# One capture until Ctrl-C
./pi5la --rate 2M --out capture.sr

# Stop automatically once the line goes quiet (keeps 10ms lead-in)
./pi5la --rate 2M --out capture.sr --silence-timeout 1s

# Bounded caps
./pi5la --rate 2M --out capture.sr --duration 200ms
./pi5la --rate 2M --out capture.sr --samples 5M

# Loopback self-test (drives GPIO20; jumper GPIO20→GPIO2 to actually see it)
./pi5la --rate 2M --out selftest.sr --duration 200ms --generator 1M

# Monitor mode: one .sr per burst, idle skipped  (full detail: MONITOR.md)
./pi5la --rate 2M --monitor ~/captures/mon1 --gap 100ms --pre 10ms --post 50ms
./pi5la --rate 2M --monitor ~/captures/mon1 --dma-buf 262144   # ~130ms burst latency

# Wait for a trigger on a channel before sampling
./pi5la --rate 2M --out capture.sr --trigger r:0     # rising edge on ch0

# Fewer channels / different base pin
./pi5la --rate 10M --out capture.sr --channels 1 --base-gpio 2

# Live stream into a decoder (refuses to write to a terminal — always pipe it)
./pi5la --rate 2M --stream | sigrok-cli -i - ...
```

### Key flags

| Flag | Meaning |
|---|---|
| `--rate RATE` | sample rate, `k`/`M` suffix (8-ch sustained ceiling ~50 MSa/s) |
| `--out FILE.sr` | write one sigrok session file |
| `--monitor DIR` | record only while the bus is active, one `.sr` per burst |
| `--silence-timeout T` | stop after the line has been quiet for T (`500ms`,`2s`,`1m`) |
| `--gap/--pre/--post T` | burst gap / lead-in / tail (monitor mode) |
| `--duration T` / `--samples N` | optional stop caps |
| `--channels N` | 1, 2, 4 or 8 (default 8) |
| `--base-gpio G` | first capture GPIO (pins are consecutive) |
| `--trigger T:N` | `r`/`f`/`h`/`l` : channel N — wait before sampling |
| `--generator FREQ` | square wave on GPIO 20 while capturing (loopback) |
| `--stream` | write samples to stdout as they arrive (pipe into a decoder) |
| `--raw FILE --mode naive` | M2 baseline; needs `--samples`/`--duration` |

**Exit codes:** `0` ok · `1` error · `2` dropped samples. A healthy run reports
`stall events: 0`.

---

## Testing UART sniffing (loopback: GPIO14 pin8 → GPIO2 pin3)

The Pi's own UART TX (GPIO14, pin 8) can be the test signal source for the
sniffer on GPIO2 (channel 0). A bare jumper carries nothing — the Pi must
transmit, and the GPIO UART must be enabled.

```bash
# 1. Enable the GPIO UART live (no reboot; reverts on reboot — see below)
ssh mopi 'sudo dtoverlay uart0'          # GPIO14=TXD0, GPIO15=RXD0, creates /dev/ttyAMA0

# 2. Generate test traffic (leave running in one terminal)
ssh mopi '~/uart-test-traffic.sh'        # "pi5la test message N" every 1s @115200
#   or a one-off:  printf 'hello\r\n' > /dev/ttyAMA0   (after: stty -F /dev/ttyAMA0 115200 raw -echo)

# 3a. Watch it live on the CLI
ssh mopi '~/pi5la/build/uart-listen.sh'              # channel 0 = GPIO2, 115200, 2M
# 3b. …or capture + decode a file
ssh mopi '~/pi5la/build/pi5la --rate 2M --out /tmp/u.sr --channels 1 --silence-timeout 500ms'
ssh mopi 'sigrok-cli -i /tmp/u.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx'
```

**In the web GUI → Live decode:** protocol **UART**, channel **0** (= GPIO2),
baud **115200**, rate **2M** → Start, *then* make sure traffic is flowing
(step 2). Note: in the live/stream view sigrok names channels `0..7`, so the
decoder uses `rx=0`; in a saved `.sr` it's `rx=GPIO2`.

⚠️ Live decode runs sigrok's Python decoder — keep the rate modest (≈16–20× baud,
so 2M for 115200). Too high → the pipe falls behind.

**Persist the UART across reboots** (optional; needs sudo + reboot — do it
yourself): add `dtoverlay=uart0` to `/boot/firmware/config.txt`, then reboot.
Without this, `sudo dtoverlay uart0` must be re-run after each boot.

**Real sniffing (external device):** wire the target's TX → GPIO2 and a shared
GND. ⚠️ 3.3 V only — anything 5 V needs a 74LVC245 buffer + series resistors, or
it kills the RP1.

---

## Decode a capture

```bash
# UART on ch0 (GPIO2) at 115200
sigrok-cli -i capture.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx

# Inspect a .sr
sigrok-cli -i capture.sr --show

# Live UART listener (copied next to the binary)
~/pi5la/build/uart-listen.sh --rate 2M --baud 115200        # (needs sigrok-cli)
```

## Elasticsearch export (optional)

```bash
export ES_USER ES_PASSWORD           # or ES_API_KEY  (never on argv)
~/pi5la/build/sr2es.py capture.sr --baud 115200                 # -> capture.sr.ndjson
~/pi5la/build/sr2es.py capture.sr --es-url https://es.example:9200
```

---

## Rebuild / re-sync

```bash
# after editing source on mopi
ssh mopi 'cmake --build ~/pi5la/build -j4 && (cd ~/pi5la/build && ctest)'

# push workstation edits (source of truth) to mopi
rsync -az --delete --exclude build/ --exclude build-host/ \
    /home/elansari/wiresploit/poc/pi5la/ mopi:~/pi5la/
ssh mopi 'cmake --build ~/pi5la/build -j4'

# from-scratch build deps on a fresh Pi: piolib (~/utils), pioasm (~/build-pioasm),
# then: cmake -S ~/pi5la -B ~/pi5la/build -DPIOASM_EXECUTABLE=$HOME/build-pioasm/pioasm
```
