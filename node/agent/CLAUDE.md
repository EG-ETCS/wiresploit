# CLAUDE.md

Guidance for Claude Code (and humans) working in this repository.

## What this is

**pi5la** is an 8-channel logic analyzer for the **Raspberry Pi 5**. It samples
GPIO 2–9 with a **PIO** state machine inside the Pi's **RP1** southbridge,
streams the packed samples to RAM by **cyclic DMA over PCIe**, and writes
sigrok `.sr` files that open in PulseView. Target: **10 MSa/s on 8 channels
with zero dropped samples** (met; the sustained 8-channel ceiling is 50 MSa/s).

The C core is the analyzer. On top of it: a live UART listener (`--stream` +
`tools/uart-listen.sh`), an unbounded/monitor recorder (`src/record.c`), a
Flask web UI (`web/`), and an Elasticsearch decode pipeline (`tools/sr2es.py`).

> The git repository root is `pi5la/` (this directory), and now holds the whole
> project, including `SPECS.md` (the original brief). The parent `wiresploit/`
> folder is **not** a repo and contains only this directory.

## Build & test

The full program only builds and runs **on a Pi 5** (needs `piolib` from
`raspberrypi/utils`, `pioasm` from `pico-sdk`, and `/dev/pio0`). The
hardware-independent logic builds and tests **on any Linux host**.

```bash
# On the Pi — full build (see README.md §Building for the piolib/pioasm setup)
cmake -S . -B build -DPIOASM_EXECUTABLE=$HOME/build-pioasm/pioasm
cmake --build build -j4
(cd build && ctest)

# On any host — logic-only unit tests, no piolib/pioasm/hardware
cmake -S . -B build-host -DPI5LA_TESTS_ONLY=ON
cmake --build build-host -j4
(cd build-host && ctest)
```

- `-DPI5LA_TESTS_ONLY=ON` builds only `pi5la_core` and its unit tests — use it
  when working on pure logic (unpack, srzip, analyze, progbuild, burst, idle,
  util, srwriter) without a Pi.
- Requires: CMake ≥ 3.16, a C11 compiler, `libzip-dev`, `python3` (for the
  two Python tests). Runtime access to `/dev/pio0` needs the user in the
  `gpio` group — **no `sudo`**.
- Run one test: `(cd build && ctest -R test_unpack --output-on-failure)`.

## Architecture

Three layers meet — never call RP1 registers directly; everything is an
**ioctl to `/dev/pio0`** that the kernel relays to RP1 firmware over PCIe
(≈10 µs round trip — the fact that dictates the whole capture design).

```
RP1 silicon:  GPIO 2–9 → PIO state machine (`in pins,8`) → RX FIFO → DMA
Linux kernel: rp1-pio driver, /dev/pio0, cyclic DMA, FDEBUG register
Userspace:    pi5la (C11) + piolib
On top:       web/app.py (Flask) · sigrok-cli/PulseView · tools/sr2es.py → ES
```

### CMake targets

- `pi5la_core` (static lib) — host-safe logic, no piolib. Sources: `analyze`,
  `burst`, `idle`, `srwriter`, `progbuild`, `srzip`, `unpack`, `util`.
- `pi5la_hw` (static lib) — needs piolib + generated `.pio` headers:
  `capture`, `dmacyclic`, `record`, `shipper`, `fdebug`, `generator`.
- Executables: `pi5la` (CLI), `ratesweep`, `bitprobe`, `fdebugtest`; scripts
  `sr2es.py` and `uart-listen.sh` are copied next to them.
- `.pio` files are assembled to headers by `pioasm` at build time
  (`la8`, `la8_trig`, `sqwave`, `sqwave_slow`).

### File map

