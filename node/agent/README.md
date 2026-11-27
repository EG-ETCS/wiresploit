# pi5la: 8-channel PIO logic analyzer for Raspberry Pi 5

A command-line logic analyzer that captures GPIO 2–9 with the PIO block in the
Pi 5's RP1 southbridge. It writes sigrok `.sr` files that open in PulseView.

Target: 10 MSa/s on 8 channels, sustained, with **zero dropped samples**.

> **Status:** M0–M7 are verified on Pi 5 hardware, along with live listening,
> unbounded capture, `--silence-timeout`, `--monitor` and the Elasticsearch
> decode (2026-09-16 … 09-21). The spec target is met, and the reliable ceiling
> is 50 MB/s of packed data: 8 ch at 50 MSa/s, 4 ch at 100 MSa/s, 1–2 ch at
> 200 MSa/s. Still unverified: the `_bulk` upload against a live cluster, the
> PulseView visual check, and any external target through the 74LVC245 buffer.
> M8 is not implemented. Findings are collected in `REPORT.md`.

---

## ⚠️ Hardware safety: read before connecting anything

- **Pi 5 GPIO is 3.3 V and NOT 5 V tolerant.** Overvoltage on a GPIO pin damages
  **RP1**, the Pi 5 southbridge. RP1 is soldered to the board and cannot be
  replaced. Damaging it effectively destroys the Pi.
- **Before you connect to any external target**, you need a buffered front end:
  a **74LVC245** (or equivalent 5 V-tolerant-input buffer powered from 3.3 V)
  between the target and the Pi. Put **series resistors (e.g. 100–330 Ω)** on
  each channel, on the target side of the buffer. Share ground between the
  target and the Pi. The buffer protects RP1 from overvoltage, from contention,
  and from targets that are powered while the Pi is off.
- **The only safe bare-wire setup is loopback testing:** jumper wires between
  Pi 5 header pins (for example GPIO 20 → GPIO 2), with nothing external
  attached.
- GPIO 2 and GPIO 3 have on-board pull-ups to 3.3 V (I²C). An undriven channel
  0 or 1 reads high, not low.

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
pin 19 = /OE to GND. **Tie unused A inputs to GND**: a floating CMOS input
reads as noise, which looks like a busy channel and keeps `--silence-timeout`
and `--monitor` from ever seeing the line go quiet.

---

## Build (M0)

These are the commands used to build pi5la on the development Pi 5.

### 1. Prerequisites

```bash
sudo apt install git build-essential cmake pkg-config libzip-dev \
                 device-tree-compiler libncurses-dev libfdt-dev libgnutls28-dev
```

### 2. Driver and permissions

```bash
uname -r              # this project targets kernel 6.18+ (cyclic DMA)
ls -l /dev/pio0
```

- If `/dev/pio0` does not exist, the `rp1-pio` driver is not loaded. Update the
  Pi 5 firmware and kernel (`sudo apt full-upgrade`) and reboot.
- If it is `root root`, add the following to `/etc/udev/rules.d/99-com.rules`,
  following piolib's README:

  ```
  SUBSYSTEM=="*-pio", GROUP="gpio", MODE="0660"
  ```

  Then run `sudo udevadm control --reload-rules && sudo udevadm trigger` and
  make sure your user is in the `gpio` group. **Don't run pi5la under `sudo` to
  get around this.**

Measured on the development Pi 5 (Raspberry Pi OS Bookworm, kernel
`6.18.50+rpt-rpi-2712`):

```
crw-rw---- 1 root gpio 239, 0 Sep 15 03:18 /dev/pio0
```

It is group `gpio` with mode 0660 by default, and the user is in `gpio`, so no
root is needed. pi5la runs as a normal user.

### 3. piolib (raspberrypi/utils)

```bash
git clone https://github.com/raspberrypi/utils
cd utils
cmake -S . -B build
cmake --build build -j4
sudo cmake --install build        # libpio.a -> /usr/local/lib, headers -> /usr/local/include/piolib
cd ..
```

Stock example check (M0 acceptance): load a PIO program and run a state
machine.

```bash
./utils/build/piolib/examples/piopwm 4     # PWM on GPIO 4; expect no errors
```

### 4. pioasm (pico-sdk)

The spec says pioasm comes from raspberrypi/utils, but it lives in pico-sdk.
Build it on its own:

```bash
git clone --depth 1 https://github.com/raspberrypi/pico-sdk
cmake -S pico-sdk/tools/pioasm -B build-pioasm -DPIOASM_VERSION_STRING=2.2.0
cmake --build build-pioasm -j4
```

### 5. pi5la

```bash
cmake -S pi5la -B build-pi5la -DPIOASM_EXECUTABLE=$PWD/build-pioasm/pioasm
cmake --build build-pi5la -j4
(cd build-pi5la && ctest)
```

This builds `pi5la`, `ratesweep` and `bitprobe`. `.pio` files are assembled by
pioasm at build time.

To build and run only the unit tests on any Linux box, without piolib:
`cmake -S pi5la -B build-host -DPI5LA_TESTS_ONLY=ON`.

---

## Usage

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

Generator output is GPIO 20 = pin 38, the Pi's own UART TX is GPIO 14 = pin 8,
and GND is pins 6, 9, 14, 20, 25, 30, 34 and 39. Odd pins are the inner row,
even pins the outer row, and pin 1 is the square pad. The same table is in
`PROJECT_SUMMARY.md`; `--channels N` keeps channels 0..N-1 and drops the rest.

```
pi5la --rate 2M --out capture.sr                       # until Ctrl-C
pi5la --rate 2M --out capture.sr --silence-timeout 1s  # stop when it goes quiet
pi5la --rate 2M --monitor ~/captures/mon1              # bursts only, idle skipped
      [--gap 100ms] [--pre 10ms] [--post 50ms]
      [--channels 8] [--base-gpio 2] [--trigger rising:0]
      [--duration 60s] [--samples N] [--max-size 4G] [--dma-buf 1048576]
pi5la --rate 10M --samples 1000000 --raw words.bin [--mode naive]
pi5la --generator 1 --generator-only
pi5la --rate 2M --stream [--channels 8] [--trigger falling:0] | <decoder>
```

