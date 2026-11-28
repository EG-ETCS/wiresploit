# Capture guide

How to build pi5la, install it on a Raspberry Pi 5, and use it: capture to a
file, listen live, record bursts, and send decoded messages to Elasticsearch.

pi5la is a command-line logic analyzer that captures GPIO 2–9 with the PIO
block in the Pi 5's RP1 chip. It writes sigrok `.sr` files that open in
PulseView. The spec target is 10 MSa/s on 8 channels, sustained, with zero
dropped samples. It is met; the measured limits are in the
[findings report](findings_report.md).

In this guide, `<pi-host>` is your Pi's hostname or IP address and
`<install_dir>` is the folder pi5la is installed in on the Pi.

---

## Hardware safety

!!! danger "Read before connecting anything"
    - **Pi 5 GPIO is 3.3 V and not 5 V tolerant.** Overvoltage on a GPIO pin
      damages **RP1**, the Pi 5's I/O chip. RP1 is soldered to the board and
      cannot be replaced, so damaging it destroys the Pi.
    - **Before you connect any external target**, you need a buffered front end:
      a **74LVC245** (or an equivalent buffer with 5 V-tolerant inputs, powered
      from 3.3 V) between the target and the Pi. Put **series resistors
      (100–330 Ω)** on each channel, on the target side of the buffer. **Share
      ground** between the target and the Pi. The buffer protects RP1 from
      overvoltage, from contention, and from targets that are powered while the
      Pi is off.
    - **The only safe bare-wire setup is loopback testing:** jumper wires
      between Pi 5 header pins (for example GPIO 20 → GPIO 2), with nothing
      external attached.
    - **Never connect pin 1 (3.3 V) or pins 2 and 4 (5 V) to a GPIO.**
    - GPIO 2 and GPIO 3 have on-board pull-ups to 3.3 V (for I²C). An undriven
      channel 0 or 1 reads high, not low.

Suggested front end per channel:

```
target ──[R 100–330 Ω]──► 74LVC245 An ──► Bn ──► Pi 5 GPIO (2..9)
                          VCC = 3.3 V (from Pi), DIR = A→B, /OE = GND
target GND ───────────────────────────────────── Pi 5 GND
```

One DIP-20 74LVC245 covers all 8 channels. Chip pin → Pi pin, in channel order:

| Channel | Buffer in (A) | Buffer out (B) | Pi pin |
|---|---|---|---|
| 0 | 2 | 18 | 3 |
| 1 | 3 | 17 | 5 |
| 2 | 4 | 16 | 7 |
| 3 | 5 | 15 | 29 |
| 4 | 6 | 14 | 31 |
| 5 | 7 | 13 | 26 |
| 6 | 8 | 12 | 24 |
| 7 | 9 | 11 | 21 |

Chip pin 20 = VCC (Pi pin 1, 3.3 V), pin 10 = GND, pin 1 = DIR to 3.3 V (A→B),
pin 19 = /OE to GND. **Tie unused A inputs to GND**: a floating CMOS input reads
as noise, which looks like a busy channel and keeps `--silence-timeout` and
`--monitor` from ever seeing the line go quiet.

---

## Build

Build and test on a laptop, in the dev container. The Pi only receives finished
binaries; it never builds. The scripts are in `node/scripts/` and all take
`--help`.

### 1. Get the dev container

The container (`node/docker/Dockerfile`) is Debian Bookworm with the arm64
cross toolchain, arm64 libzip, `pioasm` built from pico-sdk and `piolib`
cross-built from raspberrypi/utils (both pinned to fixed commits), plus Python,
sigrok-cli and the docs tools.

- **VS Code:** open the repo and choose "Reopen in Container". This uses
  `.devcontainer/node/devcontainer.json`.
- **Command line**, from the repo root:

  ```bash
  docker build -t wiresploit-node -f node/docker/Dockerfile .
  docker run --rm -it -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work wiresploit-node
  ```

- **No Docker:** run `node/scripts/setup_host.sh`. It checks for or installs the
  same packages (it asks before using `sudo`) and creates `.venv/`.
  `setup_host.sh --check` only reports what is missing. You can run the tests
  this way; building the Pi binary needs the container.

### 2. Run the tests

```bash
node/scripts/test.sh            # same as: test.sh all
node/scripts/test.sh unit       # C and Python unit tests
node/scripts/test.sh integration
```

