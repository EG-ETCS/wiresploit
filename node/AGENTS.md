# node/AGENTS.md

Agent context for the Wiresploit **Node**: everything under `node/`. Read the
root `AGENTS.md` first for terminology, branches and the update rule.

Core and web application owners can copy this file as a template for their own
component. Keep the headings, replace the content. See "Using this file as a
template" at the end.

## What this is

A Node is the device attached to the DUT. It has two functions:

- **Capture** (`node/capture/`): **pi5la**, an 8-channel logic analyzer for the
  Raspberry Pi 5. It samples GPIO 2–9 with a PIO state machine inside the Pi's
  RP1 chip, moves the packed samples to RAM by cyclic DMA over PCIe, and writes
  sigrok `.sr` files that open in PulseView. Target: 10 MSa/s on 8 channels with
  zero dropped samples. The target is met; the sustained 8-channel ceiling is
  50 MSa/s.
- **Actions** (`node/actions/`): performs configured actions on the DUT. Not
  started; the folder is a placeholder.

Around the C core: live UART listening (`--stream` and `uart-listen`), unbounded
and monitor recording (`src/record.c`), the Elasticsearch exporter
(`node/exporter/sr2es.py`), and a Flask capture test page
(`node/capture/tools/web_ui/`). The test page is a **local Pi testing tool
only**. Real capture commands will come from Core, so don't build product
features on it.

## Build and test

Build and test in the dev container, or on a laptop prepared by
`setup_host.sh`. The Pi only receives finished binaries; it never builds.

- **Dev container:** in VS Code, open the repo and choose "Reopen in Container".
  It uses `.devcontainer/node/devcontainer.json`, built from
  `node/docker/Dockerfile` (Debian Bookworm with the arm64 cross toolchain,
  pioasm and piolib pinned). The repo is mounted at `/work` and the container
  runs as your own uid.
- **Without VS Code**, from the repo root:

  ```bash
  docker build -t wiresploit-node -f node/docker/Dockerfile .
  docker run --rm -it -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work \
      wiresploit-node node/scripts/test.sh all
  ```

- **Without Docker:** `node/scripts/setup_host.sh` checks for or installs the
  same packages and creates `.venv/`. `setup_host.sh --check` only reports what
  is missing.

All scripts are in `node/scripts/`, start with `set -euo pipefail`, find the
repo from their own location, take `--help`, and never run `sudo` without
asking.

| Script | Runs on | What it does |
|---|---|---|
| `test.sh [unit\|integration\|all]` | container or host | Runs the tests. `all` is the default, and is what every gate runs |
| `build_pi.sh` | container | Cross-compiles for the Pi into `node/capture/build-arm64/` and checks the result is "ARM aarch64" |
| `deploy_to_pi.sh` | container or host | Copies the install folder to the Pi with `rsync` over ssh. Asks for `<pi-host>`, the ssh user and `<install_dir>` on first run and saves them in the gitignored `node/.deploy.conf`. `--dry-run` prints the rsync command |
| `setup_pi.sh` | the Pi | Installs the Pi's packages after asking, checks `/dev/pio0` and the `gpio` group, asks for each setting and writes `node.conf`, optionally enables UART on GPIO 14. `--check` only reports; `--config <file>` skips the dialogs |
| `docs.sh [build\|serve]` | container or host | Builds or serves the MkDocs site |
| `setup_host.sh [--check]` | host | Prepares a laptop that has no Docker |

Test levels (details in `docs/02_Engineering/04_Testing.md`):

1. **Unit** (`test.sh unit`): C tests in `node/capture/tests/unit/`
   (`test_record` builds on the host with `tests/unit/stubs/piolib.h`), plus the
   Python tests in `exporter/tests/`, `capture/tools/web_ui/tests/` and
   `lib/tests/`. pytest is configured in `node/pyproject.toml`.
2. **Integration** (`test.sh integration`): synthetic `.sr` samples in
   `node/capture/tests/integration/samples/` run through the real pipeline
   (`test_pipeline`) and the real exporter (`test_decode.py`), compared with
   `expected/`. No Pi needed.
3. **Pi hardware** (`<install_dir>/bin/test_pi.sh` on the Pi; source in
   `node/capture/tools/test_pi.sh`): loopback checks with PASS or FAIL for
   each. It asks you to confirm the wiring with `pinctrl` first.

Every test is registered with CTest and labelled `unit` or `integration`;
`ctest` runs the Python tests through pytest, or plain unittest when pytest is
missing. **`test.sh all` must pass before a task branch is merged.** A change to
the capture hardware path (`src/capture.c`, `src/dmacyclic.c`, `src/fdebug.c`,
`src/generator.c`, the `.pio` programs) also needs a passing `test_pi.sh` run.