**The capture runs until you stop it** unless you give a limit. Samples are
written to disk as they arrive, so length is bounded by disk space, not RAM,
and Ctrl-C always leaves a readable `.sr`. Measured on the development Pi
(2026-09-21): 30 s at 2 MSa/s x 8 channels held memory flat at 6 MB.

The SD card writes 14.4 MB/s but stalls in bursts, so continuous recording is
comfortable to about 10 MSa/s on 8 channels. Above that, or for long sessions,
use `--monitor`.

| Option | Meaning |
|---|---|
| `--rate` | Sample rate with `k`/`M` suffix. Rate = 200 MHz / divider; the divider is an integer plus a 1/256 fraction. Fractional dividers add jitter, so pi5la prints a note when one is used. |
| `--out` | sigrok `.sr` output, written continuously. |
| `--monitor DIR` | Record only while the bus is active: one `.sr` per burst in `DIR`, plus `index.json`. Sampling never stops; idle is simply not written, so a quiet hour costs nothing. |
| `--silence-timeout` | Wait for the first level change on any captured channel, keeping `--pre` of lead-in, then stop once the line has been quiet this long. Every captured channel counts: tie unused buffer inputs to GND or they may never go quiet. |
| `--gap` | Silence that ends a burst, `--monitor` only (default 100 ms). |
| `--pre` | Lead-in kept before the first change (default 10 ms). Pulled from a small RAM ring, so the start of a transmission is never clipped. |
| `--post` | Tail kept after the last change (default 50 ms). |
| `--es-url URL` | Send each burst to Elasticsearch **as it closes**, instead of after the run. `--monitor` only. |
| `--protocol P` | What is on the bus, declared up front: `uart` (default) or `raw`. Recorded with every burst. |
| `--decode-channel N` | Which captured channel to decode (default 0; unused by `raw`). |
| `--baud N` | UART baud rate (default 115200). |
| `--duration` | Optional cap: stop after this much time (`500ms`, `10s`, `2m`). |
| `--samples` | Optional cap: stop after this many samples. Required with `--raw` or `--mode naive`, which capture into RAM. |
| `--max-size` | Stop once this much sample data is written (default 4G; suffix k/M/G). |
| `--raw` | The packed 32-bit FIFO words, as received. Used for M4 analysis; needs `--samples`/`--duration`. |
| `--channels` | 1, 2, 4 or 8. The PIO program is `in pins, N`, which packs 32/N samples per word. |
| `--base-gpio` | First capture GPIO. Pins must be consecutive (default 2). |
| `--trigger` | `rising`, `falling`, `high` or `low`, then `:N` with N relative to base. |
| `--mode` | `dma` (default) or `naive` (the M2 baseline). |
| `--generator` | Square wave on GPIO 20 during capture, for loopback self-test. |
| `--generator-only` | Run only the generator until Ctrl-C (M1). |
| `--dma-buf` | Cyclic DMA period size in bytes (the ring holds 4 periods). Writing to disk defaults to 4 MiB periods, `--stream` to 1 MiB. The card stalls in bursts, so the bigger ring is what keeps a recording from overflowing: at 2 MB/s, 1 MiB periods overflowed after 7 s while 4 MiB ran clean. |
| `--stream` | Capture until Ctrl-C and write samples to stdout as they arrive (1 byte per sample, channel N in bit N). Refuses to write to a terminal: pipe it into a decoder. |

Exit status: `0` OK, `1` error, `2` dropped samples detected. Stats and the
drop report go to stderr:

- **stall events**: in DMA mode, the number of DMA buffers after which the SM's
  sticky `RXSTALL` flag was set. `RXSTALL` means the FIFO filled and the SM
  paused, so samples are missing. In naive mode the flag is checked once at
  the end. Any stall event gives exit code 2.
- **estimated lost samples**: `rate × elapsed − samples received`, measured
  from the first buffer to the last. It's a wall-clock estimate that includes
  scheduling jitter, so it is reported but does **not** affect the exit code.

### Stopping by itself

`--silence-timeout` ends the capture once the line has been quiet for a while,
so a recording of one transmission needs no Ctrl-C:

```bash
pi5la --rate 2M --out capture.sr --silence-timeout 1s
```

It waits for the first change (the wait writes nothing but the `--pre`
lead-in), records, and stops after the line has been silent that long.

**Verified on the Pi (2026-09-21)**, UART jumper pin 8 -> pin 3, with the text
sent 3 s in: the capture ended itself at 4.194 s, exit 0, and sigrok decoded
`hello silence`. Of 8 388 608 samples captured, only 2 415 412 were written:
the waiting cost no disk.

*(This option was called `--idle-stop` until 2026-09-21. The old name still
works and prints a note.)*

It works with `--monitor` too, and then ends the **whole session** once the bus
has been quiet that long -- which is rarely what monitoring is for. Leave it off
to keep recording until you stop it. When it does fire, pi5la now says so
explicitly in monitor mode; before 2026-09-22 the session simply ended without a
word, which looked like a crash.

### Monitoring a bus: `--monitor`

For watching a bus over hours, recording idle would cost 1 byte per sample
whatever happens: 8 channels at 2 MSa/s is 7.2 GB/hour of nothing. Monitor mode
keeps sampling all the time but writes only the bursts:

```bash
pi5la --rate 2M --monitor ~/captures/mon1 --gap 100ms --pre 10ms --post 50ms
```

```
~/captures/mon1/
    index.json               rate, channels, and every burst so far
    burst-0001.sr
    burst-0001.sr.meta.json  when this burst arrived, and what it looked like
    burst-0002.sr
    burst-0002.sr.meta.json
```