| Area | Files | Role |
|---|---|---|
| CLI | `src/main.c` | flag parsing, signals, dispatch, reporting |
| Capture | `src/capture.c/.h` | PIO setup; `run_dma`, `run_naive`, `capture_stream` |
| DMA | `src/dmacyclic.c/.h`, `src/rp1_ioctl.h` | cyclic-DMA ioctls, kernel ABI structs |
| Drop check | `src/fdebug.c/.h` | read/clear FDEBUG RXSTALL via READ_HW/WRITE_HW |
| PIO builder | `src/progbuild.c/.h` | hand-encode `in`/`wait` opcodes at runtime + parse triggers |
| Recording | `src/record.c/.h`, `src/burst.c/.h`, `src/idle.c/.h` | unbounded/monitor/silence, burst gating, arrival timestamps |
| Live export | `src/shipper.c/.h` | spawn `sr2es.py` per burst, out of the capture path |
| Output | `src/srzip.c/.h`, `src/srwriter.c/.h` | build/stream sigrok `.sr` (libzip) |
| Unpack | `src/unpack.c/.h` | packed word → 1 byte/sample (measured bit map) |
| Support | `src/analyze.c/.h`, `src/generator.c/.h`, `src/util.c/.h` | run-length checks, test signal, rate/clkdiv math |
| PIO | `pio/*.pio` | capture, triggered capture, generator programs |
| Tools | `tools/ratesweep.c`, `tools/bitprobe.c`, `tools/fdebugtest.c`, `tools/sr2es.py`, `tools/uart-listen.sh` | benchmark, bit-order finder, stall check, ES export, live decode |
| Web | `web/app.py`, `web/args.py`, `web/static/`, `web/templates/` | Flask UI that spawns `pi5la` as a subprocess; light-only palette, no auth |
| Tests | `tests/*` | host unit tests (ctest) |

## Critical constraints — read before changing the capture path

These are load-bearing; violating them silently loses samples or corrupts data.
Background in `SPECS.md §3` and `REPORT.md`.

1. **Never poll the FIFO word-by-word for bulk data.** Each piolib call is a
   PCIe message (~10 µs). A read loop caps ~250–400 kB/s. All bulk data goes
   through DMA. `run_naive` exists only as the deliberately-slow M2 baseline
   (`--mode naive`); keep it, don't "fix" it.
2. **Pack 4 samples per 32-bit word** — `sm_config_set_in_shift(right,
   autopush, 32)`. Skipping this wastes 75% of PCIe bandwidth.
3. **Use cyclic DMA, not queued buffers.** Queued receives drop samples at
   every buffer hand-over; the kernel's cyclic ring is gapless
   (`RP1_PIO_SM_CONFIG_XFER_FL_DMA_CYCLE`). The rp1-pio ring max is 4 periods.
4. **Drop detection is manual.** The Pi's RP1 firmware lacks
   `PIO_SM_GET_FLAGS`, so read the PIO **FDEBUG** register (`0xf0000008`,
   RXSTALL = bit `sm`) via READ_HW/WRITE_HW. Don't reintroduce
   `pio_sm_get_flags()` — it panics on this firmware.
5. **The bit-order map in `unpack.c` was measured on hardware** (M4, via
   `bitprobe`), not derived. The 8-channel table is verified; 1/2/4-channel
   follow the same rule but are unverified. If you change shift config, re-run
   `bitprobe` — don't hand-edit the tables from theory.
6. **Capture pins are consecutive** from the input base; there is no remapping.
7. **`progbuild` output is self-checked** against the pioasm-built `la8` /
   `la8_trig` references (`capture_selftest_programs`). Keep the `.pio` files
   and the runtime encoder in sync.
8. **Kernel ≥ 6.18** is required for cyclic DMA.
9. **Nothing slow may run inside `burst_close()`.** Live export spawns
   `sr2es.py` (`shipper.c`, `posix_spawn`) and never waits for it until
   teardown: a `sigrok-cli` run or a stalled HTTP POST in the capture path
   overruns the four-period ring. One shipper at a time; a burst that closes
   while it is busy is left on disk for the next sweep. Disk state is the queue.