Running tests by hand, without the wrapper:

```bash
cmake -S node/capture -B node/capture/build-host -DPI5LA_TESTS_ONLY=ON
cmake --build node/capture/build-host -j4
ctest --test-dir node/capture/build-host -L unit --output-on-failure
ctest --test-dir node/capture/build-host -R test_unpack --output-on-failure
```

- `-DPI5LA_TESTS_ONLY=ON` builds the tests without piolib or pioasm, so it works
  on any Linux machine.
- Requires CMake 3.16 or newer, a C11 compiler, `libzip-dev` and `python3`.
- At run time on the Pi, `/dev/pio0` needs the user in the `gpio` group. Never
  run pi5la with `sudo`.

## Paths and settings

Nothing may depend on a home folder or a fixed path. No `~`, home-folder or
machine-specific path in code, scripts or docs.

- **The Node root** is the nearest folder above a program that holds
  `node.conf.example`. In the repo that is `node/`; on a Pi it is
  `<install_dir>`, wherever it was copied. Every program finds its files by
  walking up from its own location (`node/lib/node_config.py`, and
  `scripts/lib/config.sh` for the shell scripts). Moving the folder keeps it
  working.
- **`node.conf`** in the Node root holds every per-machine setting as plain
  `KEY=value` lines. It is parsed, never executed, and is gitignored. Copy it
  from `node/node.conf.example`, which documents each key. It may hold
  credentials, so keep it at mode 600.
- **Precedence**, highest first: command-line flag > environment variable >
  `node.conf` > built-in default.
- Relative paths in `node.conf` resolve against the Node root.
- `pi5la` finds `sr2es` next to its own binary, then on `PATH`
  (`src/shipper.c`).

The install folder on a Pi:

```
<install_dir>/
    bin/        pi5la ratesweep bitprobe fdebugtest sr2es uart-listen test_pi.sh
    web_ui/     capture test page
    lib/        node_config.py
    scripts/    setup_pi.sh lib/
    node.conf.example  node.conf  captures/
```

## Architecture

Never touch RP1 registers directly. Everything is an **ioctl to `/dev/pio0`**,
which the kernel relays to RP1 firmware over PCIe. That round trip takes about
10 µs, and this one fact dictates the whole capture design.

```
RP1 silicon:  GPIO 2–9 → PIO state machine (in pins, 8) → RX FIFO → DMA
Linux kernel: rp1-pio driver, /dev/pio0, cyclic DMA, FDEBUG register
Userspace:    pi5la (C11) + piolib
On top:       web_ui/app.py (Flask) · sigrok-cli / PulseView · exporter/sr2es.py → Elasticsearch
```

CMake targets (`node/capture/CMakeLists.txt`):

- `pi5la_core` (static library): host-safe logic, no piolib. Sources: `analyze`,
  `burst`, `idle`, `srwriter`, `progbuild`, `srzip`, `unpack`, `util`.
- `pi5la_hw` (static library): needs piolib and the generated `.pio` headers:
  `capture`, `dmacyclic`, `record`, `shipper`, `fdebug`, `generator`.
- Executables: `pi5la`, `ratesweep`, `bitprobe`, `fdebugtest`. The build copies
  `exporter/sr2es.py` and `tools/uart-listen.sh` next to them, and the install
  step names them `sr2es` and `uart-listen`.
- `pioasm` assembles the `.pio` files into headers at build time: `la8`,
  `la8_trig`, `sqwave`, `sqwave_slow`.

## Critical constraints: read before changing the capture path

These are load-bearing. Breaking one silently loses samples or corrupts data.
Background: the original brief (`docs/03_Documentation/Node/spec.md`, §3) and
the findings report (`docs/03_Documentation/Node/findings_report.md`).

1. **Never poll the FIFO word by word for bulk data.** Each piolib call is a
   PCIe message (about 10 µs). A read loop tops out at about 250–400 kB/s. All
   bulk data goes through DMA. `run_naive` exists only as the deliberately slow
   M2 baseline (`--mode naive`). Keep it, and don't "fix" it.
2. **Pack 4 samples per 32-bit word**: `sm_config_set_in_shift(right,
   autopush, 32)`. Skipping this wastes 75% of the PCIe bandwidth.
3. **Use cyclic DMA, not queued buffers.** Queued receives drop samples at
   every buffer hand-over; the kernel's cyclic ring is gapless
   (`RP1_PIO_SM_CONFIG_XFER_FL_DMA_CYCLE`). The rp1-pio ring holds at most
   4 periods.
