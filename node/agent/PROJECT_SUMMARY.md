# pi5la: project summary

pi5la turns a **Raspberry Pi 5** into an **8-channel logic analyzer** using the PIO
block inside the Pi's RP1 chip. It captures GPIO 2–9, writes sigrok `.sr` files that
open in PulseView, and can listen to a UART live.

- **Spec:** `SPECS.md`
- **Detailed build steps and measurements:** `README.md`
- **Findings report:** `REPORT.md`

---

## 1. Results at a glance

| What | Result |
|---|---|
| Goal from the spec | 10 MSa/s, 8 channels, zero dropped samples |
| **Achieved at 10 MSa/s** | **0 dropped samples**, 10.0 MB/s, byte-exact UART decode |
| **Sustained ceiling, 8 channels** | **50 MSa/s**, confirmed over 200 M samples (4 s) |
| Sustained ceiling, 4 channels | 100 MSa/s, confirmed over 400 M samples |
| Sustained ceiling, 1 and 2 channels | 200 MSa/s, the hardware maximum, confirmed over 1600 M / 800 M samples |
| Naive baseline (one read per word) | 396 kB/s, about 25× slower than DMA |
| Hardware trigger | Works: blocks on an idle line, starts at the edge |
| UART decode (115200 baud) | 8000 bytes decoded exactly (`cmp` → MATCH) |

### Milestones

| Milestone | Status |
|---|---|
| M0 Toolchain | ✅ builds; stock `piopwm` example runs without errors (2026-09-17) |
| M1 Test signal generator | ✅ 1 Hz LED blink confirmed |
| M2 Naive capture baseline | ✅ 396 kB/s |
| M3 DMA capture | ✅ 0 drops at 10 MSa/s (cyclic DMA) |
| M4 Bit order / correctness | ✅ all 8 channels verified |
| M5 Rate sweep | ✅ 50 / 100 / 200 / 200 MSa/s at 8 / 4 / 2 / 1 channels, all long-run confirmed |
| M6 sigrok output / UART | ✅ decode is byte-exact. The PulseView visual check and the ESP32 test are still open |
| M7 Hardware trigger | ✅ |
| M8 RLE / pre-trigger | Not started (skipped by agreement) |
| Extra: live UART listening | ✅ verified on the Pi (2026-09-19): text decoded live, 0 stalls |
| Extra: `--silence-timeout` | ✅ verified on the Pi (2026-09-19, then named `--idle-stop`): capture stops 1 s after the signal ends |
| Extra: unbounded capture + `--monitor` | ✅ verified on the Pi (2026-09-21): memory flat, monitor recorded 1.09% of the time |
| Extra: Elasticsearch export (`tools/sr2es.py`) | ✅ end to end on the Pi (2026-09-21): 3 bursts → 3 documents, byte-exact, accepted by the live cluster |

---

## 2. What was done, and the problems solved along the way

1. **Scaffolded the whole project**, M0–M7. It's C11 with CMake, and piolib
   comes from `raspberrypi/utils`. Because pioasm lives in pico-sdk (not in
   utils, as the spec assumed), it's built from there. `.sr` files are written
   with libzip, and there are host unit tests.
2. **The firmware lacked the FIFO flag call.** piolib crashed with
   `Failed to get FIFO flags`, because the Pi's RP1 firmware (op count 33) has
   no `PIO_SM_GET_FLAGS` operation, and a bootloader update didn't add it.
   - **Fix:** read the PIO **FDEBUG register** directly through the kernel's
     `READ_HW` / `WRITE_HW` ioctls.
   - Verified on hardware with `fdebugtest`.
3. **Samples were lost at every DMA buffer hand-over.** piolib's queued DMA
   receives left a gap each time one buffer finished and the next started:
   245, 16 and 4 stalls for 64 KiB, 1 MiB and 4 MiB buffers.
   - **Fix:** the kernel's **cyclic DMA** mode, where the hardware loops over
     one ring with no gaps. This took stalls to **0**.
4. **Verified the bit layout** of the packed 32-bit words on all 8 channels:
   the oldest sample is in the low byte, and channel k is bit k.
5. **Measured the ceiling** with `ratesweep`, then confirmed 50 MSa/s at
   8 channels with a long run.
6. **Verified the trigger:** `low`, `falling` and `rising` block, `high`
   fires, and the edge lands at sample 0.
