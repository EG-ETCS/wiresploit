# Testing

The Node is tested at three levels. Levels 1 and 2 run on any laptop or in the
dev container, with no Pi. Level 3 runs on a Raspberry Pi 5 with loopback
jumpers.

| Level | Where | What it proves | Command |
|---|---|---|---|
| 1. Unit | laptop, container | each module on its own: C core, record/monitor logic, Python helpers | `node/scripts/test.sh unit` |
| 2. Integration | laptop, container | committed sample captures pass through the real pipeline and the real exporter | `node/scripts/test.sh integration` |
| 3. Pi hardware | Raspberry Pi 5 | the capture hardware path works: PIO, DMA, channel map, throughput, UART decode | `<install_dir>/bin/test_pi.sh` |

`node/scripts/test.sh all` runs levels 1 and 2 together.

## The merge rule

**A phase branch is merged only when `node/scripts/test.sh all` passes**, that
is, every unit and integration test. Run it in the dev container, which has
`sigrok-cli`, so that no integration test is skipped. A change that touches the
capture hardware path (`src/capture.c`, `src/dmacyclic.c`, `src/fdebug.c`,
`src/generator.c`, the `.pio` programs) also needs a passing `test_pi.sh` run on
a Pi before it is merged.

## How the tests are wired

All tests are registered with CTest in `node/capture/CMakeLists.txt`. Each test
has exactly one label:

- `unit`: level 1. `ctest -LE integration` runs these.
- `integration`: level 2. `ctest -L integration` runs these.

`ctest` with no option runs both. `node/scripts/test.sh` is a wrapper around
these three commands.

Python tests run through `python3 -m pytest` when pytest is installed, and as
plain `python3 <file>` (unittest) when it is not. CMake decides this once, when
the build folder is configured, and prints which one it chose. All Python test
files are written with `unittest`, so they give the same result either way.
pytest is configured in `node/pyproject.toml`.

To run the tests by hand on a laptop, without the wrapper:

```bash
cmake -S node/capture -B node/capture/build-host -DPI5LA_TESTS_ONLY=ON
cmake --build node/capture/build-host -j
cd node/capture/build-host
ctest --output-on-failure                  # levels 1 and 2
ctest -L unit --output-on-failure          # level 1 only
ctest -L integration --output-on-failure   # level 2 only
```

`-DPI5LA_TESTS_ONLY=ON` builds the tests without piolib or pioasm, so it works
on any Linux machine. Every `build*/` folder is ignored by git.

The Python tests can also run on their own, from `node/`:

```bash
cd node
python3 -m pytest                          # every Python test in pyproject.toml
python3 -m pytest exporter/tests           # one folder
```

## Level 1: unit tests

### What they cover

C tests, in `node/capture/tests/unit/`:

| Test | Module |
|---|---|
| `test_unpack` | `src/unpack.c`: FIFO word to one byte per sample, the channel map |
| `test_srzip` | `src/srzip.c`: the `.sr` file and its metadata |
| `test_srwriter` | `src/srwriter.c`: streaming `.sr` writer, truncate, abort |
| `test_analyze` | `src/analyze.c`: run-length statistics |
| `test_progbuild` | `src/progbuild.c`: PIO program builder, trigger parsing |
| `test_util` | `src/util.c`: SI number parsing, clock dividers, clock helpers |
| `test_idle` | `src/idle.c`: activity scan on packed words |
| `test_burst` | `src/burst.c`: the monitor-mode burst state machine |
| `test_record` | `src/record.c` and `src/shipper.c`: continuous and monitor recording, index and sidecar files |

`test_record` compiles `record.c` and `shipper.c` directly and replaces
`capture_stream()` with a stub that feeds synthetic periods. The header
`tests/unit/stubs/piolib.h` stands in for the real piolib header and only
declares the `PIO` type. That is why `test_record` builds and runs on a laptop.

Python tests:

| Test (CTest name) | File |
|---|---|
| `test_web_args` | `node/capture/tools/web_ui/tests/test_args.py` |
| `test_sr2es` | `node/exporter/tests/test_sr2es.py` |
| `test_node_config` | `node/lib/tests/test_node_config.py` |

### Run

```bash
node/scripts/test.sh unit
```

### Add a unit test

C:

1. Create `node/capture/tests/unit/test_<module>.c`. Include `check.h`, use
   `CHECK(condition)` for each assertion and end `main()` with `CHECK_DONE();`.