4. **Drop detection is manual.** The Pi's RP1 firmware lacks
   `PIO_SM_GET_FLAGS`, so read the PIO **FDEBUG** register (`0xf0000008`,
   RXSTALL = bit `sm`) through READ_HW / WRITE_HW. Don't reintroduce
   `pio_sm_get_flags()`: it panics on this firmware.
5. **The bit-order map in `unpack.c` was measured on hardware** (M4, with
   `bitprobe`), not derived. The 8-channel table is verified; the 1, 2 and
   4-channel tables follow the same rule but are unverified. If you change the
   shift configuration, re-run `bitprobe`. Don't hand-edit the tables from
   theory.
6. **Capture pins are consecutive** from the input base. There is no remapping.
7. **`progbuild` output is self-checked** against the pioasm-built `la8` and
   `la8_trig` references (`capture_selftest_programs`). Keep the `.pio` files
   and the runtime encoder in sync.
8. **Kernel 6.18 or newer** is required for cyclic DMA.
9. **Nothing slow may run inside `burst_close()`.** Live export spawns
   `sr2es` (`shipper.c`, `posix_spawn`) and never waits for it until teardown:
   a `sigrok-cli` run or a stalled HTTP POST in the capture path overruns the
   four-period ring. One shipper runs at a time; a burst that closes while it
   is busy is left on disk for the next sweep. Disk state is the queue.
10. **A burst is timestamped when its first edge arrives**, not when it is
    decoded. `burst_action_t::first_edge` is the unadjusted change (the
    pre-roll is subtracted only from `start`), and `record.c` stamps it on the
    spot, anchored to the wall clock read as that DMA period landed.
    `t0 + n/rate` would drift with the PIO crystal (±20 ppm is about
    72 ms/hour) and would not follow NTP. Wall-clock times are integer
    nanoseconds everywhere: a double holds only about 240 ns at epoch scale.
11. **The protocol is declared before a run** (`--protocol`) and recorded in
    every `burst-NNNN.sr.meta.json`. Nothing infers it from the data. The
    capture path stays protocol-blind: it detects edges.
12. **The per-burst sidecar is written after the `.sr` is final**, so its
    presence means the burst is complete. The shipper keys off the sidecar,
    never off `index.json`, which is rewritten on every burst.
13. **Burst numbering resumes from what the directory already holds**
    (`next_burst_index`). The directory is the shipper's queue, so unshipped
    bursts and shipped bursts' sidecars persist across runs; numbering from 1
    again overwrites data that was never sent. Covered by `test_record`.

## Hardware safety

State this in any user-facing output that involves connecting hardware.

- Pi 5 GPIO is **3.3 V and not 5 V tolerant**. Overvoltage destroys RP1, which
  is soldered to the board, so the Pi is lost.
- External targets need a **74LVC245 buffer, series resistors (100–330 Ω) and a
  shared ground**.
- The only safe bare-wire test is a **loopback jumper** between Pi header pins,
  for example GPIO 20 (pin 38) → GPIO 2 (pin 3).
- Never connect pin 1 (3.3 V) or pins 2 and 4 (5 V) to a GPIO.
- Have the user confirm the wiring with `pinctrl` before any loopback test.

Wiring diagram and buffer pinout: the capture guide's "Hardware safety" section.

## Conventions

- C11, built as **gnu11** (piolib uses GNU statement expressions), clean under
  `-Wall -Wextra`. Small single-purpose modules, each with a `.h` and a host
  unit test where the logic does not need hardware.
- Functions return `0` or `-1` with a message on `stderr`; `capture_*` fill a
  result struct. **Exit codes:** `0` OK, `1` error, `2` dropped samples.
- The kernel ABI lives only in `src/rp1_ioctl.h` (it mirrors `rp1_pio_if.h`).
- Python follows the coding rules (`docs/02_Engineering/01_Coding_Rules.md`):
  Black, Ruff, Google-style docstrings, module headers.
- `web_ui/args.py`, `exporter/sr2es.py` and `lib/node_config.py` have their own
  tests (`web_ui/tests/test_args.py`, `exporter/tests/test_sr2es.py`,
  `lib/tests/test_node_config.py`). Keep them passing.
- **No credential ever reaches an argv or a form field.** Credentials
  (`ES_API_KEY`, or `ES_USER` and `ES_PASSWORD`) come from the environment or
  from `node.conf`, and the environment wins. At startup, `sr2es.py` and the
  test page call `NodeConfig.export_es_credentials()` (`lib/node_config.py`),
  which copies the `node.conf` values into the process environment only where
  the environment does not already have them. The test page passes its
  environment to the `pi5la` it spawns, and the `sr2es` that `pi5la` spawns
  loads `node.conf` from its own location, so a shipped burst gets `node.conf`
  credentials too. `test_args.py` asserts nothing secret can be forwarded.