Each burst gets its own sidecar, written once the `.sr` is final:

```json
{
  "samplerate_hz": 2000000, "channels": 8, "base_gpio": 2,
  "burst": "burst-0001.sr",
  "start_sample": 20018, "samples": 121552,
  "start_time_ns": 1789986306512345678,
  "first_edge_sample": 40018,
  "first_edge_time_ns": 1789986306522345678,
  "duration_s": 0.060776, "gap_before_s": 2.99,
  "protocol": "uart", "decode_channel": 0, "baud": 115200,
  "edges": 214, "active_channels": [0],
  "clock_synchronised": true, "clock_max_error_us": 1234,
  "clock_drift_ns": -41200, "clock_source": "CLOCK_REALTIME"
}
```

- **`first_edge_time_ns` is the arrival time**, stamped the moment the edge was
  seen rather than worked out later. `start_time_ns` is `--pre` earlier, since
  the file deliberately begins before the traffic does.
- **Times are integer nanoseconds.** A double holds only about 240 ns at today's
  epoch values, which is coarser than one sample at 50 MSa/s and coarser than a
  bit on any bus worth sniffing. The float `start_time` is still written for
  anything that already reads it.
- **The clock is `CLOCK_REALTIME`**, which is what NTP disciplines.
  `clock_synchronised` comes from `adjtimex(2)` and says whether anything was
  actually steering the clock; `clock_drift_ns` is how far the capture's own
  sample count had walked from it. pi5la warns at startup when the clock is
  free-running, but never refuses to capture.
- **`edges` and `active_channels` are protocol-blind**, counted from the samples
  themselves, so a burst is describable even when no decoder understands the bus.
- The sidecar is written *after* the `.sr` is finalised, so its presence means
  the burst is complete. That is what a shipper waits for, rather than reading
  `index.json` while it is being rewritten.
- **Numbering resumes** when a directory is reused: a run that finds
  `burst-0003.sr` starts at `burst-0004.sr` and says so. The directory doubles
  as the shipper's work queue, so a burst waiting to be sent, and the sidecar
  tombstone of one already sent, both stay behind -- restarting at 1 would
  overwrite an unshipped burst and give two different bursts the same name.
  `index.json` still describes only the run that wrote it; the per-burst
  sidecars are the record that spans runs.
- **How soon a burst appears** is set by the DMA period, not by the shipper: a
  burst cannot be seen until the period holding it fills, which is **2.1 s at
  2 MSa/s** with the 4 MiB default (`--dma-buf 262144` brings that to ~130 ms).
  Then `--gap` of silence has to pass, and `sigrok-cli` has to start. The large
  default is there because an SD card stalls for seconds under a *continuous*
  capture; monitor mode writes almost nothing, so a smaller period is much
  safer here -- but watch for `DMA ring overflow` and raise it again if one
  appears.

- **Nothing is missed.** The DMA capture never pauses; every period is scanned
  for a change, and the lead-in ring means a burst file starts *before* the
  first edge, not after it.
- **Timing stays honest.** Each file is continuous, so decoders and time
  measurements inside it are correct. The silence between bursts is recorded in
  `index.json` as `gap_before_s` rather than as gigabytes of unchanging bytes.
- A protocol that pauses for longer than `--gap` splits into two files, so
  raise `--gap` if one transaction is being cut in half.

**Verified on the Pi (2026-09-21).** Three UART lines were sent 3 s apart
during a 15 s monitor run at 2 MSa/s on 8 channels:

```
burst 1: 121552 samples (0.061 s)     gap_before_s 2.99
burst 2: 121548 samples (0.061 s)     gap_before_s 2.95
burst 3: 121548 samples (0.061 s)     gap_before_s 2.95
monitor: 3 burst(s), 1.09% of the time recorded
```

The directory came to **20 kB**, against about 33 MB for the same 15 s
recorded continuously, and `burst-0002.sr` decoded to `burst 2`.

Decode one burst exactly like any other capture:

```bash
sigrok-cli -i ~/captures/mon1/burst-0001.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx
```

### Live UART listening

`uart-listen` (copied next to `pi5la` in the build directory) captures
continuously with no gaps and decodes UART as bytes arrive, until Ctrl-C:

```bash
~/build-pi5la/uart-listen.sh                          # text, 115200 8N1, channel 0 (GPIO 2)
~/build-pi5la/uart-listen.sh --hex                    # "sample-number: hex" per byte
~/build-pi5la/uart-listen.sh --baud 9600 --channel 3 --rate 500k
```

It runs `pi5la --stream` into
`sigrok-cli -I binary:numchannels=8:samplerate=<Hz> -i - -P uart:rx=<channel>:baudrate=<baud>`,
so it needs `sigrok-cli` (`sudo apt install sigrok-cli`). The binary input
names channels `0`–`7`, not `GPIO2`–`GPIO9`.

**Verified on the Pi (2026-09-19)**, UART loopback jumper pin 8 → pin 3:

```bash
( sleep 3; printf 'hello live\r\n' > /dev/ttyAMA0 ) & timeout 8 ./uart-listen.sh
```

printed `hello live` as it arrived, streaming 15 728 640 samples in 7.865 s
with 0 stall events. The silence timeout was confirmed on the same wiring
(as `--idle-stop`, its name then):
`pi5la --rate 2M --duration 60s --silence-timeout 1s --out uart_idle.sr` ended by
itself 4.196 s in, exit 0, and sigrok decoded the line byte-exact.

sigrok's decoders run in Python and may not keep up with high sample rates.
The default is 2 MSa/s (about 17 samples per bit at 115200). If the pipe
can't keep up, the DMA ring overflows. pi5la then stops with
`DMA ring overflow … Lower --rate` and exit code 2, so data is never dropped
silently. RXSTALL events are reported live on stderr.

Other decoders work the same way, for example I²C on channels 0 and 1:

```bash
pi5la --rate 4M --stream | sigrok-cli -I binary:numchannels=8:samplerate=4000000 -i - \
    -P i2c:scl=0:sda=1 -A i2c
```

---

## Sending captures to Elasticsearch

`tools/sr2es.py` closes the pipeline: capture → decode → one document per
decoded message → NDJSON → `_bulk` into the `wiresploit-poc1` index.

```bash
sr2es.py capture.sr --baud 115200                      # writes capture.sr.ndjson
sr2es.py ~/captures/mon-test --channel 0               # a whole monitor session
sr2es.py capture.sr --es-url https://es.example:9200   # and upload it
```

### Live: shipping each burst as it ends

Waiting for a monitor session to be killed before anything reaches the database
is no good for a bus you are watching for hours. With `--es-url`, pi5la hands
each burst to `sr2es` the instant it closes:

```bash
pi5la --rate 2M --monitor ~/captures/mon1 \
      --es-url https://es.example:9200 --protocol uart --baud 115200
```

A message is in the index about `--gap` + `--post` + decode time after the line
falls quiet — roughly a second at the defaults — rather than whenever the run is
stopped.

- **Nothing slow runs in the capture path.** Decoding and uploading always
  happen in a separate process (`sr2es.py --ship DIR`), never inside the capture
  loop: a `sigrok-cli` run or a stalled POST there would overrun the four-period
  DMA ring and drop samples.
- **One shipper at a time.** A burst that closes while one is still working is
  simply left on disk; the next sweep finds it. There is no queue to lose — what
  is on disk *is* the work list.
- **A failed upload is retried.** The NDJSON is written before the upload, so a
  cluster that was down comes back to find its backlog waiting, and the next
  burst's shipper sends it. Nothing is re-decoded that was decoded already.
- **Confirmed data is deleted.** Once the cluster accepts a burst, its `.sr` and
  `.ndjson` go, and the sidecar is kept as the tombstone (`"shipped": true`).
  `--protocol raw` is the exception: its document carries no payload, so the
  `.sr` is the only copy of the waveform and stays.
- **Credentials are never on the command line.** `ES_API_KEY`, or `ES_USER` and
  `ES_PASSWORD`, are inherited from pi5la's environment.

`sr2es.py --ship DIR` can also be run by hand, on a live directory or a finished
one, and will pick up exactly whatever has not been shipped yet.

### Protocols

The bus is **declared before the capture runs** and recorded with every burst;
nothing guesses it from the data. `--protocol` picks the decoder:

| `--protocol` | What it does |
|---|---|
| `uart` (default) | Decodes with `sigrok-cli`, one document per message. |
| `raw` | No decoder at all: one document per burst, carrying when it arrived, how long it ran, how many edges it had and which channels moved. |