7. **Verified UART decoding:** the Pi's own UART (GPIO 14) was looped into
   GPIO 2, and sigrok decoded 8000 bytes exactly.
8. **Added live listening:** `pi5la --stream` plus `uart-listen.sh`, which
   decodes UART continuously until Ctrl-C.
9. **Fixed along the way:**
   - A throughput/loss reporting bug.
   - Two error messages that printed a literal `<NL>`.
   - Line endings: added `.gitattributes`.
   - CMake 3.16 compatibility.

---

## 3. Hardware and safety

- **Pi 5 GPIO is 3.3 V and NOT 5 V tolerant.** Damaging RP1 effectively kills the
  board.
- **Any external device** (ESP32, target board) must go through a **74LVC245 buffer
  with series resistors**, plus a shared GND.
- **Jumper wires between Pi pins (loopback)** are the only safe bare-wire setup.
- **Never** connect anything to pins 1 (3.3 V), 2 or 4 (5 V).

### Pin map

| Role | GPIO | Physical pin |
|---|---|---|
| Analyzer channel 0 | GPIO 2 | 3 |
| Analyzer channel 1 | GPIO 3 | 5 |
| Analyzer channel 2 | GPIO 4 | 7 |
| Analyzer channel 3 | GPIO 5 | 29 |
| Analyzer channel 4 | GPIO 6 | 31 |
| Analyzer channel 5 | GPIO 7 | 26 |
| Analyzer channel 6 | GPIO 8 | 24 |
| Analyzer channel 7 | GPIO 9 | 21 |
| Test square-wave generator (output) | GPIO 20 | 38 |
| Pi UART TX (test source, `/dev/ttyAMA0`) | GPIO 14 | 8 |
| GND | — | 6, 9, 14, 20, 25, 30, 34, 39 |

Pin 1 is the square solder pad. Odd pins are on the inner row and even pins on
the outer row.

**Idle levels:** GPIO 2–8 read high (pull-ups) and GPIO 9 reads low. After a
reboot, `pinctrl lev 2` shows `-` until pi5la has run once.

---

## 4. Building (on the Pi)

These commands were used on Raspberry Pi OS Bookworm, kernel 6.18.50.

```bash
sudo apt install git build-essential cmake pkg-config libzip-dev \
                 device-tree-compiler libncurses-dev libfdt-dev libgnutls28-dev \
                 sigrok-cli pulseview

# piolib
git clone https://github.com/raspberrypi/utils
cmake -S utils -B utils/build && cmake --build utils/build -j4 && sudo cmake --install utils/build

# pioasm
git clone --depth 1 https://github.com/raspberrypi/pico-sdk
cmake -S pico-sdk/tools/pioasm -B build-pioasm -DPIOASM_VERSION_STRING=2.2.0
cmake --build build-pioasm -j4

# pi5la
cmake -S pi5la -B build-pi5la -DPIOASM_EXECUTABLE=$HOME/build-pioasm/pioasm
cmake --build build-pi5la -j4
(cd build-pi5la && ctest)
```

**Requirements:**
- **Kernel 6.18 or newer**, for cyclic DMA.
- **Access to `/dev/pio0`:** your user must be in the `gpio` group. No `sudo`
  is needed.
- **For the UART test:** `dtparam=uart0=on` in `/boot/firmware/config.txt`,
  then a reboot.

---

## 5. How to use it

All programs are in `~/build-pi5la/`.

### Capture to a file (opens in PulseView)

```bash
pi5la --rate 10M --out capture.sr        # until Ctrl-C; samples stream to disk
pulseview capture.sr
```

| Option | Meaning |
|---|---|
| `--rate` | Sample rate, e.g. `2M`, `10M`, `50M` (max 200M) |
| `--silence-timeout 1s` | Wait for traffic, then stop once the line is quiet |
| `--monitor DIR` | Record only bursts of traffic: one `.sr` each, plus `index.json` |
| `--duration` / `--samples` | Optional caps; without them the capture runs until stopped |
| `--max-size 4G` | Stop once this much sample data is written |
| `--out` / `--raw` | sigrok `.sr` file / raw packed words (`--raw` needs a cap) |
| `--channels 1\|2\|4\|8` | Fewer channels allow higher rates |
| `--trigger rising:0` | Wait for `rising`, `falling`, `high` or `low` on channel N |
| `--generator 100k` | Test square wave on GPIO 20 during capture |
| `--generator 1 --generator-only` | Run only the generator (e.g. blink an LED) until Ctrl-C |
| `--stream` | Capture until Ctrl-C and write samples to stdout |

