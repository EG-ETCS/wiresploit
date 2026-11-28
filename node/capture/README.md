# Capture: pi5la

The Node's Capture function. pi5la is an 8-channel logic analyzer for the
Raspberry Pi 5: it samples GPIO 2–9 with a PIO state machine in the Pi's RP1
chip, moves the samples to RAM by cyclic DMA, and writes sigrok `.sr` files that
open in PulseView. It sustains 10 MSa/s on 8 channels with zero dropped samples,
and up to 50 MB/s of packed data (50 MSa/s at 8 channels).

> **Hardware safety.** Pi 5 GPIO is **3.3 V and not 5 V tolerant**.
> Overvoltage destroys RP1, which is soldered to the board, so the Pi is lost.
> Connect an external target only through a **74LVC245 buffer with series
> resistors (100–330 Ω) and a shared ground**. The only safe bare-wire setup is
> a loopback jumper between Pi header pins, for example GPIO 20 (pin 38) →
> GPIO 2 (pin 3). Never connect pin 1 (3.3 V) or pins 2 and 4 (5 V) to a GPIO.

## Quick start

From the repo root, in the dev container:

```bash
node/scripts/test.sh            # unit and integration tests
node/scripts/build_pi.sh        # cross-compile into node/capture/build-arm64/
node/scripts/deploy_to_pi.sh    # copy the install folder to the Pi
```

Then on the Pi: `<install_dir>/scripts/setup_pi.sh`, and
`<install_dir>/bin/pi5la --rate 2M --out capture.sr`.

## What is here

| Path | What |
|---|---|
| `src/` | The C11 core: capture, DMA, recording, output |
| `pio/` | PIO programs: capture, triggered capture, test generator |
| `tests/` | Unit and integration tests |
| `tools/` | `ratesweep`, `bitprobe`, `fdebugtest`, `uart-listen.sh`, `test_pi.sh` |
| `tools/web_ui/` | A Flask page for testing captures by hand on a Pi. Local testing only, not the product UI |
| `cmake/` | The aarch64 cross-compile toolchain file |

## Documentation

- `node/AGENTS.md`: build and test, critical capture-path constraints,
  conventions, file map. Read it before changing the capture path.
- `docs/03_Documentation/Node/` on the docs site:
  - `capture_guide.md`: build, install, usage, Elasticsearch export.
  - `monitor_mode.md`: recording only the bursts on a bus.
  - `cheatsheet.md`: day-to-day commands.
  - `findings_report.md`: every measurement.
  - `decisions.md`: why it is built this way.
  - `spec.md`: the original brief.
