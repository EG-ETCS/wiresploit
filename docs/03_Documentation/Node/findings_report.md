# Findings report

pi5la is an 8-channel logic analyzer on a Raspberry Pi 5, built on the PIO block
of the RP1 chip. This report collects what was measured, what it means for
capturing real protocols, and what is still open.

- **Hardware:** Raspberry Pi 5, 8 GB, Raspberry Pi OS Bookworm, kernel
  `6.18.50+rpt-rpi-2712`, RP1 firmware `23fca1cd…` (op count 33).
- **Channels:** GPIO 2–9 (physical pins 3, 5, 7, 29, 31, 26, 24, 21).
- **Dates:** all measurements are from 2026-09-16 unless dated otherwise. Items
  marked *unmeasured* are estimates.
- **One home for each figure:** every measurement is recorded once, in this
  report. Other pages link here rather than repeat numbers. Appendix A shows how
  each milestone was tested; appendix B gives the feature verification runs.

!!! danger "Hardware safety"
    Every test in this report used loopback jumpers between Pi header pins only.
    Pi 5 GPIO is **3.3 V and not 5 V tolerant**. Before connecting any external
    target, fit a **74LVC245 buffer with series resistors and a shared ground**
    ([capture guide, hardware safety](capture_guide.md#hardware-safety)).

---

## 1. Summary

| Question | Answer |
|---|---|
| Does it meet the spec (10 MSa/s, 8 ch, 0 drops)? | **Yes.** 0 dropped samples at 10.0 MB/s |
| Highest sustained rate, 8 channels | **50 MSa/s** (5 / 5 long runs clean) |
| Highest sustained rate, 4 / 2 / 1 channels | **100 / 200 / 200 MSa/s** (4 and 2 ch: 5 / 5 long runs clean; 1 ch: 1 long run, §5) |
| What limits the rate? | Data transfer: **50 MB/s** of packed samples is reliable (15 / 15 runs); 66.7 MB/s is not (9 / 20); 100 MB/s fails outright |
| Protocol decoding verified | UART 115200: 8000 bytes decoded exactly |
| Trigger | Works (level and edge, no pre-trigger) |
| Pi 4 as an alternative? | No: it has no PIO, so timing would come from software |

---

## 2. How capture works

1. A PIO state machine samples N pins on a hardware clock (200 MHz / divider),
   packs 32/N samples into each 32-bit word, and pushes words into its RX FIFO.
2. The kernel's **cyclic DMA** copies words into a ring of 4 periods (1 MiB each
   by default for streaming, 4 MiB when writing to disk) with no gaps between
   periods.
3. pi5la reads each completed period and writes a sigrok `.sr` file, or a byte
   stream with `--stream`.
4. Dropped samples are detected two ways: the PIO **RXSTALL** flag (read
   directly from the FDEBUG register) and ring **overflow** (`-EOVERFLOW`).
   Either makes pi5la exit with code 2.

---

## 3. Findings

### 3.1 Problems found and solved

| Problem | Cause | Fix | Result |
|---|---|---|---|
| piolib crashed: `Failed to get FIFO flags` | RP1 firmware has no `PIO_SM_GET_FLAGS` op | Read FDEBUG at `0xf0000008` via `READ_HW` / `WRITE_HW` | Stall flag reads and clears correctly |
| Samples lost at every buffer hand-over | piolib queues one-shot DMA receives; the gap between them overflows the 16-word FIFO (6.4 µs at 10 MSa/s) | Kernel cyclic DMA ring | Stalls 245 / 16 / 4 (64 KiB / 1 MiB / 4 MiB buffers) → **0** |
| Naive capture too slow | One firmware RPC over PCIe per word | DMA only for bulk capture | About 25× faster (naive versus DMA table, appendix A, M3) |
| Throughput report showed false losses | Final period left out of the byte count | Counting fixed | 10.000 MB/s reported correctly |
| A recording to the SD card overflowed the DMA ring | The card stalls in bursts, longer than a 4 × 1 MiB ring can absorb | 4 × 4 MiB ring by default when writing to disk | Clean; figures in §4, other limits |

### 3.2 Verification

| Test | Result |
|---|---|
| Toolchain (M0, 2026-09-17) | Stock `piopwm` example loads and runs on PIO without errors; output seen through the loopback |
| Generator (M1) | 1 Hz LED blink, `actual 1.000000 Hz` |
| Stall detection (M2) | `fdebugtest`: FDEBUG read `0x00000001` with SM 0 stalled, `0x00000000` after the clear |
| Bit map (M4) | Channel k at word bits k, k+8, k+16, k+24 on all 8 channels |
| Correctness (M4) | 100 kHz at 10 MSa/s, 16 M samples: 320 000 transitions, every run exactly 50 samples, other channels constant, 0 stalls |
| Trigger (M7) | `low` / `falling` / `rising` block on an idle-high line, `high` fires; rising edge lands just before sample 0 |
| UART (M6) | 115200 8N1 loopback, 8000 bytes, `cmp` → MATCH |
| Live listening (2026-09-19) | Decoded UART text as it arrived, 0 stalls (B2) |
| Auto-stop (2026-09-19) | `--duration 60s --silence-timeout 1s` (then named `--idle-stop`) ended a capture on its own 1 s after the signal stopped, with the generator and again with a real UART line, decoded byte-exact (B2) |
| Streaming to disk (2026-09-21) | 30 s at 2 MSa/s × 8 ch: RSS flat at 6016 kB (it would have been about 120 MB in RAM before), 0 stalls, exit 0 |
| Silence timeout (2026-09-21) | Ended itself 1 s after the line went quiet, decoded byte-exact; the wait wrote nothing (B1) |
| Monitor mode (2026-09-21) | Three UART bursts recorded as three files, idle skipped, each decoded correctly (B4) |
| Elasticsearch export (2026-09-21) | A 3-burst monitor session decoded to 3 documents, byte-exact, and the live cluster accepted them (B5) |

### 3.3 Rate ceiling

Short rate sweep (M5): 4 M samples per point, the generator locked to the
capture clock (25-sample half period on channel 0), integer dividers only
(200 MHz / {100, 40, 20, 10, 8, 5, 4, 2, 1}), cyclic DMA with a 4 × 1 MiB ring.

| Channels | Highest clean rate | Packed data | First failing rate |
|---|---|---|---|
| 1 | 200 MSa/s (maximum) | 25 MB/s | none |
| 2 | 200 MSa/s (maximum) | 50 MB/s | none |
| 4 | 100 MSa/s | 50 MB/s | 200 MSa/s (100 MB/s): 2 stalls, 55.9 % violations |
| 8 | 50 MSa/s | 50 MB/s | 100 MSa/s (100 MB/s): 4 stalls, 80.7 % violations |

Every channel count is clean at 50 MB/s and fails at 100 MB/s, so the DMA limit
lies between the two. At 8 channels, 4 M samples (4 MB) is smaller than the
ring, so the short runs alone don't prove sustained throughput. A long
confirmation followed, `ratesweep --channels 8 --dividers 5,4,3 --samples 200M`
(7.2 GiB RAM available):

| Rate | Samples | Duration | Packed data | Violations | Stalls |
|---|---|---|---|---|---|
| 40 MSa/s | 200 M | 5 s | 40 MB/s | 0 | 0 |
| 50 MSa/s | 200 M | 4 s | 50 MB/s | **0** | **0** |
| 66.7 MSa/s | 200 M | 3 s | 66.7 MB/s | 1 | 1 |

The 1, 2 and 4-channel ceilings were confirmed with long runs too (§5).

### 3.4 Why more channels means a lower rate

PIO can sample at 200 MSa/s whatever the channel count. The limit is moving the
data to Linux: each channel adds one bit per sample, packed 32/N samples per
word, through the FIFO, the DMA ring, PCIe and pi5la's reads.

```
packed bits per second = sample rate × channels
```

| Channels | Ceiling | Bits/s | Words/s | Confidence |
|---|---|---|---|---|
| 8 | 50 MSa/s | 400 Mbit/s | 12.5 M | 200 M samples, 0 drops |
| 4 | 100 MSa/s | 400 Mbit/s | 12.5 M | 400 M samples, 0 drops |
| 2 | 200 MSa/s | 400 Mbit/s | 12.5 M | 800 M samples, 0 drops |
| 1 | 200 MSa/s | 200 Mbit/s | 6.25 M | 1600 M samples, 0 drops; PIO clock limit |

Every ceiling lands on the same data rate: clean at 50 MB/s, failing between
50 and 67 MB/s. Capturing only the channels you need doubles the rate for each
halving. Which stage sets the limit (DMA refill, PCIe, or userspace reads) has
not been isolated; the drop counters only say that data was lost.

---

## 4. What it means for protocols

A decoder needs at least 4 samples per bit or clock period; 10 is comfortable.
So the usable clock is about rate / 10 (safe) to rate / 4.

| Protocol | Typical speed | Can pi5la capture it? |
|---|---|---|
| UART | up to about 3 Mbaud | Yes. 115200 needs only 1–2 MSa/s |
| I²C | 100 k / 400 k / 1 M | Yes, at 10 MSa/s |
| 1-Wire, WS2812 | ≤ 800 kHz | Yes |
| CAN / CAN-FD (logic side) | 1 / 5 Mbit | Yes |
| SWD / JTAG | usually ≤ 4 MHz | Yes |
| SPI | clock-dependent | About 5 MHz at 8 ch; about 10 MHz at 4 ch; 10–25 MHz marginal |
| Flash SPI 40/80 MHz, SDIO, USB, Ethernet, HDMI, DDR | too fast or differential | No |

### Other limits

- **Live decoding (*unmeasured*).** `uart-listen` feeds sigrok's Python
  decoders, which probably can't keep up with 50 MSa/s; hence the 2 MSa/s
  default. For fast signals, capture to a file and decode afterwards.
- **Recording length.** One byte per sample, written straight to disk, so length
  is bounded by the card, not RAM. The SD card in this Pi writes **14.4 MB/s**
  (300 MB, `dd conv=fsync`, 2026-09-21), but it stalls in bursts: at 2 MB/s a
  4 × 1 MiB ring (about 2 s of slack) overflowed after 7 s, while 4 MiB periods
  ran 30 s clean. File capture therefore uses a 4 × 4 MiB ring by default.
  Practical continuous ceiling: well under 14 MB/s, so about 10 MSa/s on
  8 channels; above that use `--monitor`, which writes only bursts. `.sr` files
  compress well: 60 M idle samples came to 59 kB.
- **DMA memory.** The CMA pool is 64 MB (`CmaTotal: 65536 kB`), so the largest
  DMA ring is 4 × 8 MiB (§5).
- **Electrical.** 3.3 V inputs only; anything else goes through a 74LVC245.
  Digital levels only; timing resolution is one sample (20 ns at 50 MSa/s).
  Loose jumpers distort signals above a few MHz. GPIO 2 and 3 (channels 0 and 1)
  have 1.8 kΩ on-board pull-ups, which can load a weak unbuffered signal.
- **Channels.** 8 by design.

### Pi 5 versus Pi 4

The Pi 4 has no RP1 and no PIO. Its GPIOs sit on the BCM2711 SoC, so sampling
would be a CPU loop reading a GPIO register. Linux interrupts and scheduling
pause that loop, so samples are unevenly spaced, gaps corrupt decoded bytes, and
drops are hard to detect. DMA-timed tools such as pigpio target kHz signals.

| | Pi 5 (RP1 PIO) | Pi 4 (software polling) |
|---|---|---|
| Sample timing | Hardware clock, exact | Loop timing, jittery |
| Dropped samples | Detected; 0 at 50 MSa/s × 8 ch | Likely, hard to detect |
| Practical rate | 50–200 MSa/s | kHz to low MSa/s (*unmeasured*) |

Beyond about 400 Mbit/s the next step is a dedicated FPGA and USB 3 analyzer
(DSLogic, Saleae), not another Pi.

---

## 5. Maximum-rate confirmation

The 1, 2 and 4-channel ceilings first came from 4 M-sample runs, which fit
inside the DMA ring and so don't prove sustained throughput. These long runs
match the 8-channel confirmation: about 200 MB of packed data at the ceiling.

**Result:** PASS (2026-09-17). Jumper pin 38 (GPIO 20) → pin 3 (GPIO 2), wiring
checked with the 1 Hz generator and `pinctrl lev` first. The generator was
locked to the capture clock with a 25-sample half period, so every run on
channel 0 must be exactly 25 samples; any lost sample would break a run.

| Channels | Rate | Samples | Duration¹ | Packed data | Runs | Run min/max | Violations | Stalls | Status |
|---|---|---|---|---|---|---|---|---|---|
| 4 | 100 MSa/s | 400 M | 4 s | 50 MB/s | 15 999 999 | 25 / 25 | **0** | **0** | ok |
| 2 | 200 MSa/s | 800 M | 4 s | 50 MB/s | 31 999 999 | 25 / 25 | **0** | **0** | ok |
| 1 | 200 MSa/s | 1600 M | 8 s | 25 MB/s | 63 999 999 | 25 / 25 | **0** | **0** | ok |

¹ Samples ÷ rate; ratesweep does not print elapsed time.

Commands, run from `<install_dir>/bin` (the CSVs `long-4ch.csv`, `long-2ch.csv`
and `long-1ch.csv` stayed on the Pi):

```bash
./ratesweep --channels 4 --dividers 2 --samples 400M --out long-4ch.csv
./ratesweep --channels 2 --dividers 1 --samples 800M --out long-2ch.csv
./ratesweep --channels 1 --dividers 1 --samples 1600M --out long-1ch.csv
```

All four channel counts are confirmed sustained at their ceilings: 50 MB/s of
packed data runs clean for 4–8 s with every sample accounted for.

### Pushing past 50 MB/s (2026-09-17)

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

**Conclusion:** 50 MB/s of packed data is reliable (15 / 15 clean runs, 3 GB of
samples); 66.7 MB/s is not (9 / 20); 100 MB/s fails outright. The edge lies
between 50 and 66.7 MB/s; integer dividers can't test in between. The decision
that followed is in [decisions](decisions.md).

---

## 6. Still open

- PulseView visual check of `uart.sr` (M6).
- An external target: an ESP32 UART at 115200 through a 74LVC245 buffer with a
  shared GND.
- Live per-burst shipping (`--es-url`) and the flat `psf` document shape on the
  Pi. On the Pi, against a stub cluster first: a document must arrive about a
  second after each line, the `.sr` must disappear once confirmed,
  `psf.first_edge_time_ns` must match `date +%s.%N` at send time, and stalls
  must stay at zero. Then `--protocol raw` (the square-wave generator works,
  since nothing decodes it), then the outage retry, then the real cluster. Also
  check `chronyc tracking`, and that `psf.clock_synchronised` is true.
- `ratesweep` `status=nosignal` rows: that code path has not been seen on
  hardware (every run so far was `status=ok`).
- The 100 kHz `sqwave` generator path has not been checked with a scope (M1).
- Not started, by agreement: M8 (RLE, pre-trigger buffer).

---

## Appendix A. Milestone tests

How each milestone from the [original brief](spec.md) was tested. Each one
needed its acceptance test to pass on the Pi before the next. Results already
given in §3 are referenced rather than repeated.

### M0: toolchain bring-up

Accept when a stock piolib example runs without error.

**Result:** PASS (2026-09-17). The stock example was run on GPIO 20 instead of
GPIO 4, so the existing loopback jumper (pin 38 → pin 3) could show its output
on GPIO 2:

```bash
timeout 14 ./utils/build/piolib/examples/piopwm 20 & sleep 1; pinctrl get 20;   for i in $(seq 1 24); do pinctrl lev 2,20; sleep 0.5; done; wait %1; echo "exit=$?"
```

- It printed `Loaded program at 25, using sm 0, gpio 20`, with no errors.
- `pinctrl get 20` showed `a7 … // GPIO20 = PIO20`: the pin was handed to PIO.
- GPIO 2 matched GPIO 20 in all 24 readings. Readings were all low for the first
  6 s or so, then increasingly high as the PWM duty cycle ramped up.
- Exit status 124, from `timeout` stopping the endless example as intended.

### M1: test signal generator

```bash
pi5la --generator 1 --generator-only          # LED + resistor on GPIO 20: 1 Hz blink
pi5la --generator 100k --generator-only       # check with a scope or frequency meter
```

Verified without using the capture path. **Result:** PASS (2026-09-16), §3.2.
`--generator 1` reported `actual 1.000000 Hz (sqwave_slow)`.

### M2: naive capture baseline

First confirm that stall detection works (no wiring needed). It must print
`PASS`:

```bash
fdebugtest
```

Then:

```bash
pi5la --mode naive --rate 10M --samples 400000 --raw naive.bin
```

Read the `throughput` line; the brief expected about 250 kB/s. RXSTALL will be
set, which is expected for this mode.

**Result:** PASS (2026-09-16): **396.3 kB/s** (396 295 samples/s). The state
machine stalled, as expected.

### M3: DMA capture

```bash
pi5la --rate 10M --samples 16000000 --raw dma.bin --out dma.sr --generator 100k
```

Accept when there are no transfer errors (sample correctness is M4).

**Result:** PASS (2026-09-16, cyclic DMA, 4 × 1 MiB ring): 16 000 000 samples in
1.679 s, **0 stall events**, exit 0, `.sr` written. The same capture with
`bitprobe --verify` showed exactly 320 000 transitions and 0 violations on
channel 0, so no samples were lost. The run printed 9.5 MB/s and "782549 lost
samples", but those numbers came from a reporting bug (the final period was left
out of the byte count), fixed afterwards (§3.1). After the fix, a
10 000 000-sample capture during M6 measured **10.000 MB/s**, 0 stalls, with an
estimated loss of 178 samples (within wall-clock jitter).