The three test levels, and the rule that unit and integration tests pass before
a merge, are described in [Testing](../../02_Engineering/04_Testing.md).

### 3. Build the Pi binary

```bash
node/scripts/build_pi.sh
```

This cross-compiles `pi5la`, `ratesweep`, `bitprobe` and `fdebugtest` for
aarch64 into `node/capture/build-arm64/`, and checks with `file` that the result
is "ARM aarch64". piolib is linked statically, so the Pi only needs `libzip4`
at run time.

??? note "Background: building natively on a Pi"
    Before the container existed, pi5la was built on the Pi itself. This is no
    longer the supported path, but it shows what the container does for you.

    ```bash
    sudo apt install git build-essential cmake pkg-config libzip-dev \
                     device-tree-compiler libncurses-dev libfdt-dev libgnutls28-dev

    # piolib: libpio.a -> /usr/local/lib, headers -> /usr/local/include/piolib
    git clone https://github.com/raspberrypi/utils
    cmake -S utils -B utils/build
    cmake --build utils/build -j4
    sudo cmake --install utils/build

    # M0 check: a stock example loads a PIO program and runs a state machine
    ./utils/build/piolib/examples/piopwm 4

    # pioasm: the spec says it comes from raspberrypi/utils, but it lives in pico-sdk
    git clone --depth 1 https://github.com/raspberrypi/pico-sdk
    cmake -S pico-sdk/tools/pioasm -B build-pioasm -DPIOASM_VERSION_STRING=2.2.0
    cmake --build build-pioasm -j4

    # pi5la, from the repo root
    cmake -S node/capture -B node/capture/build -DPIOASM_EXECUTABLE=$PWD/build-pioasm/pioasm
    cmake --build node/capture/build -j4
    (cd node/capture/build && ctest)
    ```

    `.pio` files are assembled by pioasm at build time. To build and run only
    the unit tests on any Linux machine, without piolib, configure with
    `-DPI5LA_TESTS_ONLY=ON`.

---

## Install on a Pi

The Pi needs Raspberry Pi OS Bookworm (64-bit) and **kernel 6.18 or newer**
(cyclic DMA). Check with `uname -r`.

### 1. Copy the install folder

From the container or your laptop:

```bash
node/scripts/deploy_to_pi.sh
```

On the first run it asks, in dialog boxes, for the Pi's address (`<pi-host>`),
the ssh user and the install folder on the Pi (`<install_dir>`). It saves the
answers in the gitignored `node/.deploy.conf` and offers to reuse them next
time. It then copies the install folder with `rsync` over ssh.
`deploy_to_pi.sh --dry-run` prints the rsync command without running it.

The install folder on the Pi:

```
<install_dir>/
    bin/        pi5la ratesweep bitprobe fdebugtest sr2es uart-listen test_pi.sh
    web_ui/     capture test page
    lib/        node_config.py
    scripts/    setup_pi.sh lib/
    node.conf.example  node.conf  captures/
```

Every program finds `node.conf` and its other files from its own location, so
the folder can live anywhere and can be moved.

### 2. Set up the Pi

On the Pi:

```bash
<install_dir>/scripts/setup_pi.sh
```

It does not build anything. In order, it:

1. Lists the packages the Pi needs (`libzip4`, `sigrok-cli`, `python3`,
   `python3-flask`, `whiptail`) and asks before running `sudo apt`.
2. Checks that `/dev/pio0` exists and that your user is in the `gpio` group,
   and offers to add you if not.
3. Asks for each setting in a dialog, pre-filled with the current value: Node
   name, test page address and port, Elasticsearch URL and API key, Core
   address (optional). It shows a summary, then writes `node.conf` with mode
   600.
4. Optionally enables the UART on GPIO 14 for the loopback test. This edits
   `config.txt` and needs a reboot; it asks first.
5. Prints the hardware-safety warning.

`setup_pi.sh --check` only reports what is missing. `setup_pi.sh --config <file>`
takes the settings from a file instead of the dialogs. Running it again is safe
and keeps the existing settings.

### Settings

All per-machine settings live in `<install_dir>/node.conf`, plain `KEY=value`
lines described in `node.conf.example`. A setting is taken from, highest
first: the command-line flag, an environment variable with the same name,
`node.conf`, then the built-in default.

### Driver and permissions

pi5la runs as a normal user. **Don't run it under `sudo`.**

