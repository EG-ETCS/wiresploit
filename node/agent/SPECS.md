# Raspberry Pi 5 — 8-channel PIO logic analyzer

Implementation spec for a coding agent.

---

## 1. Goal

Build a command-line 8-channel logic analyzer that runs natively on a Raspberry Pi 5, capturing on GPIO 2–9 using the PIO block inside the RP1 southbridge. Output must open in PulseView for protocol decoding.

Target buses: UART (up to 1 Mbaud), I2C (up to 400 kHz), SPI (up to 5 MHz). Required sample rate is therefore modest — **10 MSa/s sustained with zero dropped samples is the success bar.** Do not sacrifice correctness to chase a higher number.

---

## 2. Environment

- Raspberry Pi 5, Raspberry Pi OS Bookworm 64-bit
- `piolib` from `https://github.com/raspberrypi/utils` (subdirectory `piolib`)
- Kernel driver `rp1-pio`, character device `/dev/pio0`
- `pioasm` (built from the same repo) to assemble `.pio` → C header
- C11, CMake ≥ 3.16

Check `/dev/pio0` permissions early. If it requires root, document that rather than silently running everything under `sudo`.

---

## 3. Critical constraints — read before writing code

These are the things that will silently ruin the implementation.

**3.1 — Never poll the FIFO word-by-word for bulk data.**
PIOLib is not a register interface. Each call sends a message over PCIe to firmware running on RP1, which performs the operation and replies. Round-trip latency is at least 10 µs. A `while (1) { buf[i++] = pio_sm_get_blocking(...); }` loop yields roughly 250 kB/s and will look like a hardware limitation. It is not. All bulk capture must go through piolib's DMA transfer API.

**3.2 — Pack 4 samples per FIFO word.**
The PIO FIFO is 32 bits wide. At 8 channels, configure autopush with a push threshold of 32 so each word carries 4 samples. Failing to do this wastes 75% of your already-limited PCIe bandwidth.

**3.3 — Verify the piolib API from the headers, not from memory.**
Read `piolib/include/piolib.h` and `piolib/pio_rp1.c` in the cloned repo before writing the DMA path. The bulk transfer functions are named along the lines of `pio_sm_config_xfer()` and `pio_sm_xfer_data()`, but confirm the exact signatures, direction constants, and buffer-count semantics from the source. piolib is a subset of the Pico SDK, not a drop-in equivalent — do not assume Pico SDK behaviour carries over.

**3.4 — Capture pins must be consecutive.**
`in pins, 8` reads 8 pins upward from the state machine's configured input base. GPIO 2–9 is the intended block. There is no pin remapping.

**3.5 — Bit order within the packed word is a real hazard.**
The `in` instruction shifts into the ISR, and the shift direction determines where each sample lands in the 32-bit word. Do not guess this. Milestone 4 exists specifically to determine it empirically.

**3.6 — Hardware safety note for the README.**
Pi 5 GPIO is 3.3 V and is not 5 V tolerant. Damage destroys RP1, which is not a replaceable part. The README must state that a buffered front end (74LVC245 or equivalent, with series resistors) is required before connecting to any external target, and that loopback testing with jumper wires between Pi 5 pins is the only safe bare-wire configuration.

---

## 4. Repository layout

```
pi5la/
  CMakeLists.txt
  README.md
  src/
    main.c            CLI entry point
    capture.c/.h      PIO setup, DMA capture loop
    generator.c/.h    test signal generator (second state machine)
    srzip.c/.h        sigrok .sr file writer
    unpack.c/.h       packed-word to per-sample byte conversion
  pio/
    la8.pio           capture program
    la8_trig.pio      capture with hardware trigger
    sqwave.pio        test signal generator
  tools/
    ratesweep.c       benchmark harness (Milestone 5)
  tests/
```

---

## 5. PIO programs

Keep these minimal. Complexity here costs sample rate.

**Capture (`la8.pio`)** — one instruction, one sample per clock:

```
.program la8
.wrap_target
    in pins, 8
.wrap
```

Configure: input base = GPIO 2, autopush enabled, push threshold 32, 8 pins as input.
Sample rate = PIO clock ÷ clock divider.

**Capture with trigger (`la8_trig.pio`)** — blocks until the trigger pin asserts, then samples continuously:

```
.program la8_trig
    wait 1 pin 0
.wrap_target
    in pins, 8
.wrap
```

The `wait` pin index is relative to the input base, so pin 0 here means GPIO 2.

**Generator (`sqwave.pio`)** — square wave at PIO clock ÷ 2, for self-test:

```
.program sqwave
.wrap_target
    set pins, 1
    set pins, 0
.wrap
```

---

## 6. Milestones

Each milestone has a binary acceptance test. Do not proceed to the next until the current one passes.

### M0 — Toolchain bring-up

Clone `raspberrypi/utils`, build `piolib` and `pioasm`, confirm the `rp1-pio` driver is loaded and `/dev/pio0` exists. Build and run one of the stock piolib examples unmodified.