The first attempt used piolib one-shot read-ahead (1 MiB buffers). It ran at
10.006 MB/s but set RXSTALL after all 16 buffers, and 27 run violations were
seen.

**Naive versus DMA:**

| Mode | Rate | Throughput | Stall events |
|---|---|---|---|
| naive (M2) | 10 MSa/s | 396.3 kB/s | stalled (checked once at the end) |
| dma, piolib read-ahead | 10 MSa/s | 10 006 kB/s | 16 / 16 buffers |
| dma, cyclic (M3) | 10 MSa/s | 10 000 kB/s | 0 |

### M4: correctness and bit order

1. `bitprobe --map` prompts you to move the GPIO 20 jumper through GPIO 2..9 one
   at a time. For each channel it prints the word bits that toggle and their
   chronological order, and at the end a `map_8ch` table.
2. If the table differs from `src/unpack.c`, paste it there and update the
   matching check in the unpack unit test.
3. Acceptance: `bitprobe --verify 0 --generator 100k --rate 10M` shows channel 0
   with every run exactly 50 samples and every other channel with no
   transitions.

Offline analysis of a `--raw` file: `bitprobe --raw-in dma.bin --channels 8`.

**Result:** PASS (2026-09-16), §3.2.

- **Map:** `bitprobe --probe` was run with the generator jumpered to each of
  GPIO 2..9 in turn, checking with `pinctrl lev` before each capture. The
  measured layout (slot 0 = low byte, channel k = bit k) was the one already in
  `src/unpack.c`, so no table change was needed.
