# pi5la: findings report

An 8-channel logic analyzer on a Raspberry Pi 5, built on the PIO block of the
RP1 chip. This report collects what was measured, what it means for capturing
real protocols, and what is still being tested.

- **Hardware:** Raspberry Pi 5, 8 GB, Raspberry Pi OS Bookworm, kernel
  `6.18.50+rpt-rpi-2712`, RP1 firmware `23fca1cd…` (op count 33).
- **Channels:** GPIO 2–9 (physical pins 3, 5, 7, 29, 31, 26, 24, 21).
- **Details and raw measurements:** `README.md` (milestones M0–M7, all verified).

All measurements are from 2026-09-16 unless dated otherwise. Items marked
*unmeasured* are estimates.

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

1. A PIO state machine samples N pins on a hardware clock
   (200 MHz / divider), packs 32/N samples into each 32-bit word, and pushes
   words into its RX FIFO.
2. The kernel's **cyclic DMA** copies words into a ring of 4 periods
   (1 MiB each by default) with no gaps between periods.
3. pi5la reads each completed period and writes a sigrok `.sr` file, or a
   byte stream with `--stream`.
4. Dropped samples are detected two ways: the PIO **RXSTALL** flag (read
   directly from the FDEBUG register) and ring **overflow** (`-EOVERFLOW`).
   Either makes pi5la exit with code 2.

---

## 3. Findings

### 3.1 Problems found and solved

| Problem | Cause | Fix | Result |
|---|---|---|---|
| piolib crashed: `Failed to get FIFO flags` | RP1 firmware has no `PIO_SM_GET_FLAGS` op | Read FDEBUG at `0xf0000008` via `READ_HW`/`WRITE_HW` | Stall flag reads and clears correctly |
| Samples lost at every buffer hand-over | piolib queues one-shot DMA receives; the gap between them overflows the 16-word FIFO (6.4 µs at 10 MSa/s) | Kernel cyclic DMA ring | Stalls 245 / 16 / 4 (64 KiB / 1 MiB / 4 MiB) → **0** |
| Naive capture too slow | One firmware RPC over PCIe per word | DMA only for bulk capture | 396 kB/s → 10 000 kB/s (≈25×) |
| Throughput report showed false losses | Final period left out of the byte count | Counting fixed | 10.000 MB/s reported correctly |

### 3.2 Verification