`raw` is what makes this useful on a bus nothing here understands yet — the
traffic is still timestamped and stored. Adding I²C or SPI means adding a
`PROTOCOLS` entry in `sr2es.py` (a decoder command line, its annotation classes,
how to group events, and that bus's own fields), not rewriting anything. Neither
is implemented yet: neither can be checked on the loopback rig.

A burst's sidecar records the protocol it was captured under, and that wins over
`--protocol` — the flag may just be a sweep's default, while whoever ran the
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

**`psf` is flat** — no `psd` level and no sub-objects. It carries a common core
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

> This shape **differs from the older documents in `wiresploit-poc1`**, which
> nest a flat `psd` under `psf`. The two now sit side by side in the index, and
> the `wiresploit-analyst` role cannot delete, so the old ones stay until an
> admin removes them.

- **Messages** are bytes grouped by idle gap: a new message starts when the
  line has been quiet for more than `--gap-bytes` byte-times (default 10, about
  0.9 ms at 115200). Framing and parity errors reported by sigrok are listed
  in `psf.errors`, and `psf.framing_error` is set.
- **`duration` is nanoseconds**, from the first bit to the last, and
  `timestamp` is `date_nanos`. Both are built from integers throughout: a double
  has only ~240 ns of resolution at today's epoch values, coarser than one
  sample at 50 MSa/s.
- **Absolute time** comes from the sidecar pi5la writes beside every capture
  (`<capture>.sr.meta.json`, or `burst-NNNN.sr.meta.json` in a monitor session),
  which carries `start_time_ns` and `first_edge_time_ns` as integers. Without a
  sidecar the tool falls back to the file's mtime and says so.
- **Decoding reuses `sigrok-cli`** (`-A uart=rx-data --protocol-decoder-samplenum`),
  the same decoder that produced byte-exact results in M6.
- **The NDJSON is always written first**, so a failed upload loses nothing:
  re-send it later with `curl --data-binary @file <url>/_bulk`.
- **Credentials never go on the command line.** Set `ES_API_KEY`, or `ES_USER`
  and `ES_PASSWORD`, in the environment:

  ```bash
  read -s ES_API_KEY; export ES_API_KEY      # not saved in shell history
  sr2es.py capture.sr --es-url https://es.example:9200
  ```

**Verified on the Pi (2026-09-21).** A three-burst monitor session decoded to
three documents, 36 bytes in total, with the older `psd` shape:

```
payload_hex 68656c6c6f20657320310d0a   psf.text "hello es 1
"
timestamps 2.008 s apart, psf.burst and gap_before_s carried through
```

**Live shipping and the flat `psf` shape have not been run on the Pi yet.** They
are covered by the host tests, and the whole chain — a burst closing, pi5la
spawning the shipper, the document reaching a stub `_bulk` endpoint, the files
being cleaned up, and an outage being retried — was exercised against a stub
cluster on a development machine.

---

## Web UI

`web/app.py` is a small Flask app that runs on the Pi and drives the same
binaries from a browser: set up a capture, watch its progress live, then
download or decode the `.sr` file. It never uses a shell — every argument is
validated and passed as an argument list by `web/args.py`.

> ⚠️ **No authentication.** Anyone who can reach the Pi on your network can
> start captures and download files. Keep it on a trusted LAN, or bind it to
> localhost (`--host 127.0.0.1`) and reach it through an SSH tunnel.

Install once (Bookworm's system Python is externally managed, so use a venv):

```bash
python3 -m venv ~/pi5la-ui-venv
~/pi5la-ui-venv/bin/pip install -r ~/pi5la/web/requirements.txt
```

Run it:

```bash
~/pi5la-ui-venv/bin/python ~/pi5la/web/app.py \
    --pi5la ~/build-pi5la/pi5la --captures ~/captures
```

To let the page ship bursts to Elasticsearch, export the credentials **before**
starting the server — pi5la's shipper inherits its environment, and the page
says so up front when they are missing:

```bash
export ES_USER ES_PASSWORD        # or ES_API_KEY
~/pi5la-ui-venv/bin/python ~/pi5la/web/app.py --pi5la ~/build-pi5la/pi5la
```

Then open `http://<pi-address>:9090`. Options: `--host`, `--port`,
`--sigrok`, `--captures` (default `~/captures`).

What the page does:

- **Capture** — continuous or monitor mode, rate, channels, base GPIO, silence
  timeout, optional caps, trigger and
  DMA buffer, with presets for UART, I²C and SPI. Start and Stop; Stop sends
  SIGINT, so pi5la still writes the partial file. pi5la's own output (stall
  events, throughput, `silence timeout: …`, bursts) streams into the page, and rates above
  the proven 50 MB/s show a warning.
- **Send to Elasticsearch** — in monitor mode, a switch turns on live shipping:
  each burst is decoded and uploaded as it ends, exactly as `--es-url` does on
  the command line. Pick `UART` for one document per decoded message, or `Raw`
  for one per burst with no decoder (and the `.sr` files kept, since a raw
  document carries no payload). The URL, index, decode channel, baud rate and
  line type are all here; **credentials are not**, and never travel through the
  page — see below.
- **Live decode** — `pi5la --stream` piped into `sigrok-cli`, the same pipeline
  as `uart-listen`, with the decoded bytes appended as they arrive.
- **Captures** — list, download, delete, and decode a saved file as UART, I²C
  or SPI. Bursts already shipped stay listed, marked with what they sent, even
  though their `.sr` has been deleted: the sidecar is what records that they
  existed.

**Credentials never go through the browser.** There is no password field: the
shipper reads `ES_API_KEY`, or `ES_USER` and `ES_PASSWORD`, from the environment
the web server was started with. A form field would put a password into the POST
body, the job log and the browser's history. When the server has no credentials,
the page shows a red marker in the header and explains how to restart it, rather
than letting every burst fail silently.

Only one job runs at a time, since a capture holds `/dev/pio0`; a second Start
is refused with a message. `tests/test_web_args.py` covers the argument
building — including that no credential can reach the command line — and runs as
part of `ctest`.

---

## Design notes

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
    with `pio_sm_xfer_data(..., NULL)`. On the development Pi, RXSTALL fired
    exactly once per buffer hand-over at 10 MSa/s: 245 stalls with 64 KiB
    buffers, 16 with 1 MiB, 4 with 4 MiB. Each stall lost a few dozen samples,
    because the 16-word FIFO covers only 6.4 µs.
- **Stall detection via FDEBUG.** piolib's `pio_sm_get_flags()` needs RP1
  firmware op `PIO_SM_GET_FLAGS` (op 33). The development Pi's RP1 firmware
  (`23fca1cd…`, `op count 33`) doesn't have it, and piolib panicked with
  `Failed to get FIFO flags`. A bootloader EEPROM update (2026/05/26) did not
  change the RP1 firmware. pi5la therefore reads the PIO FDEBUG register
  (`0xf0000008`, the address used by piolib's `rp1sm` example) through the
  `READ_HW` ioctl and clears it through `WRITE_HW`, on its own `/dev/pio0`
  handle. It uses the RP2040 bit layout: RXSTALL is bit `sm`, and writing 1
  clears it. `fdebugtest` confirmed this on the development Pi (2026-09-16):
  FDEBUG read `0x00000001` with SM 0 stalled and `0x00000000` after the clear.
- **Packing.** `sm_config_set_in_shift(shift_right = true, autopush, 32)` and
  `PIO_FIFO_JOIN_RX` (TX is unused, so the RX FIFO is twice as deep).
- **Runtime PIO programs.** Channel counts other than 8 and all trigger types
  are built at runtime (`src/progbuild.c`). On every capture the builder is
  checked against the pioasm output of `pio/la8.pio` and `pio/la8_trig.pio`.
- **Generator.** `sqwave.pio` (the spec program) covers ~1.53 kHz–100 MHz.
  Lower frequencies, such as the 1 Hz LED check, use `sqwave_slow.pio`, a delay
  loop at divider 1.
- **Prior art.** cleverca22's `libsigrok` `pi5-rp1-pio` driver uses a single
  4096-byte DMA buffer (`buf_count = 1`), one channel and `shift_right = true`.
  It doesn't solve continuous rotation. raspberrypi/utils issue #123 (large
  transfers timing out on 6.12) was fixed in the kernel. Issue #124 (DMA reads
  can't be terminated early) is still open.

### Capture limits

Figures come from the M5 rate sweep unless marked *unmeasured*.

**Throughput, not sampling, sets the ceiling.** PIO samples at up to
200 MSa/s (200 MHz clock, divider 1) whatever the channel count. The limit is
moving the data from RP1 to Linux: each channel adds one bit per sample, packed
32/N samples per word, through the FIFO, DMA ring, PCIe and pi5la's reads. All
measured ceilings land on the same bit rate:

| Channels | Ceiling | Bits/s | Words/s | Confidence |
|---|---|---|---|---|
| 8 | 50 MSa/s | 400 Mbit/s | 12.5 M | 200 M samples, 0 drops |
| 4 | 100 MSa/s | 400 Mbit/s | 12.5 M | 400 M samples, 0 drops |
| 2 | 200 MSa/s | 400 Mbit/s | 12.5 M | 800 M samples, 0 drops |
| 1 | 200 MSa/s | 200 Mbit/s | 6.25 M | 1600 M samples, 0 drops; PIO clock limit |

So fewer channels buy a higher rate: capture only the signals you need. Which
stage sets the ~400 Mbit/s limit (DMA refill, PCIe, or userspace copy) has not
been isolated; the drop counters only say that data was lost.

**Protocols.** Decoding wants at least 4 samples per bit or clock period;
10 is comfortable. So the usable clock is about rate/10 (safe) to rate/4.

| Protocol | Typical speed | Verdict |
|---|---|---|
| UART | up to ~3 Mbaud | yes; 115200 needs only 1–2 MSa/s |
| I²C | 100 k / 400 k / 1 M | yes, at 10 MSa/s |
| 1-Wire, WS2812 | ≤ 800 kHz | yes |
| CAN / CAN-FD (logic side) | 1 / 5 Mbit | yes |
| SWD / JTAG | usually ≤ 4 MHz | yes |
| SPI | clock-dependent | ~5 MHz at 8 ch; ~10 MHz at 4 ch; 10–25 MHz marginal |
| Flash SPI 40/80 MHz, SDIO, USB, Ethernet, HDMI, DDR | too fast or differential | no |

**Other limits.**
- *Live decoding (unmeasured).* `uart-listen` pipes into sigrok's Python
  decoders, which likely fall well behind 50 MSa/s; hence the 2M default.
  Capture to a file and decode afterwards for high rates.
- *Recording length (unmeasured).* One byte per sample is 50 MB/s at
  50 MSa/s; SD card write speed and free space cap the duration. Use a lower
  rate or the trigger for long sessions.
- *Electrical.* 3.3 V inputs only (5 V goes through the 74LVC245). Digital
  levels only; timing resolution is one sample (20 ns at 50 MSa/s). Loose
  jumpers distort signals above a few MHz. GPIO 2 and 3 (channels 0 and 1)
  have 1.8 kΩ on-board pull-ups, which can load a weak unbuffered signal.
- *Channels.* 8 by design.

### Why a Pi 5 and not a Pi 4

The Pi 4 has no RP1 and no PIO. Its GPIOs sit on the BCM2711 SoC, so sampling
would be a CPU loop reading a GPIO register. Linux interrupts and scheduling
pause that loop, so samples are unevenly spaced, gaps corrupt decoded bytes,
and drops are hard to detect. Expected rates are kHz to low MSa/s
(*unmeasured*); DMA-timed tools such as pigpio target kHz signals.

| | Pi 5 (RP1 PIO) | Pi 4 (software polling) |
|---|---|---|
| Sample timing | hardware clock, exact | loop timing, jittery |
| Dropped samples | detected; 0 at 50 MSa/s × 8 ch | likely, hard to detect |
| Practical rate | 50–200 MSa/s | kHz to low MSa/s (estimate) |

Beyond ~400 Mbit/s the next step is a dedicated FPGA + USB 3 analyzer
(DSLogic, Saleae), not another Pi.

---

## Milestones

Each one needs its acceptance test to pass on the Pi before the next one.

### M0 — Toolchain bring-up
Follow [Build](#build-m0). Accept when a stock piolib example runs without
error.
**Result:** PASS (2026-09-17). The stock example was run on GPIO 20 instead of
GPIO 4, so the existing loopback jumper (pin 38 → pin 3) could show the output
on GPIO 2:

```bash
timeout 14 ~/utils/build/piolib/examples/piopwm 20 & sleep 1; pinctrl get 20;   for i in $(seq 1 24); do pinctrl lev 2,20; sleep 0.5; done; wait %1; echo "exit=$?"
```

- It printed `Loaded program at 25, using sm 0, gpio 20`, with no errors.
- `pinctrl get 20` showed `a7 … // GPIO20 = PIO20`: the pin was handed to PIO.
- GPIO 2 matched GPIO 20 in all 24 readings. Readings were all low for the
  first ~6 s, then increasingly high as the PWM duty cycle ramped up.
- Exit status 124, from `timeout` stopping the endless example as intended.

### M1 — Test signal generator
```bash
pi5la --generator 1 --generator-only          # LED + resistor on GPIO 20: 1 Hz blink
pi5la --generator 100k --generator-only       # check with a scope / frequency meter
```
Verify without using the capture path.
**Result:** PASS (2026-09-16). `--generator 1` reported
`actual 1.000000 Hz (sqwave_slow)`, and an LED on GPIO 20 blinked with a
1 s period. The 100 kHz `sqwave` path has not been checked with a scope yet.

### M2 — Naive capture baseline
First confirm that stall detection works (no wiring needed). It must print
`PASS`:
```bash
fdebugtest
```
Then:
```bash
pi5la --mode naive --rate 10M --samples 400000 --raw naive.bin
```
Read the `throughput` line. Expect about 250 kB/s. RXSTALL will be set, which
is expected for this mode.
**Result:** PASS (2026-09-16): **396.3 kB/s** (396 295 samples/s). The state
machine stalled, as expected for this mode.

### M3 — DMA capture
```bash
pi5la --rate 10M --samples 16000000 --raw dma.bin --out dma.sr --generator 100k
```
Accept when there are no transfer errors (sample correctness is M4).
**Result:** PASS (2026-09-16, cyclic DMA, 4 × 1 MiB ring): 16 000 000 samples
in 1.679 s, **0 stall events**, exit 0, `.sr` written. The same capture with
`bitprobe --verify` showed exactly 320 000 transitions and 0 violations on
channel 0, so no samples were lost. The run printed 9.5 MB/s and "782549 lost
samples", but those numbers came from a reporting bug (the final period was
left out of the byte count) and were fixed afterwards. After the fix, a
10 000 000-sample capture during M6 measured **10.000 MB/s**, 0 stalls, with an
estimated loss of 178 samples (within wall-clock jitter).

The first attempt used piolib one-shot read-ahead (1 MiB buffers). It ran at
10.006 MB/s but set RXSTALL after all 16 buffers, and 27 run violations were
seen. See Design notes.

### Naive vs DMA

| Mode | Rate | Throughput | Stall events |
|---|---|---|---|
| naive (M2) | 10 MSa/s | 396.3 kB/s | stalled (checked once at end) |
| dma, piolib read-ahead | 10 MSa/s | 10 006 kB/s | 16 / 16 buffers |
| dma, cyclic (M3) | 10 MSa/s | 10 000 kB/s | 0 |

### M4 — Correctness and bit order
1. `bitprobe --map`: prompts you to move the GPIO 20 jumper through GPIO 2..9
   one at a time. For each channel it prints the word bits that toggle and
   their chronological order. At the end it prints a `map_8ch` table.
2. Paste that table into `src/unpack.c`. The table currently holds the
   *theoretical* layout (slot 0 in bits 0–7, channel k at bit k), which
   still needs to be checked on hardware. Update the matching check in
   `tests/test_unpack.c` too.
3. Acceptance: `bitprobe --verify 0 --generator 100k --rate 10M` should show
   channel 0 with every run exactly 50 samples and every other channel with no
   transitions.

Offline analysis of a `--raw` file: `bitprobe --raw-in dma.bin --channels 8`.
**Result:** PASS (2026-09-16).
- **Map:** `bitprobe --probe` was run with the generator jumpered to each of
  GPIO 2..9 in turn, checking with `pinctrl lev` before each capture. Channel
  k was active in word bits k, k+8, k+16, k+24, in that chronological order,
  for every k from 0 to 7. This is the layout already in `src/unpack.c` (slot 0
  = low byte, channel k = bit k), so no table change was needed.
- **Verify:** `bitprobe --verify 0 --generator 100k --rate 10M --samples 16M`
  gave 320 000 transitions with every run exactly 50 samples, channels 1–7
  constant, and 0 stalls.
- **Wiring tip:** check with
  `for i in 1 2 3 4 5 6; do pinctrl lev 2-9,20; sleep 0.5; done`, running
  `pi5la --generator 1 --generator-only`, before every probe. An empty probe
  ("0 active bits") meant the jumper wasn't connected to a capture pin.
  `bitprobe --map` is interactive, and a jumper that hasn't actually moved
  makes it report channel 0 for every step.

### M5 — Rate sweep
```bash
ratesweep --out ratesweep.csv            # jumper GPIO 20 -> GPIO 2
```
Columns: `channels,sample_rate,samples_captured,violations,violation_pct,stalls,status`.
`status` is `ok`, `violations` (any violation, stall or ring overflow) or
`nosignal` (no runs; `violation_pct` is then empty). The 2026-09-16 CSVs below
predate the `stalls` and `status` columns.
Only integer dividers are swept (200 MHz / {100,40,20,10,8,5,4,2,1}). The
generator is locked to the capture clock with a 25-sample half period.
**Result:** PASS (2026-09-16). **Measured ceiling at 8 channels: 50 MSa/s
sustained.** CSV: `ratesweep.csv` (full sweep) and `ratesweep-long.csv`
(8-channel confirmation), both generated on the Pi.

Sweep: cyclic DMA with a 4 × 1 MiB ring, 4 000 000 samples per point,
25-sample half period on channel 0.

| Channels | Highest clean rate | Packed data rate | First failing rate |
|---|---|---|---|
| 1 | 200 MSa/s (max swept) | 25 MB/s | — |
| 2 | 200 MSa/s (max swept) | 50 MB/s | — |
| 4 | 100 MSa/s | 50 MB/s | 200 MSa/s: 2 stalls, 55.9 % violations |
| 8 | 50 MSa/s | 50 MB/s | 100 MSa/s: 4 stalls, 80.7 % violations |

Every channel count is clean at 50 MB/s and fails at 100 MB/s, so the DMA
limit lies between the two. At 8 channels, 4 M samples (4 MB) is smaller than
the ring, so the short runs alone don't prove sustained throughput. A long
confirmation run followed:

`ratesweep --channels 8 --dividers 5,4,3 --samples 200M` (Pi 5 with 8 GB RAM,
7.2 GiB available):

| Rate | Samples | Duration | Violations | Stalls |
|---|---|---|---|---|
| 40 MSa/s | 200 M | 5 s | 0 | 0 |
| 50 MSa/s | 200 M | 4 s | **0** | **0** |
| 66.7 MSa/s | 200 M | 3 s | 1 | 1 |

Long confirmation of the other channel counts (2026-09-17), same jumper,
about 200 MB of packed data each. CSVs: `long-4ch.csv`, `long-2ch.csv`,
`long-1ch.csv` on the Pi.

| Command | Samples | Duration | Violations | Stalls | Status |
|---|---|---|---|---|---|
| `ratesweep --channels 4 --dividers 2 --samples 400M` (100 MSa/s) | 400 M | 4 s | **0** | **0** | ok |
| `ratesweep --channels 2 --dividers 1 --samples 800M` (200 MSa/s) | 800 M | 4 s | **0** | **0** | ok |
| `ratesweep --channels 1 --dividers 1 --samples 1600M` (200 MSa/s) | 1600 M | 8 s | **0** | **0** | ok |

Every run on channel 0 was exactly 25 samples (run min/max 25/25), so no
sample was lost.

#### Pushing past 50 MB/s (2026-09-17)

Same jumper. Can larger DMA periods (`--dma-buf`) lift the ceiling?

**DMA memory.** `CmaTotal: 65536 kB`, `CmaFree: 55296 kB`. The ring is
4 periods, so 4 × 16 MiB (64 MB) fails with `Cannot allocate memory`; the
largest ring that fits is 4 × 8 MiB.

**100 MB/s is a hard limit.** With 4 MiB periods, 8 ch at 100 MSa/s gave
48 stalls and 82.3 % violations, and 4 ch at 200 MSa/s gave 48 stalls and
55.8 % violations: a stall in nearly every period.

**66.7 MB/s is marginal at every period size.** 8 ch at 66.7 MSa/s,
200 M samples, repeated 5 times per period size:

| `--dma-buf` | Clean runs | Stalls per run |
|---|---|---|
| 1 MiB | 3 / 5 | 0, 3, 4, 0, 0 |
| 2 MiB | 0 / 5 | 2, 1, 2, 1, 1 |
| 4 MiB | 3 / 5 | 0, 1, 2, 0, 0 |
| 8 MiB | 3 / 5 | 0, 0, 3, 0, 1 |

No period size is reliable, and a single clean run proves nothing (the first
4 MiB run was clean by luck). Losses are rare but real: 1–4 stalls in 200 M
samples. One 4 MiB run had a stall with 0 violations (the lost samples didn't
break a run), which the stall counter still caught.

**Repeatability at 50 MB/s.** The same 5-run standard applied to the proven
ceilings, default 1 MiB periods:

| Channels | Rate | Samples per run | Clean runs | Stalls |
|---|---|---|---|---|
| 8 | 50 MSa/s | 200 M | **5 / 5** | 0 in every run |
| 4 | 100 MSa/s | 400 M | **5 / 5** | 0 in every run |
| 2 | 200 MSa/s | 800 M | **5 / 5** | 0 in every run |

Every run on channel 0 was exactly 25 samples.

**Conclusion:** 50 MB/s of packed data is reliable (15 / 15 clean runs,
3 GB of samples); 66.7 MB/s is not (9 / 20); 100 MB/s fails outright. The
edge lies between 50 and 66.7 MB/s; integer dividers can't test in between.

**Decision (2026-09-17):** 50 MB/s is the documented limit. Fractional-divider
tests between 50 and 66.7 MB/s were not pursued: they would gain at most ~30 %
with uneven sample spacing, close to the drop edge.



Note: a first sweep had the jumper on the wrong pin. Every row then read
`violations=0, violation_pct=100` (no runs found = no signal).

### M6 — sigrok output
Capture a real 115200-baud UART **through the buffer described above**, open
it in PulseView on the Pi, add the UART decoder, and compare the decoded bytes
with what was sent.
```bash
pi5la --rate 10M --samples 10000000 --out uart.sr
```
**Result (2026-09-16):** byte-exact decode, using the Pi 5's own UART in
loopback. No external target was connected, so no buffer was needed.
- **Setup:** `dtparam=uart0=on` in `/boot/firmware/config.txt` (plus a reboot)
  creates `/dev/ttyAMA0` on GPIO 14 (TXD0). A jumper runs from pin 8 (GPIO 14)
  to pin 3 (GPIO 2, channel 0). The port was configured with
  `stty -F /dev/ttyAMA0 115200 raw -echo`.
- **Data:** `sent.txt` has 200 lines of the form
  `pi5la M6 line NNN ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n` (8000 bytes). It was sent
  with `cat` 0.2 s after starting the capture.
- **Capture:** `pi5la --rate 10M --samples 10000000 --out uart.sr` recorded
  10 M samples at 10.000 MB/s, 0 stalls, exit 0. Channel 0 had 60 922
  transitions.
- **Decode:**
  `sigrok-cli -i uart.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx > decoded.txt`,
  then `cmp sent.txt decoded.txt` reported **MATCH**. sigrok read the metadata
  correctly (10 MHz, 8 channels GPIO2–GPIO9, 10 000 000 samples).
- **Still open:** the PulseView visual check on the Pi, and a test with an
  external target (ESP32) through the 74LVC245 front end.
- **Troubleshooting note:** after a reboot, GPIO 2 has no function and its input
  is disabled, so `pinctrl lev 2` prints `-` until pi5la has run once.

### M7 — Hardware trigger
```bash
pi5la --rate 10M --samples 1000000 --trigger rising:0 --out trig.sr
```
On an idle line this should block (Ctrl-C to abort) and start only when the
condition occurs. There is no pre-trigger buffer: the first captured sample is
the first one after the trigger.

**Result:** PASS (2026-09-16), loopback on GPIO 20 → GPIO 2.
- **Blocking and condition checks,** with the jumper removed so GPIO 2 sat high
  on its pull-up, each run limited to 5 s by `timeout`:
  - `low:0`, `falling:0` and `rising:0` blocked.
  - `high:0` fired immediately.
  - `rp1sm 0` showed the SM parked on `instr 00002020` (`wait 0 pin 0`).
- **Edge trigger:** with the jumper on and the line idle high (`pinctrl lev 2,20`
  = `1 1`), `--trigger rising:0 --samples 1000000` waited 30.1 s until
  `--generator 1k --generator-only` started, then completed with 0 stalls.
  The first sample was high, and the first change came at sample 4999: one
  full 5000-sample half period, so the rising edge falls just before sample 0.
- **Wiring caveat:** GPIO 20 is undriven when no generator is running. An
  earlier `rising:0` attempt fired straight away because the line wasn't at a
  known level when the trigger was armed. Check `pinctrl lev` before arming a
  loopback trigger test.

### M8 — RLE and pre-trigger buffer
Not implemented. It is planned only after M0–M7 pass.

---

## Repository layout

```
CMakeLists.txt
src/        main.c capture.c generator.c srzip.c unpack.c
            progbuild.c (runtime PIO programs) analyze.c (run-length checks) util.c
pio/        la8.pio la8_trig.pio sqwave.pio sqwave_slow.pio
tools/      ratesweep.c (M5) bitprobe.c (M4)
tests/      host unit tests (ctest)
```