- **Verify:** `bitprobe --verify 0 --generator 100k --rate 10M --samples 16M`.
- **Wiring tip:** check with
  `for i in 1 2 3 4 5 6; do pinctrl lev 2-9,20; sleep 0.5; done`, while running
  `pi5la --generator 1 --generator-only`, before every probe. An empty probe
  ("0 active bits") meant the jumper wasn't on a capture pin. `bitprobe --map`
  is interactive, and a jumper that hasn't actually moved makes it report
  channel 0 for every step.

### M5: rate sweep

```bash
ratesweep --out ratesweep.csv            # jumper GPIO 20 -> GPIO 2
```

Columns: `channels,sample_rate,samples_captured,violations,violation_pct,stalls,status`.
`status` is `ok`, `violations` (any violation, stall or ring overflow) or
`nosignal` (no runs; `violation_pct` is then empty). The 2026-09-16 CSVs predate
the `stalls` and `status` columns.

**Result:** PASS (2026-09-16). Measured ceiling at 8 channels: 50 MSa/s
sustained. The sweep and confirmation tables are in §3.3 and §5. The CSVs
(`ratesweep.csv` for the full sweep, `ratesweep-long.csv` for the 8-channel
confirmation) were generated on the Pi.

A first sweep had the jumper on the wrong pin. Every row then read
`violations=0, violation_pct=100` (no runs found = no signal).