2. Add `test_<module>` to the `foreach(t ...)` list in the Level 1 part of
   `node/capture/CMakeLists.txt`. It is then built, linked with `pi5la_core`,
   registered and labelled `unit`.
3. If the module needs a piolib function, do not add it to `stubs/piolib.h`.
   Stub the function in the test file, the way `test_record.c` stubs
   `capture_stream()`, and compile the module's `.c` file into the test
   directly.

Python:

1. Create `tests/test_<module>.py` next to the module. Write it with
   `unittest`, end it with `if __name__ == "__main__": unittest.main()`, and
   give it a file name no other test file uses.
2. Register it in `node/capture/CMakeLists.txt` with
   `pi5la_python_test(test_<module> <path to file> unit)`.
3. If it lives in a new folder, add that folder to `testpaths` in
   `node/pyproject.toml`.

Unit tests must not need Elasticsearch, `sigrok-cli`, network access or a Pi.
Name each test by its condition and expected result, as the coding rules say.

## Level 2: integration tests

### What they cover

Integration tests run committed sample captures through the real code: the
same C functions pi5la runs while capturing, and the real `sr2es` command line.
The files are in `node/capture/tests/integration/`:

```
make_samples.c     builds the synthetic samples
samples/           the .sr captures and their .sr.meta.json sidecars
expected/          what each test must get from them
test_pipeline.c    C: pack, unpack, idle scan and burst state machine
test_decode.py     Python: sr2es (and sigrok-cli) to Elasticsearch documents
```

The samples. Each is 8 channels at 1 MSa/s, idle level 0x01 (channel 0 high,
the rest low), with probes named `GPIO2` to `GPIO9` and a sidecar with a fixed
start time:

| Sample | Length | Content | Declared protocol |
|---|---|---|---|
| `uart_hello.sr` | 4096 samples | `hello`, UART 8N1 at 115200 baud on channel 0, start bit at sample 1000 | `uart` |
| `two_bursts.sr` | 8192 samples | two 100-sample low pulses on channel 0, at samples 2000 and 4200 | `raw` |
| `all_idle.sr` | 4096 samples | nothing moves | `uart` |

The tests:

| Test (CTest name) | What it checks |
|---|---|
| `test_samples_current` | `make_samples --check`: the committed samples are exactly what `make_samples.c` builds today |
| `test_pipeline` | every case in `expected/*.bursts`: burst start, first edge and end, sample for sample |
| `test_decode_raw` | `two_bursts.sr` gives one RAW document, timestamped at its first edge, equal to `expected/two_bursts.docs.json` |
| `test_decode_uart` | `uart_hello.sr` decodes to one document with payload `hello`, equal to `expected/uart_hello.docs.json`; `all_idle.sr` decodes to nothing and `sr2es` exits 1 |

`test_pipeline` feeds the samples to the idle scan and the burst state machine
period by period, as `src/record.c` does. The two `two_bursts` cases put a quiet
stretch of exactly the gap length on a period boundary: with `gap 1992` the
pulses become two bursts, with `gap 1993` one. An off-by-one error in the gap
test, in either direction, fails one of the two cases.

`test_decode_uart` needs `sigrok-cli`. Without it, its cases are skipped with
the reason `sigrok-cli not found on PATH`, and CTest reports the test as
skipped, not passed. The dev container has `sigrok-cli`. `test_decode_raw` runs
no decoder, so it always runs.

No test talks to Elasticsearch. `test_decode.py` runs `sr2es` with an empty
`--es-url`, so `sr2es` writes the NDJSON file and uploads nothing.

### Run

```bash
node/scripts/test.sh integration
```

### Add a pipeline case

Add a file `node/capture/tests/integration/expected/<name>.bursts`. No code
changes. Format:

```
# Comment lines start with #. Say how the expected numbers were worked out.
sample two_bursts.sr      a file in samples/
period_words 256          FIFO words fed to the state machine per period
pre 100                   lengths in samples
post 200
gap 1992
burst 1900 2000 2304      start, first edge, end; one line per burst
burst 4100 4200 4504
```

A case with no `burst` line expects no burst. Work the numbers out from the
sample's definition by hand, then run the test. Do not copy them from the
program's output: then the test checks nothing.

### Add or change a synthetic sample

1. Edit the `samples[]` table and the build functions in `make_samples.c`.
   Keep samples small (a few thousand samples each) and describe each one in the
   comment at the top of the file.