| Test | Result |
|---|---|
| Toolchain (M0) | Stock `piopwm` example loads and runs on PIO without errors; output seen through the loopback |
| Generator (M1) | 1 Hz LED blink, `actual 1.000000 Hz` |
| Bit map (M4) | Channel k at word bits k, k+8, k+16, k+24 on all 8 channels |
| Correctness (M4) | 100 kHz at 10 MSa/s, 16 M samples: 320 000 transitions, every run exactly 50 samples, other channels constant |
| Trigger (M7) | `low`/`falling`/`rising` block on an idle-high line, `high` fires; rising edge lands just before sample 0 |
| UART (M6) | 115200 8N1 loopback, 8000 bytes, `cmp` → MATCH |
| Auto-stop (2026-09-19) | `--duration 60s --silence-timeout 1s` (then named `--idle-stop`) ended a capture on its own 1 s after the signal stopped, with the generator and again with a real 115200 UART line (decoded byte-exact, 0 stalls, exit 0) |
| Streaming to disk (2026-09-21) | 30 s at 2 MSa/s x 8 ch: RSS flat at 6016 kB (it would have been ~120 MB in RAM before), 0 stalls, exit 0 |
| Silence timeout (2026-09-21) | Ended itself 1 s after the line went quiet (4.194 s total), decoded byte-exact; the wait wrote nothing: 8.4 M samples captured, 2.4 M written |
| Monitor mode (2026-09-21) | 15 s with three UART bursts 3 s apart: 3 files of 0.061 s each, `gap_before_s` ~2.95 s, **1.09% of the time recorded** (20 kB on disk vs ~33 MB continuous), each burst decoded correctly |
| Elasticsearch export (2026-09-21) | A 3-burst monitor session decoded to 3 documents, 36 bytes: `payload_hex` `68656c6c6f20657320310d0a` = `hello es 1\r\n`, timestamps 2.008 s apart, `psf.burst` and `gap_before_s` carried through |
| _bulk upload (2026-09-21) | 3 documents accepted by the live cluster (http://100.95.111.97:9200, basic auth from the environment). The index already held 437 network documents and one UART document, so the shape was matched to theirs |
| Live listening (2026-09-19) | `uart-listen.sh` decoded UART text as it arrived: 15.7 M samples streamed at 2 MSa/s, 0 stalls |

### 3.3 Rate ceiling

Short rate sweep (4 M samples per point) with the generator locked to the
capture clock (25-sample half period on channel 0), integer dividers only,
4 × 1 MiB ring.

| Channels | Highest clean rate | Packed data | First failing rate |
|---|---|---|---|
| 1 | 200 MSa/s (maximum) | 25 MB/s | none |
| 2 | 200 MSa/s (maximum) | 50 MB/s | none |
| 4 | 100 MSa/s | 50 MB/s | 200 MSa/s (100 MB/s) |
| 8 | 50 MSa/s | 50 MB/s | 100 MSa/s (100 MB/s) |

Long confirmation at 8 channels, 200 M samples each:

| Rate | Packed data | Violations | Stalls |
|---|---|---|---|
| 40 MSa/s | 40 MB/s | 0 | 0 |
| 50 MSa/s | 50 MB/s | **0** | **0** |
| 66.7 MSa/s | 66.7 MB/s | 1 | 1 |

### 3.4 Why more channels means a lower rate

PIO can sample at 200 MSa/s whatever the channel count. The limit is moving
the data to Linux, and each channel adds one bit per sample:

```
packed bits per second = sample rate × channels
```

| Channels | Ceiling | Bits/s | Words/s |
|---|---|---|---|
| 8 | 50 MSa/s | 400 Mbit/s | 12.5 M |
| 4 | 100 MSa/s | 400 Mbit/s | 12.5 M |
| 2 | 200 MSa/s | 400 Mbit/s | 12.5 M |
| 1 | 200 MSa/s | 200 Mbit/s | 6.25 M (PIO clock limit) |

Every ceiling lands on the same data rate: clean at 50 MB/s, failing between
50 and 67 MB/s. Capturing only the channels you need doubles the rate for each
halving. Which stage sets the limit (DMA refill, PCIe, or userspace reads) has
not been isolated. The 1, 2 and 4-channel ceilings were confirmed with long
runs (§5).

---

## 4. What it means for protocols

A decoder needs at least 4 samples per bit or clock period; 10 is comfortable.
So the usable clock is about rate / 10 (safe) to rate / 4.

| Protocol | Typical speed | Can pi5la capture it? |
|---|---|---|
| UART | up to ~3 Mbaud | Yes. 115200 needs only 1–2 MSa/s |
| I²C | 100 k / 400 k / 1 M | Yes, at 10 MSa/s |
| 1-Wire, WS2812 | ≤ 800 kHz | Yes |
| CAN / CAN-FD (logic side) | 1 / 5 Mbit | Yes |
| SWD / JTAG | usually ≤ 4 MHz | Yes |
| SPI | clock-dependent | ~5 MHz at 8 ch; ~10 MHz at 4 ch; 10–25 MHz marginal |
| Flash SPI 40/80 MHz, SDIO, USB, Ethernet, HDMI, DDR | too fast or differential | No |

**Other limits**

- **Live decoding (*unmeasured*).** `uart-listen` feeds sigrok's Python
  decoders, which probably can't keep up with 50 MSa/s. For fast signals,
  capture to a file and decode afterwards.
- **Recording length.** One byte per sample, written straight to disk, so
  length is bounded by the card, not RAM. The SD card in this Pi writes
  **14.4 MB/s** (300 MB, `dd conv=fsync`, 2026-09-21), but it stalls in bursts:
  at 2 MB/s a 4 x 1 MiB ring (~2 s of slack) overflowed after 7 s, while
  4 MiB periods ran 30 s clean. File capture therefore uses a 4 x 4 MiB ring by
  default. Practical continuous ceiling: well under 14 MB/s, so about 10 MSa/s
  on 8 channels; above that use `--monitor`, which writes only bursts.
  `.sr` files compress well: 60 M idle samples came to 59 kB.
- **Electrical.** 3.3 V inputs only; anything else goes through a 74LVC245.
  Digital levels only; timing resolution is one sample (20 ns at 50 MSa/s).
  Loose jumpers distort signals above a few MHz. GPIO 2 and 3 have 1.8 kΩ
  on-board pull-ups.
- **Channels.** 8 by design.

### Pi 5 versus Pi 4

The Pi 4 has no RP1 and no PIO. Sampling would be a CPU loop reading a GPIO
register, and Linux interrupts and scheduling pause that loop.

| | Pi 5 (RP1 PIO) | Pi 4 (software polling) |
|---|---|---|
| Sample timing | hardware clock, exact | loop timing, jittery |
| Dropped samples | detected; 0 at 50 MSa/s × 8 ch | likely, hard to detect |
| Practical rate | 50–200 MSa/s | kHz to low MSa/s (*unmeasured*) |

Beyond ~400 Mbit/s the next step is a dedicated FPGA + USB 3 analyzer, not
another Pi.

---

## 5. Maximum-rate confirmation

The 1, 2 and 4-channel ceilings first came from 4 M-sample runs, which fit
inside the DMA ring and so don't prove sustained throughput. These long runs
match the 8-channel confirmation: about 200 MB of packed data at the ceiling.

**Result:** PASS (2026-09-17). Jumper pin 38 (GPIO 20) → pin 3 (GPIO 2),
wiring checked with the 1 Hz generator and `pinctrl lev` first. The generator
was locked to the capture clock with a 25-sample half period, so every run on
channel 0 must be exactly 25 samples; any lost sample would break a run.

| Channels | Rate | Samples | Duration¹ | Packed data | Runs | Run min/max | Violations | Stalls | Status |
|---|---|---|---|---|---|---|---|---|---|
| 4 | 100 MSa/s | 400 M | 4 s | 50 MB/s | 15 999 999 | 25 / 25 | **0** | **0** | ok |
| 2 | 200 MSa/s | 800 M | 4 s | 50 MB/s | 31 999 999 | 25 / 25 | **0** | **0** | ok |
| 1 | 200 MSa/s | 1600 M | 8 s | 25 MB/s | 63 999 999 | 25 / 25 | **0** | **0** | ok |

¹ Samples ÷ rate; ratesweep does not print elapsed time.

Commands (in `~/build-pi5la`; CSVs `long-4ch.csv`, `long-2ch.csv`,
`long-1ch.csv` on the Pi):

```bash
./ratesweep --channels 4 --dividers 2 --samples 400M --out long-4ch.csv
./ratesweep --channels 2 --dividers 1 --samples 800M --out long-2ch.csv
./ratesweep --channels 1 --dividers 1 --samples 1600M --out long-1ch.csv
```

All four channel counts are now confirmed sustained at their ceilings: 50 MB/s
of packed data runs clean for 4–8 s with every sample accounted for.

---|---|---|---|---|---|---|
| 4 | 100 MSa/s | 400 M | 50 MB/s | pending | pending | pending |
| 2 | 200 MSa/s | 800 M | 50 MB/s | pending | pending | pending |
| 1 | 200 MSa/s | 1600 M | 25 MB/s | pending | pending | pending |

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

**Conclusion:** 50 MB/s of packed data is reliable (15 / 15 clean runs,
3 GB of samples); 66.7 MB/s is not (9 / 20); 100 MB/s fails outright. The
edge lies between 50 and 66.7 MB/s; integer dividers can't test in between.

**Decision (2026-09-17):** 50 MB/s is the documented limit. Fractional-divider
tests between 50 and 66.7 MB/s were not pursued: they would gain at most ~30 %
with uneven sample spacing, close to the drop edge.

---

## 6. Still open

- PulseView visual check of `uart.sr`.
- ESP32 UART at 115200 through a 74LVC245 buffer with a shared GND.
- Not started by agreement: M8 (RLE, pre-trigger buffer).