**Exit codes:** `0` OK, `1` error, `2` dropped samples detected.

### Send a capture to Elasticsearch

```bash
sr2es.py capture.sr --baud 115200                     # writes capture.sr.ndjson
sr2es.py ~/captures/mon-test --es-url https://es:9200 # and uploads it
```

One document per decoded message into `wiresploit-poc1`: `timestamp`
(date_nanos), `event_type`, `protocol`, `duration` (ns), `length_bytes`,
`payload_hex`, and `psf` with the UART details. Credentials come from
`ES_API_KEY`, or `ES_USER` / `ES_PASSWORD`.

### Decode a capture on the command line

```bash
sigrok-cli -i capture.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx
```

### Listen to UART live (until Ctrl-C)

```bash
~/build-pi5la/uart-listen.sh                  # text, 115200 baud, channel 0 (GPIO 2)
~/build-pi5la/uart-listen.sh --hex            # one line per byte: sample number + hex
~/build-pi5la/uart-listen.sh --baud 9600 --channel 3 --rate 500k
```

If it reports a DMA ring overflow, lower `--rate`. About 16–20× the baud rate
is plenty.

### Choosing a sample rate

A decoder needs at least about 4 samples per bit; 10 or more is comfortable.

| Signal | Suggested rate |
|---|---|
| UART 115200 | 2M |
| UART 1 Mbaud | 10M+ |
| I²C 400 kHz | 4M–10M |
| SPI 5 MHz | 50M |

### Self-tests (loopback jumpers only)

```bash
# Channel check: jumper pin 38 → pin 3
bitprobe --verify 0 --generator 100k --rate 10M --samples 16M

# Rate sweep: same jumper
ratesweep --out ratesweep.csv

# Stall-flag check: no wiring
fdebugtest

# UART loopback: jumper pin 8 → pin 3
stty -F /dev/ttyAMA0 115200 raw -echo
printf 'hello\r\n' > /dev/ttyAMA0
```

**Before any loopback test**, confirm the jumper actually connects with a
1 Hz generator:

```bash
pi5la --generator 1 --generator-only &
for i in 1 2 3 4 5 6; do pinctrl lev 2-9,20; sleep 0.5; done
kill %1
```

The column for the jumpered channel must alternate together with the last
column (GPIO 20).

---

## 6. Code layout

```
src/main.c          CLI: capture, generator, --stream
src/capture.c/.h    PIO setup, cyclic DMA capture, streaming, stall checks
src/dmacyclic.c/.h  kernel cyclic-DMA ioctls
src/fdebug.c/.h     FDEBUG (RXSTALL) register access via READ_HW/WRITE_HW
src/rp1_ioctl.h     rp1-pio ioctl definitions
src/generator.c/.h  test square wave (sqwave / sqwave_slow)
src/progbuild.c/.h  runtime PIO programs (channel count, trigger)
src/unpack.c/.h     packed words → 1 byte per sample (verified bit map)
src/srzip.c/.h      sigrok .sr writer (libzip)
src/analyze.c/.h    run-length / violation checks
src/util.c/.h       rate parsing, clock dividers
pio/                la8.pio, la8_trig.pio, sqwave.pio, sqwave_slow.pio
tools/              ratesweep.c, bitprobe.c, fdebugtest.c, uart-listen.sh
tests/              host unit tests (ctest)
```

---

## 7. Still to do

- [x] Run the live `uart-listen.sh` on the Pi (2026-09-19): decoded `hello live` as it arrived.
- [x] `--duration` and `--silence-timeout` (2026-09-19): capture ends itself when the line goes quiet.
- [x] Unbounded capture and `--monitor` (2026-09-21): 30 s with flat memory; 3 bursts captured, idle skipped.
- [ ] Confirm the PulseView visual decode of `uart.sr` (M6).
- [x] Confirm the stock `piopwm` example runs cleanly (M0, 2026-09-17, on GPIO 20).
- [ ] ESP32 UART test **through a 74LVC245 buffer** with a shared GND.
- [x] Long-run confirmation of the 1/2/4-channel ceilings (2026-09-17).
- [ ] Optional: M8 (RLE, pre-trigger buffer), or a PulseView live driver.