```bash
ls -l /dev/pio0
# expected: crw-rw---- 1 root gpio 239, 0 … /dev/pio0
```

- If `/dev/pio0` does not exist, the `rp1-pio` driver is not loaded. Update the
  Pi's firmware and kernel (`sudo apt full-upgrade`) and reboot.
- If it is owned by `root root`, add this line to
  `/etc/udev/rules.d/99-com.rules`, following piolib's README:

  ```
  SUBSYSTEM=="*-pio", GROUP="gpio", MODE="0660"
  ```

  Then run `sudo udevadm control --reload-rules && sudo udevadm trigger`, and
  make sure your user is in the `gpio` group.

On Raspberry Pi OS Bookworm with kernel 6.18 the device is group `gpio` with
mode 0660 by default.

---

## Usage

The commands below assume `<install_dir>/bin` is on your `PATH`. Otherwise
write the full path, for example `<install_dir>/bin/pi5la`.

### Channels

Channel k is GPIO `base + k`, so with the default base of GPIO 2:

| Channel | GPIO | Physical pin | Name in `.sr` | Name in `--stream` |
|---|---|---|---|---|
| 0 | 2 | 3 | GPIO2 | 0 |
| 1 | 3 | 5 | GPIO3 | 1 |
| 2 | 4 | 7 | GPIO4 | 2 |
| 3 | 5 | 29 | GPIO5 | 3 |
| 4 | 6 | 31 | GPIO6 | 4 |
| 5 | 7 | 26 | GPIO7 | 5 |
| 6 | 8 | 24 | GPIO8 | 6 |
| 7 | 9 | 21 | GPIO9 | 7 |

The generator output is GPIO 20 = pin 38, the Pi's own UART TX is GPIO 14 =
pin 8, and GND is pins 6, 9, 14, 20, 25, 30, 34 and 39. Odd pins are the inner
row, even pins the outer row, and pin 1 is the square pad. `--channels N` keeps
channels 0 to N−1 and drops the rest.

### Commands

```
pi5la --rate 2M --out capture.sr                       # until Ctrl-C
pi5la --rate 2M --out capture.sr --silence-timeout 1s  # stop when it goes quiet
pi5la --rate 2M --monitor <install_dir>/captures/mon1  # bursts only, idle skipped
      [--gap 100ms] [--pre 10ms] [--post 50ms]
      [--channels 8] [--base-gpio 2] [--trigger rising:0]
      [--duration 60s] [--samples N] [--max-size 4G] [--dma-buf 1048576]
pi5la --rate 10M --samples 1000000 --raw words.bin [--mode naive]
pi5la --generator 1 --generator-only
pi5la --rate 2M --stream [--channels 8] [--trigger falling:0] | <decoder>
```

**The capture runs until you stop it** unless you give a limit. Samples are
written to disk as they arrive, so length is bounded by disk space, not RAM,
and Ctrl-C always leaves a readable `.sr`.

