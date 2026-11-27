# Handoff: pi5la (Raspberry Pi 5 PIO logic analyzer)

Paste this into a new Claude chat, or attach it, to continue the project.

---

## How I want you to work

- **Ask me when anything is unclear. Don't make design decisions on your own.**
  Use multiple-choice questions for real choices, and give a recommendation.
- **You can't reach the Pi.** You write and compile code on my Windows PC. I copy it to
  the Pi, run the commands you give me, and paste the output back.
- Give short, exact commands. Tell me which terminal and which physical pin, and what
  output to expect.
- **Before any loopback test, have me confirm the wiring** with `pinctrl`. Wrong
  jumper pins caused most of our past confusion.
- **Hardware safety:** any external device must go through a 74LVC245 buffer. Never let
  me connect pins 1, 2 or 4 (3.3 V / 5 V) to a GPIO.

## Environment

- **PC:** Windows 11. Repo at `D:\CSE\wiresploit\pi5la` (git, local only, no remote).
  - The spec is at `SPECS.md`, at the repo root.
  - Commit messages end with a `Co-Authored-By: Claude` line.
- **Compile-checking on the PC:** WSL Ubuntu 22.04 has a test tree in `~/pi5la-check`,
  with pioasm, a local piolib install, and libzip unpacked locally (no sudo). A rebuild
  script copies the repo in, runs cmake with
  `-DCMAKE_PREFIX_PATH=~/pi5la-check/prefix -DPIOASM_EXECUTABLE=~/pi5la-check/build-pioasm/pioasm`
  and `PKG_CONFIG_PATH` pointing at `~/pi5la-check/zroot`, then builds and runs `ctest`.
- **C: drive is nearly full** (~0.5 GB free). WSL sometimes fails with out-of-memory or
  "not enough space". If so, say so; don't retry in a loop.
- **Pi 5:** 8 GB RAM, Raspberry Pi OS Bookworm, kernel `6.18.50+rpt-rpi-2712`.
  - User `ansari`, IP `192.168.111.145`.
  - `/dev/pio0` is `root:gpio 0660`, and the user is in `gpio`, so no sudo is needed.
  - RP1 firmware `23fca1cd…` reports **op count 33**, so there is no `PIO_SM_GET_FLAGS`.
  - `dtparam=uart0=on` is set, giving `/dev/ttyAMA0` on GPIO 14.
  - Build dirs on the Pi: `~/utils/build`, `~/build-pioasm`, `~/pi5la`, `~/build-pi5la`.
- **Copying code to the Pi:** read-only `.git` objects make `scp` over an existing copy
  fail. Have me run `mv ~/pi5la ~/pi5la-oldN` on the Pi first, then
  `scp -r D:\CSE\wiresploit\pi5la ansari@192.168.111.145:~/`, then rebuild.
  If only source files changed, copying single files works too.

## Current state

The latest commits add unbounded capture, `--silence-timeout` and `--monitor`, plus the
web UI. See `REPORT.md` for the findings,
`PROJECT_SUMMARY.md` for the overview, and `README.md` for every measurement.

**Verified on the Pi:**
- **M1:** generator.
- **M2:** naive capture, 396 kB/s.
- **M3:** cyclic DMA, 0 drops at 10 MSa/s, 10 MB/s.
- **M4:** bit map on all 8 channels.
- **M5:** sustained ceilings confirmed with long runs: 50 / 100 / 200 / 200 MSa/s at 8 / 4 / 2 / 1 channels
  (1/2/4-ch confirmed 2026-09-17). `ratesweep` CSV now has `stalls` and `status` (`ok`/`violations`/`nosignal`).
- **M7:** trigger.
- **M6:** UART loopback decoded byte-exact with sigrok-cli.
- **M0:** stock `piopwm` example (2026-09-17).
- **Live `--stream` / `uart-listen.sh`, `--duration`, `--idle-stop`** (2026-09-19).
- **Unbounded capture, `--silence-timeout`, `--monitor`** (2026-09-21): 30 s at 2 MSa/s
  with RSS flat at 6 MB; monitor recorded 3 bursts, 1.09% of the elapsed time.

**Added but not yet run on the Pi: live per-burst shipping** (`--es-url`). Each burst is
handed to `sr2es.py` as it closes rather than after the run, timestamped at its first
edge. Host tests cover it, and the whole chain was exercised against a stub `_bulk`
endpoint on a dev machine. **The document shape changed**: `psf` is now flat (no `psd`,
no sub-objects), a common core plus the declared protocol's fields. See the README.