2. Rebuild the files: `cmake --build <build folder> --target integration_samples`.
3. Add or update its `expected/*.bursts` case, and its documents (below).
4. Commit `make_samples.c`, `samples/` and `expected/` together.
   `test_samples_current` fails if the committed samples and `make_samples.c`
   disagree.

A zip file records the time each member was written, so two builds of the same
`.sr` never have identical bytes. `make_samples --check` therefore compares
each zip member's name and contents, and the sidecars byte for byte.

### Update the expected documents

After an intended change to what `sr2es` writes:

```bash
cd node
PI5LA_UPDATE_EXPECTED=1 python3 -m pytest capture/tests/integration
git diff capture/tests/integration/expected
```

Review the diff before committing it. The same applies when a new `sigrok-cli`
version moves the sample positions of decoded bytes.

### Add a real capture from the Pi

1. Capture something short on the Pi, on the loopback rig only. For example,
   with the UART loopback wired (see level 3):
   `pi5la --rate 1M --duration 2s --silence-timeout 200ms --out uart_real.sr`
   while sending a known line on `/dev/ttyAMA0`. Keep each file well under
   100 KB.
2. Copy both files into `samples/`: the `.sr` and the `.sr.meta.json` sidecar
   pi5la writes next to it. Without the sidecar `sr2es` falls back to the
   file's modification time, and the documents change on every checkout.
3. Add a `.bursts` case for it if burst boundaries matter, and a test method in
   `test_decode.py` that checks the decoded payload against the text you sent.
   Then write its `expected/*.docs.json` with `PI5LA_UPDATE_EXPECTED=1`.
4. Commit only test traffic you sent yourself. Never commit a capture of a real
   target's bus: it can hold credentials or other private data.

Real captures are not built by `make_samples`, so `test_samples_current` does
not check them.

## Level 3: Pi hardware tests

### What they cover

`test_pi.sh` runs the loopback self-tests from the pi5la README and prints one
PASS or FAIL line for each, then a summary:

| Check | Wiring | Passes when |
|---|---|---|
| `fdebugtest` | none | stall detection (FDEBUG RXSTALL) is seen and cleared |
| `bitprobe-map` | pin 38 (GPIO 20) to pin 3 (GPIO 2) | `bitprobe --probe` finds channel 0 in word bits 0 8 16 24, in that order |
| `bitprobe-verify` | same | `bitprobe --verify 0 --generator 100k --rate 10M --samples 16M` prints PASS |
| `ratesweep` | same | `ratesweep --channels 8 --dividers 20 --samples 40M`: 10 MSa/s, 0 violations, 0 stalls, status `ok` |
| `uart-loopback` | pin 8 (GPIO 14) to pin 3 (GPIO 2) | 50 lines sent on `/dev/ttyAMA0` at 115200 baud, captured by `pi5la` and decoded by `sigrok-cli`, match byte for byte |

Before it captures anything, the script prints the hardware-safety warning and
the wiring, and asks you to confirm each wiring step from `pinctrl lev` output.
It continues only when you type `yes`.

**Hardware safety.** Pi 5 GPIO is 3.3 V and not 5 V tolerant. Overvoltage
destroys RP1 and, with it, the Pi. These tests use loopback jumpers between Pi
header pins only, one jumper at a time, with nothing external connected. Never
connect a 3.3 V, 5 V or ground pin to a GPIO.

### Run

On the Pi, after deploying:

```bash
<install_dir>/bin/test_pi.sh            # or --keep to keep every log and capture
```

In the repo on a Pi, after building the Pi binaries, run
`node/capture/build/test_pi.sh`. The build copies it next to `pi5la`.

It needs `pinctrl`, `sigrok-cli` and `stty`, and the UART check needs
`dtparam=uart0=on` in `/boot/firmware/config.txt` (then reboot) so that
`/dev/ttyAMA0` exists. Exit status is 0 when every check passes and 1
otherwise. When a check fails, the work folder with every log is kept and its
path is printed.

### Add a hardware check

1. Add a block to `node/capture/tools/test_pi.sh` that runs the tool with flags
   from its own `--help`, writes its output to `"$work/<check>.log"`, and calls
   `record <check> PASS|FAIL "<detail>"`.
2. Add the check's name to the `checks=(...)` list, so the summary shows it as
   `NOT RUN` when the script stops early.
3. If it needs different wiring, add a wiring step with a `pinctrl` reading and
   a `confirm` question before it, and add the wiring to the safety banner.
   Only use loopback jumpers between Pi header pins.
4. Run `shellcheck node/capture/tools/test_pi.sh`. It must report nothing.
