# Node

A Wiresploit Node is the device attached to the Device Under Test (DUT). It has
two functions:

- **Capture** records the DUT's onboard bus traffic. Today this is **pi5la**,
  which turns a Raspberry Pi 5 into an 8-channel logic analyzer using the PIO
  block inside the Pi's RP1 chip. It captures GPIO 2–9, writes sigrok `.sr`
  files that open in PulseView, can listen to a UART live, records only the
  bursts of traffic on a bus, and sends decoded messages to Elasticsearch.
- **Actions** performs configured actions on the DUT, such as signal generation
  or injection. It is not started yet.

!!! danger "Hardware safety: read before connecting anything"
    Pi 5 GPIO is **3.3 V and not 5 V tolerant**. Overvoltage destroys RP1, which
    is soldered to the board, so the Pi is lost. Any external target must go
    through a **74LVC245 buffer with series resistors and a shared ground**. The
    only safe bare-wire setup is a loopback jumper between Pi header pins. Never
    connect pin 1 (3.3 V) or pins 2 and 4 (5 V) to a GPIO. Full details:
    [capture guide, hardware safety](capture_guide.md#hardware-safety).

## Pages

| Page | What it covers |
|---|---|
| [Capture guide](capture_guide.md) | Build, install on a Pi, usage, Elasticsearch export, the capture test page, design notes |
| [Monitor mode](monitor_mode.md) | `--monitor`: recording only the bursts on a bus, in full |
| [Cheatsheet](cheatsheet.md) | Day-to-day commands |
| [Findings report](findings_report.md) | Every measurement: rate ceilings, verification runs, limits, milestone tests |
| [Decisions](decisions.md) | Technical decisions and why they were made |
| [Original brief](spec.md) | The spec pi5la was built from, kept as written |

For agents and developers, `node/AGENTS.md` in the repo has the build commands,
the critical capture-path constraints and the file map.

## Results at a glance

- **The spec target is met:** 10 MSa/s on 8 channels with zero dropped samples,
  and a byte-exact UART decode.
- **The sustained ceiling is 50 MB/s of packed samples:** 50 MSa/s on
  8 channels, 100 MSa/s on 4, and 200 MSa/s (the PIO clock limit) on 1 or 2.
- The figures, and how each was measured, are in the
  [findings report](findings_report.md).

### Milestones

| Milestone | Status |
|---|---|
| M0 Toolchain | Done: builds; the stock `piopwm` example runs without errors |
| M1 Test signal generator | Done: 1 Hz LED blink confirmed |
| M2 Naive capture baseline | Done: measured, about 25× slower than DMA |
| M3 DMA capture | Done: 0 drops at 10 MSa/s with cyclic DMA |
| M4 Bit order and correctness | Done: all 8 channels verified |
| M5 Rate sweep | Done: ceilings at 1, 2, 4 and 8 channels, all confirmed with long runs |
| M6 sigrok output and UART | Decode is byte-exact. The PulseView visual check and an external target are still open |
| M7 Hardware trigger | Done |
| M8 RLE and pre-trigger buffer | Not started (skipped by agreement) |
| Live UART listening | Done: text decoded as it arrives, 0 stalls |
| `--silence-timeout` | Done: the capture stops by itself once the line goes quiet |
| Unbounded capture and `--monitor` | Done: memory stays flat; monitor writes only bursts |
| Elasticsearch export | Done on the Pi with the earlier document shape. Live per-burst shipping and the flat `psf` shape are tested on a host only |

## What was built, and the problems solved

1. **The whole project, M0–M7.** C11 with CMake; piolib comes from
   `raspberrypi/utils`. pioasm lives in pico-sdk (not in utils, as the spec
   assumed), so it is built from there. `.sr` files are written with libzip, and
   the logic has host unit tests.
2. **The firmware lacked the FIFO flag call.** piolib crashed with
   `Failed to get FIFO flags`, because the Pi's RP1 firmware (op count 33) has
   no `PIO_SM_GET_FLAGS` operation, and a bootloader update didn't add it. Fix:
   read the PIO **FDEBUG register** directly through the kernel's `READ_HW` and
   `WRITE_HW` ioctls. Verified on hardware with `fdebugtest`.
3. **Samples were lost at every DMA buffer hand-over.** piolib's queued DMA
   receives left a gap each time one buffer finished and the next started. Fix:
   the kernel's **cyclic DMA** mode, where the hardware loops over one ring with
   no gaps. Stalls went to 0.
4. **The bit layout** of the packed 32-bit words was measured on all
   8 channels: the oldest sample is in the low byte, and channel k is bit k.
5. **The ceiling was measured** with `ratesweep`, then confirmed with long
   runs at every channel count.
6. **The trigger was verified:** `low`, `falling` and `rising` block, `high`
   fires, and the edge lands at sample 0.
7. **UART decoding was verified:** the Pi's own UART (GPIO 14) was looped into
   GPIO 2, and sigrok decoded 8000 bytes exactly.
8. **Live listening, unbounded capture, `--silence-timeout`, `--monitor` and the
   Elasticsearch export** were added and verified on the Pi.
9. **Fixed along the way:** a throughput and loss reporting bug, two error
   messages that printed a literal `<NL>`, line endings (`.gitattributes`), and
   CMake 3.16 compatibility.
10. **The Node was restructured** for the team: split by role (`capture/`,
    `exporter/`, `actions/`), with every per-machine setting in one `node.conf`,
    a dev container that cross-compiles the Pi binary, and scripts to test,
    build, deploy and set up a Pi. See [decisions](decisions.md).

## Pin map

Channel k is GPIO `2 + k`. The full table, with sigrok channel names, is in the
[capture guide](capture_guide.md#channels).

| Role | GPIO | Physical pin |
|---|---|---|
| Channels 0–7 | 2–9 | 3, 5, 7, 29, 31, 26, 24, 21 |
| Test square-wave generator (output) | 20 | 38 |
| Pi UART TX (test source, `/dev/ttyAMA0`) | 14 | 8 |
| GND | — | 6, 9, 14, 20, 25, 30, 34, 39 |
| 3.3 V and 5 V: never connect to a GPIO | — | 1 (3.3 V), 2 and 4 (5 V) |

Pin 1 is the square solder pad. Odd pins are on the inner row and even pins on
the outer row.

**Idle levels:** GPIO 2–8 read high (pull-ups) and GPIO 9 reads low. After a
reboot, `pinctrl lev 2` shows `-` until pi5la has run once.

## Still to do

- [ ] PulseView visual check of a UART capture (M6).
- [ ] An external target (an ESP32 UART at 115200) through a 74LVC245 buffer
      with a shared GND.
- [ ] Live per-burst shipping (`--es-url`) on the Pi, then against the real
      cluster.
- [ ] The full hardware test list (`<install_dir>/bin/test_pi.sh`) on a Pi after the
      restructure.
- [ ] The Actions function.
- [ ] Optional: M8 (RLE, pre-trigger buffer), or a PulseView live driver.