**Not yet verified:**
1. ~~`pi5la --stream` / `uart-listen.sh`~~ Done 2026-09-19: decoded `hello live` as it
   arrived (15.7 M samples at 2 MSa/s, 0 stalls). `--duration` / `--idle-stop` verified on
   the same wiring.
   **Wiring caveat:** a session was lost to a jumper whose pin-8 end was never in pin 8.
   A pull-up makes GPIO 2 read `hi` either way, so check against real traffic:
   `stty -F /dev/ttyAMA0 300 raw -echo`, send NULs, and poll `pinctrl lev 2,14` --
   both columns must move together. Pin 8 is the 4th hole of the even row from the
   USB-C end; the 1st and 2nd holes of that row are 5 V and must never be used.
2. **PulseView visual check** of `uart.sr` (M6).
2b. ~~`sr2es.py` `_bulk` upload~~ Done 2026-09-21: 3 documents accepted by
   `http://100.95.111.97:9200` (Kibana is on 5601). Credentials live in `ES_USER` /
   `ES_PASSWORD` in the user's own shell -- never in chat. The `wiresploit-analyst` role
   can write but not delete, so three early documents with the old shape
   (`event_type: "serial"`) are stuck in the index until an admin removes them.
   **Document shape is dictated by what is already in `wiresploit-poc1`:** `event_type`
   `onboard`, uppercase `protocol`, flat `psd` (`uart_type`, `port`, `baud_rate`,
   `data_bits`, `parity`, `stop_bits`, `framing_error`), with capture provenance under
   `psd.capture`. `psf.cb_id` appears in the network documents; its meaning is unknown.
3. **ESP32:** I want to sniff its UART (115200 baud, powered from my PC). **It needs a
   74LVC245 buffer and a shared GND**, which I don't have wired yet.
4. `ratesweep` `status=nosignal` rows: the code path has not been seen on hardware
   (every run so far was `status=ok`).
5. **Live shipping on the Pi.** `pi5la --rate 2M --monitor DIR --es-url URL` against a
   stub cluster first: a document must arrive about a second after each line, the `.sr`
   must disappear once confirmed, `psf.first_edge_time_ns` must match `date +%s.%N` at
   send time, and stalls must stay at zero. Then `--protocol raw` (the square-wave
   generator works, since nothing decodes it), then the outage retry, then the real
   cluster. Also check `chronyc tracking` and that `psf.clock_synchronised` is true.

## Key technical decisions (and why)

- **Capture uses the kernel's cyclic DMA** (`PIO_IOC_SM_CONFIG_XFER_V2` with the
  `DMA_CYCLE` flag, reads via `PIO_IOC_SM_XFER_DATA32`), not piolib's `pio_sm_xfer_data`
  read-ahead. Read-ahead lost samples at every buffer hand-over (stalls = buffer count).
  - Ring: 4 periods; `--dma-buf` sets the period size (default 1 MiB).
  - `-EOVERFLOW` from a read means dropped samples.
- **Stall detection reads FDEBUG** at `0xf0000008` via `READ_HW` / `WRITE_HW` (RXSTALL is
  bit `sm`, write 1 to clear), on pi5la's own `/dev/pio0` fd. piolib's
  `pio_sm_get_flags` panics on this firmware.
- **PIO capture:** `in pins, N`, `shift_right = true`, autopush threshold 32, FIFO
  joined to RX, 32/N samples per word.
  - **Verified map:** slot 0 in the low bits, channel k at bit `s*N + k`.
  - Trigger programs are built at runtime (`progbuild.c`) and checked against the
    pioasm output of `la8.pio` / `la8_trig.pio`.
- **Generator:** `sqwave.pio` above ~1.53 kHz, `sqwave_slow.pio` (a delay loop) below.
- **Live mode:** `--stream` writes one byte per sample to stdout. `uart-listen.sh` pipes
  it into `sigrok-cli -I binary:numchannels=8:samplerate=<Hz> -i -`. In that input,
  channels are named `0`–`7`, not `GPIO2`.
- **Recording streams to disk** (`src/srwriter.c`, `srzip_write_file()`): samples are
  appended to `<out>.logic` and zipped into the `.sr` at the end, so a capture needs no
  length and RAM stays flat. `capture_run()`'s in-RAM path is kept only for `--raw` and
  `--mode naive` (M2/M4 and the tools).