### M6: sigrok output

The brief: capture a real 115200-baud UART through the buffer, open it in
PulseView on the Pi, add the UART decoder, and compare the decoded bytes with
what was sent.

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
- **Capture:** 10 M samples at 10.000 MB/s, 0 stalls, exit 0. Channel 0 had
  60 922 transitions.
- **Decode:**
  `sigrok-cli -i uart.sr -P uart:rx=GPIO2:baudrate=115200 -B uart=rx > decoded.txt`,
  then `cmp sent.txt decoded.txt` reported **MATCH**. sigrok read the metadata
  correctly (10 MHz, 8 channels GPIO2–GPIO9, 10 000 000 samples).
- **Still open:** the PulseView visual check on the Pi, and a test with an
  external target (ESP32) through the 74LVC245 front end.
- **Troubleshooting:** after a reboot, GPIO 2 has no function and its input is
  disabled, so `pinctrl lev 2` prints `-` until pi5la has run once.

### M7: hardware trigger

```bash
pi5la --rate 10M --samples 1000000 --trigger rising:0 --out trig.sr
```

On an idle line this should block (Ctrl-C to abort) and start only when the
condition occurs. There is no pre-trigger buffer: the first captured sample is
the first one after the trigger.

**Result:** PASS (2026-09-16), loopback GPIO 20 → GPIO 2.