An SD card stalls in bursts, so continuous recording is comfortable up to about
10 MSa/s on 8 channels (see [findings, other limits](findings_report.md#other-limits)).
Above that, or for long sessions, use `--monitor`.

| Option | Meaning |
|---|---|
| `--rate` | Sample rate with a `k` or `M` suffix. Rate = 200 MHz / divider; the divider is an integer plus a 1/256 fraction. Fractional dividers add jitter, so pi5la prints a note when one is used. |
| `--out` | sigrok `.sr` output, written continuously. |
| `--monitor DIR` | Record only while the bus is active: one `.sr` per burst in `DIR`, plus `index.json`. See [monitor mode](monitor_mode.md). |
| `--silence-timeout` | Wait for the first level change on any captured channel, keeping `--pre` of lead-in, then stop once the line has been quiet this long. Every captured channel counts: tie unused buffer inputs to GND, or they may never go quiet. |
| `--gap`, `--pre`, `--post` | Burst end, lead-in and tail. See [monitor mode, flags](monitor_mode.md#3-flags). `--pre` also sets the lead-in for `--silence-timeout`. |
| `--es-url URL` and related flags | Send each burst to Elasticsearch as it closes. `--monitor` only. See [monitor mode, live shipping](monitor_mode.md#9-live-shipping-to-elasticsearch). |
| `--protocol P` | What is on the bus, declared up front: `uart` (default) or `raw`. Recorded with every burst. |
| `--decode-channel N` | Which captured channel to decode (default 0; unused by `raw`). |
| `--baud N` | UART baud rate (default 115200). |
| `--duration` | Optional cap: stop after this much time (`500ms`, `10s`, `2m`). |
| `--samples` | Optional cap: stop after this many samples. Required with `--raw` or `--mode naive`, which capture into RAM. |
| `--max-size` | Stop once this much sample data is written (default 4G; suffix k, M or G). |
| `--raw` | The packed 32-bit FIFO words, as received. Used for M4 analysis; needs `--samples` or `--duration`. |
| `--channels` | 1, 2, 4 or 8. The PIO program is `in pins, N`, which packs 32/N samples per word. |
| `--base-gpio` | First capture GPIO. Pins must be consecutive (default 2). |
| `--trigger` | `rising`, `falling`, `high` or `low`, then `:N` with N relative to the base. There is no pre-trigger buffer: the first sample is the first one after the trigger. |
| `--mode` | `dma` (default) or `naive` (the M2 baseline). |
| `--generator` | Square wave on GPIO 20 during capture, for a loopback self-test. |
| `--generator-only` | Run only the generator until Ctrl-C (M1). |
| `--dma-buf` | Cyclic DMA period size in bytes; the ring holds 4 periods. Writing to disk defaults to 4 MiB periods, `--stream` to 1 MiB. The card stalls in bursts, and the bigger ring is what keeps a recording from overflowing. |
| `--stream` | Capture until Ctrl-C and write samples to stdout as they arrive (1 byte per sample, channel N in bit N). Refuses to write to a terminal: pipe it into a decoder. |

**Exit status:** `0` OK, `1` error, `2` dropped samples detected. Stats and the
drop report go to stderr:

- **stall events**: in DMA mode, the number of DMA periods after which the
  state machine's sticky `RXSTALL` flag was set. `RXSTALL` means the FIFO filled
  and the state machine paused, so samples are missing. In naive mode the flag
  is checked once at the end. Any stall event gives exit code 2.
- **estimated lost samples**: `rate × elapsed − samples received`, measured
  from the first period to the last. It is a wall-clock estimate that includes
  scheduling jitter, so it is reported but does **not** affect the exit code.

### Choosing a sample rate

A decoder needs at least about 4 samples per bit; 10 or more is comfortable.

| Signal | Suggested rate |
|---|---|
| UART 115200 | 2M |
| UART 1 Mbaud | 10M or more |
| I²C 400 kHz | 4M–10M |
| SPI 5 MHz | 50M |

Fewer channels allow a higher rate. Which protocols fit at all is in the
[findings report, protocols](findings_report.md#4-what-it-means-for-protocols).

### Stopping by itself

`--silence-timeout` ends the capture once the line has been quiet for a while,
so a recording of one transmission needs no Ctrl-C:

```bash
pi5la --rate 2M --out capture.sr --silence-timeout 1s
```

It waits for the first change (the wait writes nothing but the `--pre`
lead-in), records, and stops after the line has been silent that long.

This option was called `--idle-stop` until 2026-09-21. The old name still works
and prints a note.

It works with `--monitor` too, and then ends the **whole session** once the bus
has been quiet that long, which is rarely what monitoring is for. pi5la says so
explicitly when it fires in monitor mode.

### Monitoring a bus

For watching a bus over hours, `--monitor DIR` keeps sampling all the time but
writes only the bursts of traffic, each as its own `.sr` with a sidecar that
records when it arrived:

```bash
pi5la --rate 2M --monitor <install_dir>/captures/mon1 --gap 100ms --pre 10ms --post 50ms
```

Everything about it (the burst state machine, flags, the output folder, the
sidecar fields, timestamps, latency and tuning) is in
[monitor mode](monitor_mode.md).

### Live UART listening

`uart-listen` captures continuously with no gaps and decodes UART as bytes
arrive, until Ctrl-C. It is copied next to `pi5la`.

```bash
uart-listen                                   # text, 115200 8N1, channel 0 (GPIO 2)
uart-listen --hex                             # "sample-number: hex" per byte
uart-listen --baud 9600 --channel 3 --rate 500k
```

It runs `pi5la --stream` into
`sigrok-cli -I binary:numchannels=8:samplerate=<Hz> -i - -P uart:rx=<channel>:baudrate=<baud>`,
so it needs `sigrok-cli`. That input names channels `0`–`7`, not
`GPIO2`–`GPIO9`.

sigrok's decoders run in Python and may not keep up with high sample rates. The
default is 2 MSa/s (about 17 samples per bit at 115200). If the pipe can't keep
up, the DMA ring overflows; pi5la then stops with
`DMA ring overflow … Lower --rate` and exit code 2, so data is never dropped
silently. RXSTALL events are reported live on stderr.

Other decoders work the same way, for example I²C on channels 0 and 1:

```bash
pi5la --rate 4M --stream | sigrok-cli -I binary:numchannels=8:samplerate=4000000 -i - \
    -P i2c:scl=0:sda=1 -A i2c
```

### Decoding a saved capture

```bash
sigrok-cli -i capture.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx
sigrok-cli -i capture.sr --show       # inspect the metadata
pulseview capture.sr
```

---

## Sending captures to Elasticsearch

`sr2es` (`node/exporter/sr2es.py`) closes the pipeline: capture → decode → one
document per decoded message → NDJSON → `_bulk` into the `wiresploit-poc1`
index.

```bash
sr2es capture.sr --baud 115200                       # writes capture.sr.ndjson
sr2es <install_dir>/captures/mon1 --channel 0        # a whole monitor session
sr2es capture.sr --es-url https://es.example:9200    # and upload it
```

`--es-url` and `--sigrok` default to `ES_URL` and `SIGROK_CLI` in `node.conf`.

**Live shipping:** with `pi5la --monitor DIR --es-url URL`, each burst is
decoded and uploaded as it closes, by a separate `sr2es --ship DIR` process.
How that works, and why nothing slow runs in the capture path, is in
[monitor mode, live shipping](monitor_mode.md#9-live-shipping-to-elasticsearch).
`sr2es --ship DIR` can also be run by hand, on a live folder or a finished one,
and picks up whatever has not been shipped yet.

**Credentials never go on the command line.** Set `ES_API_KEY`, or `ES_USER`
and `ES_PASSWORD`, in the environment:

```bash
read -s ES_API_KEY; export ES_API_KEY      # not saved in shell history
sr2es capture.sr --es-url https://es.example:9200
```

### Protocols

The bus is **declared before the capture runs** and recorded with every burst;
nothing guesses it from the data. `--protocol` picks the decoder:

| `--protocol` | What it does |
|---|---|
| `uart` (default) | Decodes with `sigrok-cli`, one document per message. |
| `raw` | No decoder at all: one document per burst, carrying when it arrived, how long it ran, how many edges it had and which channels moved. |

`raw` is what makes this useful on a bus nothing here understands yet: the
traffic is still timestamped and stored. Adding I²C or SPI means adding a
`PROTOCOLS` entry in `sr2es.py` (a decoder command line, its annotation classes,
how to group events, and that bus's own fields), not rewriting anything. Neither
is implemented yet, because neither can be checked on the loopback rig.

A burst's sidecar records the protocol it was captured under, and that wins over
`--protocol`: the flag may just be a sweep's default, while whoever ran the
capture knew what was on the wire.

### The document

```json
{
  "timestamp": "2026-09-21T11:25:25.278009000Z",
  "event_type": "onboard",
  "protocol": "UART",
  "duration": 1004500,
  "length_bytes": 12,
  "payload_hex": "68656c6c6f20657320310d0a",
  "psf": {
    "channel": 0, "gpio": 2, "port": "ch0",
    "text": "hello es 1\\r\\n", "byte_count": 12, "error": false,
    "file": "burst-0001.sr", "burst": "burst-0001.sr",
    "start_sample": 20018, "end_sample": 22027,
    "samplerate_hz": 2000000, "gap_before_s": 1.986508,
    "first_edge_time_ns": 1789986306522345678,
    "clock_synchronised": true, "clock_max_error_us": 1234,
    "clock_drift_ns": -41200,
    "uart_type": "TTL-3V3", "baud_rate": 115200,
    "data_bits": 8, "parity": "none", "stop_bits": 1, "framing_error": false
  }
}
```

**`psf` is flat**: no `psd` level and no sub-objects. It carries a common core,
present whatever the protocol, and the declared protocol's own fields beside it:

- **Core:** `channel`, `gpio`, `port`, `file`, `burst`, `byte_count`, `text`,
  `error`, `start_sample`, `end_sample`, `samplerate_hz`, `gap_before_s`,
  `first_edge_time_ns`, `clock_synchronised`, `clock_max_error_us`,
  `clock_drift_ns`; plus `errors`, `payload_truncated` and `time_source` when
  they apply.
- **UART adds:** `uart_type`, `baud_rate`, `data_bits`, `parity`, `stop_bits`,
  `framing_error`.
- **RAW adds:** `edges`, `active_channels`.

Because `psf` is flat, a protocol field that shadowed a core one would silently
replace it, so `sr2es.py` asserts at import that none can.

!!! note "Older documents"
    This shape differs from the older documents in `wiresploit-poc1`, which nest
    a flat `psd` under `psf`. The two sit side by side in the index. The
    `wiresploit-analyst` role cannot delete, so the old ones stay until an admin
    removes them.

- **Messages** are bytes grouped by idle gap: a new message starts when the line
  has been quiet for more than `--gap-bytes` byte-times (default 10, about
  0.9 ms at 115200). Framing and parity errors reported by sigrok are listed in
  `psf.errors`, and `psf.framing_error` is set.
- **`duration` is nanoseconds**, from the first bit to the last, and `timestamp`
  is `date_nanos`. Both are built from integers throughout: a double has only
  about 240 ns of resolution at today's epoch values, coarser than one sample at
  50 MSa/s.
- **Absolute time** comes from the sidecar pi5la writes beside every capture
  (`<capture>.sr.meta.json`, or `burst-NNNN.sr.meta.json` in a monitor session),
  which carries `start_time_ns` and `first_edge_time_ns` as integers. Without a
  sidecar the tool falls back to the file's modification time and says so.
- **Decoding reuses `sigrok-cli`** (`-A uart=rx-data --protocol-decoder-samplenum`),
  the same decoder that produced byte-exact results in M6.
- **The NDJSON is always written first**, so a failed upload loses nothing:
  re-send it later with `curl --data-binary @file <url>/_bulk`.

---

## Capture test page

`web_ui/app.py` is a small Flask page that runs on the Pi and drives the same
binaries from a browser.

!!! warning "A local testing tool only"
    This page is for trying captures by hand on a Pi. It is **not** the
    Wiresploit product UI: real capture commands will come from Core. Don't
    build product features on it.

!!! danger "No authentication"
    Anyone who can reach the Pi on your network can start captures and download
    files. Bind it to `127.0.0.1` and reach it through an SSH tunnel, or keep it
    on a trusted bench network only.

It never uses a shell: every argument is validated and passed as an argument
list by `web_ui/args.py`. Flask comes from the `python3-flask` package that
`setup_pi.sh` installs.

```bash
python3 <install_dir>/web_ui/app.py                     # settings from node.conf
python3 <install_dir>/web_ui/app.py --host 127.0.0.1 --port 9090
```

Then open `http://<pi-host>:9090`, or through a tunnel:
`ssh -N -L 9090:127.0.0.1:9090 <pi-host>` and `http://localhost:9090`.
Options: `--host`, `--port`, `--pi5la`, `--sigrok`, `--captures`. Their defaults
come from `node.conf` (`WEB_UI_HOST`, `WEB_UI_PORT`, `PI5LA_BIN`, `SIGROK_CLI`,
`CAPTURES_DIR`).

To let the page ship bursts to Elasticsearch, export the credentials **before**
starting it. pi5la's shipper inherits the page's environment, and the page says
so up front when they are missing:

```bash
export ES_USER ES_PASSWORD        # or ES_API_KEY
python3 <install_dir>/web_ui/app.py
```

What the page does:

- **Capture**: continuous or monitor mode, rate, channels, base GPIO, silence
  timeout, optional caps, trigger and DMA buffer, with presets for UART, I²C and
  SPI. Start and Stop; Stop sends SIGINT, so pi5la still writes the partial
  file. pi5la's own output (stall events, throughput, `silence timeout: …`,
  bursts) streams into the page, and rates above the proven 50 MB/s show a
  warning.
- **Send to Elasticsearch**: in monitor mode, a switch turns on live shipping,
  exactly as `--es-url` does on the command line. Pick `UART` for one document
  per decoded message, or `Raw` for one per burst with no decoder (the `.sr`
  files are then kept, since a raw document carries no payload). The URL, index,
  decode channel, baud rate and line type are set here; credentials are not.
- **Live decode**: `pi5la --stream` piped into `sigrok-cli`, the same pipeline
  as `uart-listen`, with the decoded bytes appended as they arrive.
- **Captures**: list, download, delete, and decode a saved file as UART, I²C or
  SPI. Bursts already shipped stay listed, marked with what they sent, even
  though their `.sr` has been deleted: the sidecar records that they existed.

**Credentials never go through the browser.** There is no password field: the
shipper reads `ES_API_KEY`, or `ES_USER` and `ES_PASSWORD`, from the environment
the page was started with. A form field would put a password into the POST body,
the job log and the browser's history. When there are no credentials, the page
shows a red marker in the header and explains how to restart it, rather than
letting every burst fail silently.

Only one job runs at a time, since a capture holds `/dev/pio0`; a second Start
is refused with a message. `web_ui/tests/test_args.py` covers the argument
building, including that no credential can reach the command line.

---

## Design notes

Why the capture path looks the way it does. The rules that follow from these
notes are listed as critical constraints in `node/AGENTS.md`; the decisions and
their dates are in [decisions](decisions.md).

- **No word-by-word polling.** piolib calls are RPCs to RP1 firmware over PCIe.
  Bulk capture uses DMA only. `--mode naive` exists only as the M2 baseline.
- **Cyclic DMA (kernel 6.18+).** Capture uses the kernel's cyclic DMA mode:
  `PIO_IOC_SM_CONFIG_XFER_V2` with `RP1_PIO_SM_CONFIG_XFER_FL_DMA_CYCLE` on a
  ring of 4 periods (`--dma-buf` sets the period size), then one
  `PIO_IOC_SM_XFER_DATA32` read per completed period (`src/dmacyclic.c`). The
  DMA controller (`dw-axi-dmac`) links the last period back to the first in
  hardware, so there is no hand-over gap. If pi5la falls more than 4 periods
  behind, the read fails with `-EOVERFLOW`, and that counts as dropped samples.
  piolib has no API for cyclic mode, so these ioctls go on pi5la's own
  `/dev/pio0` handle.
    - *Why not piolib read-ahead:* the first version queued one-shot receives
      with `pio_sm_xfer_data(..., NULL)`. RXSTALL fired exactly once per buffer
      hand-over, and each stall lost a few dozen samples, because the 16-word
      FIFO covers only 6.4 µs at 10 MSa/s. The stall counts are in the
      [findings report](findings_report.md#31-problems-found-and-solved).
- **Stall detection via FDEBUG.** piolib's `pio_sm_get_flags()` needs RP1
  firmware op `PIO_SM_GET_FLAGS` (op 33). The development Pi's RP1 firmware
  (`23fca1cd…`, `op count 33`) doesn't have it, and piolib panicked with
  `Failed to get FIFO flags`. A bootloader EEPROM update (2026/05/26) did not
  change the RP1 firmware. pi5la therefore reads the PIO FDEBUG register
  (`0xf0000008`, the address used by piolib's `rp1sm` example) through the
  `READ_HW` ioctl and clears it through `WRITE_HW`, on its own `/dev/pio0`
  handle. It uses the RP2040 bit layout: RXSTALL is bit `sm`, and writing 1
  clears it. `fdebugtest` confirmed this on hardware.
- **Packing.** `sm_config_set_in_shift(shift_right = true, autopush, 32)` and
  `PIO_FIFO_JOIN_RX` (TX is unused, so the RX FIFO is twice as deep).
- **Runtime PIO programs.** Channel counts other than 8 and all trigger types
  are built at run time (`src/progbuild.c`). On every capture the builder is
  checked against the pioasm output of `pio/la8.pio` and `pio/la8_trig.pio`.
- **Generator.** `sqwave.pio` (the spec program) covers about 1.53 kHz to
  100 MHz. Lower frequencies, such as the 1 Hz LED check, use `sqwave_slow.pio`,
  a delay loop at divider 1.
- **Prior art.** cleverca22's `libsigrok` `pi5-rp1-pio` driver uses a single
  4096-byte DMA buffer (`buf_count = 1`), one channel and `shift_right = true`.
  It doesn't solve continuous rotation. raspberrypi/utils issue #123 (large
  transfers timing out on 6.12) was fixed in the kernel. Issue #124 (DMA reads
  can't be terminated early) is still open.

Throughput ceilings, protocol fit, recording-length limits, electrical limits
and the Pi 4 comparison are in the [findings report](findings_report.md).