10. **A burst is timestamped when its first edge arrives**, not when it is
   decoded. `burst_action_t::first_edge` is the un-adjusted change (the pre-roll
   is subtracted only from `start`), and `record.c` stamps it on the spot,
   anchored to the wall clock read as that DMA period landed — `t0 + n/rate`
   would drift with the PIO crystal (±20 ppm is ~72 ms/hour) and would not
   follow NTP. Wall-clock times are integer nanoseconds everywhere: a double
   holds only ~240 ns at epoch scale.
11. **The protocol is declared before a run** (`--protocol`) and recorded in
   every `burst-NNNN.sr.meta.json`. Nothing infers it from the data. The
   capture path stays protocol-blind — it detects edges.
12. **The per-burst sidecar is written after the `.sr` is final**, so its
   presence means the burst is complete. The shipper keys off that, never off
   `index.json`, which is rewritten on every burst.
13. **Burst numbering resumes from what the directory already holds**
   (`next_burst_index`). The directory is the shipper's queue, so unshipped
   bursts and shipped bursts' sidecars persist across runs; numbering from 1
   again overwrites data that was never sent. Covered by `test_record`.

## Hardware safety (state it in any user-facing output)

Pi 5 GPIO is **3.3 V and not 5 V-tolerant**; overvoltage destroys RP1 (soldered,
kills the board). External targets need a **74LVC245 buffer + series resistors +
shared ground**. The only safe bare-wire test is a **loopback jumper** between
Pi header pins (e.g. GPIO 20 → GPIO 2).

## Conventions

- C11 built as **gnu11** (piolib uses GNU statement expressions), `-Wall
  -Wextra` clean. Small single-purpose modules, each with a `.h` and a host
  unit test where the logic is hardware-independent.
- Functions return `0`/`-1` with a message on `stderr`; `capture_*` fill a
  result struct. **Exit codes:** `0` ok, `1` error, `2` dropped samples.
- Kernel ABI lives only in `src/rp1_ioctl.h` (mirrors `rp1_pio_if.h`).
- `web/args.py` and `tools/sr2es.py` have their own Python tests
  (`tests/test_web_args.py`, `tests/test_sr2es.py`); keep them passing.
- **No credential ever reaches an argv or a form field.** `pi5la`, `sr2es.py`
  and the web UI all read `ES_API_KEY` / `ES_USER` / `ES_PASSWORD` from the
  environment; the web server passes its own environment to the capture it
  spawns. `test_web_args.py` asserts nothing secret can be forwarded.
- `web/args.py` has two protocol lists with the same name and different jobs:
  `PROTOCOLS` (uart/i2c/spi) picks a **sigrok decoder** for the live and decode
  views; `SHIP_PROTOCOLS` (uart/raw) matches `sr2es.py`'s table and decides what
  a **shipped document** looks like.
- `sr2es.py` documents have a **flat `psf`**: a common core plus the declared
  protocol's fields, no sub-objects. A new protocol is a `PROTOCOLS` entry, and
  must not shadow a core field name — an import-time assert enforces it.
- New hardware behaviour should be verifiable by loopback (generator → a
  channel) so it can be checked without an external target.

## Documentation

- `SPECS.md` (repo root) — the original brief and milestone acceptance tests.
- `README.md` — build steps, hardware safety, throughput measurements.
- `PROJECT_SUMMARY.md` — what was built and the problems solved.
- `REPORT.md` — findings, rate ceilings, why more channels means a lower rate.
- `CLAUDE_HANDOFF.md` — prior agent handoff notes.

## Status

M0–M7 verified on Pi 5 hardware; plus live listening, unbounded/monitor
capture, silence-timeout, and the Elasticsearch export. Open: PulseView visual
check (M6), an external target through the 74LVC245 buffer, M8 (RLE +
pre-trigger ring buffer, not started), and a Pi run of live per-burst shipping
(`--es-url`), which so far is covered only by host tests and a stub cluster.