- **Blocking and condition checks,** with the jumper removed so GPIO 2 sat high
  on its pull-up, each run limited to 5 s by `timeout`:
    - `low:0`, `falling:0` and `rising:0` blocked.
    - `high:0` fired immediately.
    - `rp1sm 0` showed the state machine parked on `instr 00002020`
      (`wait 0 pin 0`).
- **Edge trigger:** with the jumper on and the line idle high
  (`pinctrl lev 2,20` = `1 1`), `--trigger rising:0 --samples 1000000` waited
  30.1 s until `--generator 1k --generator-only` started, then completed with
  0 stalls. The first sample was high, and the first change came at
  sample 4999: one full 5000-sample half period, so the rising edge falls just
  before sample 0.
- **Wiring caveat:** GPIO 20 is undriven when no generator is running. An
  earlier `rising:0` attempt fired straight away because the line wasn't at a
  known level when the trigger was armed. Check `pinctrl lev` before arming a
  loopback trigger test.

### M8: RLE and pre-trigger buffer

Not implemented; skipped by agreement.

---

## Appendix B. Feature verification runs

### B1. Silence timeout

**2026-09-21**, UART jumper pin 8 → pin 3, with the text sent 3 s in:
`pi5la --rate 2M --out capture.sr --silence-timeout 1s` ended itself at
4.194 s, exit 0, and sigrok decoded `hello silence`. Of 8 388 608 samples
captured, only 2 415 412 were written: the waiting cost no disk.