- `web_ui/args.py` has two protocol lists with similar names and different
  jobs: `PROTOCOLS` (uart, i2c, spi) picks a **sigrok decoder** for the live and
  decode views; `SHIP_PROTOCOLS` (uart, raw) matches `sr2es.py`'s table and
  decides what a **shipped document** looks like.
- `sr2es.py` documents have a **flat `psf`**: a common core plus the declared
  protocol's fields, no sub-objects. A new protocol is a `PROTOCOLS` entry and
  must not shadow a core field name; an import-time assert enforces this.
- New hardware behaviour should be verifiable by loopback (generator → a
  channel), so it can be checked without an external target.
- Usage examples and docs write commands as `<install_dir>/bin/…` and hosts as
  `<pi-host>`.

## File map

| Area | Files | Role |
|---|---|---|
| Settings | `node.conf.example`, `lib/node_config.py` | Every per-machine setting; Node root lookup and precedence |
| Scripts | `scripts/*.sh`, `scripts/lib/config.sh` | Setup, test, build, deploy, docs; shared shell helpers |
| Container | `docker/Dockerfile`, `../.devcontainer/node/devcontainer.json`, `capture/cmake/aarch64-linux-gnu.cmake` | Dev container and cross-compile toolchain |
| CLI | `capture/src/main.c` | Flag parsing, signals, dispatch, reporting |
| Capture | `capture/src/capture.c/.h` | PIO setup; `run_dma`, `run_naive`, `capture_stream` |
| DMA | `capture/src/dmacyclic.c/.h`, `capture/src/rp1_ioctl.h` | Cyclic-DMA ioctls, kernel ABI structs |
| Drop check | `capture/src/fdebug.c/.h` | Read and clear FDEBUG RXSTALL through READ_HW / WRITE_HW |
| PIO builder | `capture/src/progbuild.c/.h` | Encode `in` / `wait` opcodes at run time, parse triggers |
| Recording | `capture/src/record.c/.h`, `burst.c/.h`, `idle.c/.h` | Unbounded, monitor and silence-timeout capture, burst gating, arrival timestamps |
| Live export | `capture/src/shipper.c/.h` | Spawn `sr2es` per burst, outside the capture path |
| Output | `capture/src/srzip.c/.h`, `srwriter.c/.h` | Build and stream sigrok `.sr` files (libzip) |
| Unpack | `capture/src/unpack.c/.h` | Packed word → 1 byte per sample (measured bit map) |
| Support | `capture/src/analyze.c/.h`, `generator.c/.h`, `util.c/.h` | Run-length checks, test signal, rate and divider maths |
| PIO | `capture/pio/*.pio` | Capture, triggered capture, generator programs |
| Tools | `capture/tools/ratesweep.c`, `bitprobe.c`, `fdebugtest.c`, `uart-listen.sh`, `test_pi.sh` | Rate sweep, bit-order finder, stall check, live decode, Pi hardware tests |
| Test page | `capture/tools/web_ui/` | Flask page that runs `pi5la` as a subprocess. Local testing only, no authentication |
| Exporter | `exporter/sr2es.py` | Decode with sigrok-cli, build documents, send to Elasticsearch |
| Tests | `capture/tests/unit/`, `capture/tests/integration/`, `*/tests/` | C and Python unit tests, sample-based integration tests |
| Actions | `actions/README.md` | Placeholder for the Actions function |

## Documentation

All Node docs are on the MkDocs site under `docs/03_Documentation/Node/`:

- `index.md`: overview, status, pin map summary.
- `capture_guide.md`: build, install, usage, Elasticsearch export, test page,
  design notes.
- `monitor_mode.md`: `--monitor` in full.
- `cheatsheet.md`: day-to-day commands.
- `findings_report.md`: every measurement, and how each milestone was tested.
- `spec.md`: the original brief, kept as written.
- `decisions.md`: technical decisions and why. Add to it when you make one.

## Status

M0–M7 are verified on Pi 5 hardware, plus live listening, unbounded and monitor
capture, the silence timeout and the Elasticsearch export. Open: the PulseView
visual check (M6), an external target through the 74LVC245 buffer, M8 (RLE and
pre-trigger ring buffer, skipped by agreement), and a Pi run of live per-burst
shipping (`--es-url`), which so far is covered only by host tests and a stub
cluster.

## Using this file as a template

For a new component, copy this file to `<component>/AGENTS.md` and keep these
headings: what this is, build and test, paths and settings, architecture,
critical constraints, safety (if it touches hardware or a DUT), conventions,
file map, documentation, status. Link it from the table in the root
`AGENTS.md`, and start a `decisions.md` beside the component's docs.