- **Auto-stop:** `--silence-timeout T` (was `--idle-stop`, alias kept) scans each DMA
  period for a word that isn't the idle level repeated (`src/idle.c`), waits for the first
  change keeping `--pre` of lead-in, then stops after T without one.
- **`--monitor DIR`** records only bursts (`src/burst.c` state machine, `src/record.c`
  I/O): burst-NNNN.sr plus index.json with start_time/duration/gap_before_s. Idle is never
  written. Trailing idle is removed with `ftruncate()`. Detection lags by up to one period.
- **Elasticsearch export:** `tools/sr2es.py` (stdlib only) decodes a capture with
  sigrok-cli, groups bytes into messages by idle gap, and writes bulk NDJSON for the
  `wiresploit-poc1` index, uploading when `--es-url` is given. Timestamps are built in
  integer nanoseconds (a double is ~240 ns coarse at epoch scale). Wall-clock time comes
  from a per-burst `burst-NNNN.sr.meta.json` (or `<out>.sr.meta.json`), written by
  record.c. Credentials only via `ES_API_KEY` / `ES_USER` + `ES_PASSWORD`.
- **Live shipping:** `--es-url` makes record.c spawn `sr2es.py --ship DIR` as each burst
  closes (`src/shipper.c`, `posix_spawn`, its own session). Never waited for until
  teardown -- decoding or uploading inside the capture path would overrun the
  four-period DMA ring. One shipper at a time; disk state is the queue, so a burst that
  closes while one is busy, a decode that failed, and a cluster that was down all
  resolve the same way: the next sweep finds it. A confirmed upload deletes the `.sr`
  and `.ndjson` and marks the sidecar `shipped`; `--protocol raw` keeps its `.sr`,
  because a raw document carries no payload.
- **Arrival timestamps:** a burst is stamped when its first edge is seen, not when it is
  decoded. `capture.c` now reports `start_realtime_ns` (at `pio_sm_set_enabled`, so
  sample 0 is not skewed by setup) and `period_realtime_ns` (as each period lands);
  record.c pins each period's last sample to the latter and places everything relative
  to it, so a long run follows NTP instead of drifting with the PIO crystal. Clock
  discipline is read with `adjtimex(2)` and recorded per burst.
- **Protocols are declared, never guessed:** `--protocol uart|raw` is set before the run
  and written into every sidecar, which wins over the shipper's flag. `raw` skips
  decoding entirely and ships one document per burst (arrival time, duration, edge
  count, active channels), so an unknown bus still reaches the index. I2C/SPI are a
  `PROTOCOLS` entry in sr2es.py when there is hardware to verify them on.
- **Exit codes:** `0` OK, `1` error, `2` dropped samples (RXSTALL or ring overflow). The
  wall-clock loss estimate is informational only.
- M8 (RLE, pre-trigger) was skipped by agreement.

## Useful facts

- **Pins:**
  - Channels 0–7 = GPIO 2–9 = physical pins 3, 5, 7, 29, 31, 26, 24, 21.
  - Generator = GPIO 20 = pin 38.
  - UART TX = GPIO 14 = pin 8.
  - GND = pins 6, 9, 39, and others.
- **Idle levels:** GPIO 2–8 high (pull-ups), GPIO 9 low. An undriven GPIO 20 on a jumper
  idles high.
- **SD card writes 14.4 MB/s** but stalls in bursts: file capture defaults to a
  4 x 4 MiB DMA ring (`CAPTURE_DMA_BUF_RECORD`), since 4 x 1 MiB overflowed after 7 s at
  only 2 MB/s. `--stream` and the tools keep 4 x 1 MiB.
- **CMA pool is 64 MB** (`CmaTotal 65536 kB`): the largest DMA ring is 4 × 8 MiB.
  66.7 MB/s packed is unreliable at every `--dma-buf` (≤3/5 clean); 100 MB/s fails outright.
  50 MB/s is reliable: 8ch@50M, 4ch@100M, 2ch@200M each 5/5 clean (2026-09-17).
- **RP1 PIO clock is 200 MHz:** rate = 200 MHz / divider (int + frac/256). Integer
  dividers avoid jitter.
- **A capture of all `0x7f7f7f7f` words** means no signal reached any channel: check the
  jumper.