### B2. Live UART listening and auto-stop

**2026-09-19**, UART loopback jumper pin 8 → pin 3:

```bash
( sleep 3; printf 'hello live\r\n' > /dev/ttyAMA0 ) & timeout 8 uart-listen
```

printed `hello live` as it arrived, streaming 15 728 640 samples in 7.865 s with
0 stall events.

The silence timeout was confirmed on the same wiring (as `--idle-stop`, its name
then): `pi5la --rate 2M --duration 60s --silence-timeout 1s --out uart_idle.sr`
ended by itself 4.196 s in, exit 0, and sigrok decoded the line byte-exact.

### B3. Unbounded capture

**2026-09-21:** 30 s at 2 MSa/s on 8 channels, streamed to disk, held memory
flat (§3.2).

### B4. Monitor mode

**2026-09-21.** Three UART lines were sent 3 s apart during a 15 s monitor run
at 2 MSa/s on 8 channels:

```bash
pi5la --rate 2M --monitor <install_dir>/captures/mon1 --gap 100ms --pre 10ms --post 50ms
```

```
burst 1: 121552 samples (0.061 s)     gap_before_s 2.99
burst 2: 121548 samples (0.061 s)     gap_before_s 2.95
burst 3: 121548 samples (0.061 s)     gap_before_s 2.95
monitor: 3 burst(s), 1.09% of the time recorded
```

The folder came to **20 kB**, against about 33 MB for the same 15 s recorded
continuously, and `burst-0002.sr` decoded to `burst 2`.

### B5. Elasticsearch export

**2026-09-21.** A three-burst monitor session decoded to three documents,
36 bytes in total, with the older `psd` document shape:

```
payload_hex 68656c6c6f20657320310d0a   psf.text "hello es 1\r\n"
timestamps 2.008 s apart, psf.burst and gap_before_s carried through
```

The three documents were accepted by the team's live cluster (basic auth taken
from the environment). The index already held 437 network documents and one
UART document, so the document shape was matched to theirs.

**Live shipping and the flat `psf` shape have not been run on the Pi yet.** They
are covered by the host tests, and the whole chain (a burst closing, pi5la
spawning the shipper, the document reaching a stub `_bulk` endpoint, the files
being cleaned up, and an outage being retried) was exercised against a stub
cluster on a development machine. The Pi test plan is in §6.