**Accept when:** a stock example loads a PIO program and runs a state machine without error. Record the exact build commands in the README.

### M1 — Test signal generator

Implement `generator.c`: load `sqwave.pio` on one state machine, output on GPIO 20, frequency configurable via clock divider.

**Accept when:** the frequency is confirmed correct. Verify by a method that does not depend on the capture path — measure with an external scope or meter if available, or by toggling slowly enough (1 Hz) to observe on an LED.

### M2 — Naive capture baseline

Deliberately implement the wrong approach: a `pio_sm_get_blocking()` loop. Measure and record sustained throughput.

**Accept when:** throughput is measured and documented. It is expected to land near 250 kB/s. This exists to establish the baseline and prove the bottleneck is software, not silicon. Keep it in the repo as `--mode=naive` for comparison.

### M3 — DMA capture

Implement the real capture path using piolib's bulk transfer API, with multiple buffers rotating so the state machine is never starved while a buffer is being drained.

**Accept when:** 16 MB is captured at 10 MSa/s and written to disk with no transfer errors reported. Sample correctness is not yet verified — that is M4.

### M4 — Correctness and bit-order verification

This milestone determines empirically what M3 actually captured.

1. Jumper GPIO 20 (generator) to GPIO 2 only. Leave GPIO 3–9 pulled low. Capture. Confirm activity appears in exactly one bit position and record which.
2. Repeat for GPIO 3, 4, … 9 individually. Build the full channel-to-bit-position map.
3. Determine sample ordering within each 32-bit word: which of the 4 packed samples is chronologically first.
4. Implement `unpack.c` using the mapping discovered, converting packed words to one byte per sample with channel N in bit N.

**Accept when:** the generator outputs a known square wave at 100 kHz, capture runs at 10 MSa/s, and the unpacked data shows exactly 50 samples high followed by 50 samples low, repeating, on the expected channel and no other.

### M5 — Rate sweep harness

Build `tools/ratesweep`. For each channel count in {1, 2, 4, 8} and a sweep of sample rates, run a fixed-length capture of a known square wave and count period violations (any run of identical samples whose length deviates from expected).

Output CSV: `channels, sample_rate, samples_captured, violations, violation_pct`.

**Accept when:** the CSV is produced and identifies the highest clean rate for 8 channels. Expect somewhere in the 20–40 MSa/s range, but report the measured value rather than assuming.

### M6 — sigrok output

Implement `srzip.c` to write a `.sr` file. The format is a zip archive containing three members:

- `version` — a file containing the single character `2`
- `metadata` — INI format. Must include a `[global]` section and a `[device 1]` section specifying `capturefile=logic-1`, `total probes=8`, `samplerate=` (with unit suffix, e.g. `10 MHz`), `unitsize=1`, and `probe1=` through `probe8=` naming each channel.
- `logic-1` — raw packed samples, one byte per sample, channel N in bit N

**Accept when:** a capture of a real 115200-baud UART line opens in PulseView on the Pi 5, the UART decoder is applied, and the decoded bytes match what was transmitted.

### M7 — Hardware trigger

Wire `la8_trig.pio` into the capture path. CLI support for `--trigger rising:N` / `falling:N` / `high:N` / `low:N`.

**Accept when:** capture on an idle line blocks indefinitely and begins only when the trigger condition occurs, with the trigger edge present near the start of the captured data.

### M8 — Stretch: RLE and pre-trigger buffer

Only after M0–M7 pass.

- RLE encoding to extend capture duration on idle-heavy buses like UART
- Ring buffer retaining N samples before the trigger point

---

## 7. CLI specification

```
pi5la --rate 10M --samples 1000000 --out capture.sr
      [--channels 8] [--base-gpio 2]
      [--trigger rising:0] [--mode dma|naive]
      [--generator 100k]
```

- `--rate` accepts suffixes k / M
- `--generator` enables the self-test generator on GPIO 20 at the given frequency, for loopback testing without external hardware
- Exit non-zero on any dropped-sample condition, and report the count on stderr

---

## 8. Definition of done

- `pi5la --rate 10M --samples 10000000 --out uart.sr` completes with zero dropped samples
- The resulting file opens in PulseView and decodes a real 115200-baud UART stream correctly
- `ratesweep` has produced a CSV documenting the measured ceiling at 1, 2, 4 and 8 channels
- README documents build steps, the 3.3 V hardware warning, the required buffer circuit, and the naive-versus-DMA throughput comparison from M2 and M3

---

## 9. Prior art worth reading

There is an existing libsigrok fork by `cleverca22` on GitHub containing a `src/hardware/pi5-rp1-pio` driver — an initial attempt at exactly this. Read it before implementing M3 and M6. It may solve the DMA configuration problem already, or reveal a dead end worth avoiding.

There is also an open issue on the `raspberrypi/utils` repository discussing RP1 PIO DMA throughput. Check whether it has been resolved, since a fix would raise the ceiling reported in M5.