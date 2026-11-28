# Decisions

Technical decisions for the Node, and why they were made. Each entry gives the
decision, the reason, and the date when it is known.

**Add an entry in the same pull request** as any change that makes or reverses a
decision (the update rule in the root `AGENTS.md`). Don't edit an old entry to
say something new: add a new one that replaces it, and mark the old one
"Replaced by D-NN".

---

## Capture path

### D-01. Capture uses the kernel's cyclic DMA

- **Decision:** capture with the kernel's cyclic DMA (`PIO_IOC_SM_CONFIG_XFER_V2`
  with the `DMA_CYCLE` flag, reads through `PIO_IOC_SM_XFER_DATA32`), not
  piolib's `pio_sm_xfer_data` read-ahead. The ring is 4 periods; `--dma-buf`
  sets the period size (default 1 MiB for streaming). `-EOVERFLOW` from a read
  means dropped samples.
- **Why:** read-ahead lost samples at every buffer hand-over (stalls equalled the
  buffer count). The cyclic ring is gapless in hardware.
- **Date:** 2026-09-16 (M3).

### D-02. Stall detection reads the FDEBUG register

- **Decision:** read FDEBUG at `0xf0000008` through `READ_HW` / `WRITE_HW`
  (RXSTALL is bit `sm`; write 1 to clear), on pi5la's own `/dev/pio0` handle.
- **Why:** piolib's `pio_sm_get_flags` panics on this RP1 firmware, which has no
  `PIO_SM_GET_FLAGS` op (op count 33). A bootloader update did not add it.
- **Date:** 2026-09-16 (confirmed with `fdebugtest`).

### D-03. PIO capture packs 32/N samples per word, with a measured bit map

- **Decision:** `in pins, N`, `shift_right = true`, autopush threshold 32, FIFO
  joined to RX. Slot 0 is in the low bits; channel k is at bit `s*N + k`.
  Trigger programs and channel counts other than 8 are built at run time
  (`progbuild.c`) and checked against the pioasm output of `la8.pio` and
  `la8_trig.pio`.
- **Why:** packing uses the full PCIe bandwidth; the map was measured on hardware
  (M4), not derived; the self-check keeps the runtime encoder and the `.pio`
  files in step.
- **Date:** 2026-09-16 (M4).

### D-04. Two generator programs

- **Decision:** `sqwave.pio` above about 1.53 kHz, `sqwave_slow.pio` (a delay
  loop) below.
- **Why:** `sqwave` cannot divide the 200 MHz PIO clock down far enough for the
  1 Hz LED check.
- **Date:** 2026-09-16 (M1).

### D-05. 50 MB/s of packed data is the documented limit

- **Decision:** document 50 MB/s (50 MSa/s at 8 channels, 100 at 4, 200 at 1–2)
  as the sustained limit, and use integer dividers only.
- **Why:** 50 MB/s was clean in 15 / 15 runs, 66.7 MB/s in only 9 / 20.
  Fractional-divider tests between the two would gain at most about 30 %, with
  uneven sample spacing, close to the drop edge.
- **Date:** 2026-09-17.

### D-06. M8 (RLE and pre-trigger buffer) is skipped

- **Decision:** don't implement M8.
- **Why:** skipped by agreement. It is a stretch goal in the brief, outside its
  definition of done.
- **Date:** not recorded.

## Recording

### D-07. Live mode streams one byte per sample to stdout

- **Decision:** `--stream` writes one byte per sample to stdout; `uart-listen`
  pipes it into `sigrok-cli -I binary:numchannels=8:samplerate=<Hz> -i -`. In
  that input, channels are named `0`–`7`, not `GPIO2`.
- **Why:** reuses sigrok's decoders unchanged, with no file in between.
- **Date:** 2026-09-19.

### D-08. Recording streams to disk

- **Decision:** samples are appended to `<out>.logic` and zipped into the `.sr`
  at the end (`src/srwriter.c`, `srzip_write_file()`), so a capture needs no
  length and RAM stays flat. `capture_run()`'s in-RAM path is kept only for
  `--raw` and `--mode naive` (M2, M4 and the tools).
- **Why:** captures of unknown length, bounded by disk rather than RAM.
- **Date:** 2026-09-21.

### D-09. File capture uses a 4 × 4 MiB DMA ring

- **Decision:** writing to disk defaults to 4 MiB periods
  (`CAPTURE_DMA_BUF_RECORD`); `--stream` and the tools keep 4 × 1 MiB.
- **Why:** the SD card stalls in bursts, and a 4 × 1 MiB ring overflowed after
  7 s at only 2 MB/s.
- **Date:** 2026-09-21.

### D-10. `--silence-timeout` replaces `--idle-stop`

- **Decision:** the auto-stop option is `--silence-timeout T`; `--idle-stop` is
  kept as an alias that prints a note. It scans each DMA period for a word that
  isn't the idle level repeated (`src/idle.c`), waits for the first change
  keeping `--pre` of lead-in, then stops after T without one.
- **Why:** the new name says what it waits for. In monitor mode it ends the whole
  session, so since 2026-09-22 pi5la says so when it fires.
- **Date:** 2026-09-21.

### D-11. `--monitor DIR` records only bursts

- **Decision:** a burst state machine (`src/burst.c`) with I/O in `src/record.c`
  writes `burst-NNNN.sr` plus `index.json` with `start_time`, `duration` and
  `gap_before_s`. Idle is never written; trailing idle is removed with
  `ftruncate()`. Detection lags by up to one DMA period.
- **Why:** watching a bus for hours without writing gigabytes of idle.
- **Date:** 2026-09-21.

### D-12. A burst is timestamped at its first edge

- **Decision:** a burst is stamped when its first edge is seen, not when it is
  decoded. `capture.c` reports `start_realtime_ns` (at `pio_sm_set_enabled`, so
  sample 0 is not skewed by setup) and `period_realtime_ns` (as each period
  lands); `record.c` pins each period's last sample to the latter. Clock
  discipline is read with `adjtimex(2)` and recorded per burst. All wall-clock
  times are integer nanoseconds.
- **Why:** `t0 + n/rate` would drift with the PIO crystal and not follow NTP; a
  double is only about 240 ns precise at epoch scale.
- **Date:** 2026-09-21.

## Export

### D-13. Elasticsearch export with `sr2es.py`, standard library only

- **Decision:** `sr2es.py` decodes a capture with sigrok-cli, groups bytes into
  messages by idle gap, and writes bulk NDJSON for the `wiresploit-poc1` index,
  uploading when `--es-url` is given. Wall-clock time comes from the per-burst
  sidecar written by `record.c`. Credentials only through `ES_API_KEY`, or
  `ES_USER` and `ES_PASSWORD`.
- **Why:** no third-party packages on the Pi; the NDJSON written first means a
  failed upload loses nothing; credentials never reach an argv.
- **Date:** 2026-09-21.

### D-14. Live shipping: one shipper, disk is the queue

- **Decision:** `--es-url` makes `record.c` spawn `sr2es --ship DIR` as each burst
  closes (`src/shipper.c`, `posix_spawn`, its own session), never waited for
  until teardown. One shipper at a time. A confirmed upload deletes the `.sr` and
  `.ndjson` and marks the sidecar `shipped`; `--protocol raw` keeps its `.sr`.
- **Why:** decoding or uploading inside the capture path would overrun the
  four-period DMA ring. With disk state as the queue, a burst that closes while
  the shipper is busy, a failed decode and a cluster that was down all resolve
  the same way: the next sweep finds it.
- **Date:** 2026-09-21. Not yet run on the Pi.

### D-15. Protocols are declared, never guessed

- **Decision:** `--protocol uart|raw` is set before the run and written into
  every sidecar, and the sidecar wins over the shipper's flag. `raw` skips
  decoding and ships one document per burst (arrival time, duration, edge count,
  active channels). I2C and SPI become a `PROTOCOLS` entry in `sr2es.py` when
  there is hardware to verify them on.
- **Why:** the capture path stays protocol-blind, and an unknown bus still
  reaches the index.
- **Date:** 2026-09-21.

### D-16. Document shape: flat `psf`

- **Decision:** a document's `psf` is flat: a common core plus the declared
  protocol's fields, no `psd` and no sub-objects. An import-time assert stops a
  protocol field from shadowing a core field.
- **Why:** one shape for every protocol, easy to query. The first documents
  matched what was already in `wiresploit-poc1` (`event_type` `onboard`,
  uppercase `protocol`, a flat `psd` with capture provenance under
  `psd.capture`); the flat `psf` replaced that. `psf.cb_id` appears in the
  network documents already in that index; its meaning is not known yet.
- **Date:** after 2026-09-21. Not yet run on the Pi.

### D-17. Exit codes

- **Decision:** `0` OK, `1` error, `2` dropped samples (RXSTALL or ring
  overflow). The wall-clock loss estimate is informational only.
- **Why:** a script can tell "ran but lost data" from "failed"; the estimate
  includes scheduling jitter, so it must not fail a run.
- **Date:** 2026-09-16.

## Node structure and tooling

### D-18. The Node is split by role

- **Decision:** `node/capture/` (the pi5la C core, its tools and the test page),
  `node/exporter/` (`sr2es.py`), `node/actions/` (placeholder), plus `node/lib/`
  and `node/scripts/`.
- **Why:** matches the architecture's terms (a Node with Capture and Actions
  functions). Rejected: a minimal move, which would have left the exporter
  inside "capture"; and a split by language, which doesn't match the
  architecture.
- **Date:** 2026-11-28.

### D-19. The Flask page is a local Pi testing tool only

- **Decision:** the page lives in `node/capture/tools/web_ui/` and is not the
  product UI.
- **Why:** real capture commands will come from Core. Building product features
  on the test page would duplicate Core's web application.
- **Date:** 2026-11-28.

### D-20. Every per-machine setting is in `node.conf`

- **Decision:** no `~`, home-folder or other machine-specific path anywhere. Programs
  find the Node root by walking up from their own location to
  `node.conf.example`; settings live in `<root>/node.conf` as plain `KEY=value`
  lines (gitignored). Precedence: flag > environment variable > `node.conf` >
  built-in default.
- **Why:** the old defaults (a build folder and a captures folder in one
  person's home) only worked for that person. The same layout now works in the
  repo, the container and any Pi, wherever it is copied.
- **Date:** 2026-11-28.

### D-21. Cross-compile in an x86 Debian Bookworm container

- **Decision:** build and test in a dev container based on `debian:bookworm`
  with arm64 multiarch libraries matching Pi OS Bookworm. pioasm is built
  natively (it runs at build time); piolib is cross-built and linked statically,
  so the Pi only needs `libzip4`. The raspberrypi/utils and pico-sdk commits are
  pinned with `ARG`s. The Pi receives finished binaries.
- **Why:** any teammate can build, test and ship to a Pi from a laptop, with
  repeatable builds. Rejected: QEMU arm64 emulation (about 10× slower to build);
  building natively on each Pi.
- **Date:** 2026-11-28.

### D-22. `setup_pi.sh` installs and configures; it doesn't build

- **Decision:** `setup_pi.sh` installs the Pi's packages after asking, checks
  `/dev/pio0` and the `gpio` group, asks for each setting in `whiptail` dialogs
  (the `raspi-config` style), and writes `node.conf`. `--config <file>` skips the
  dialogs.
- **Why:** a familiar, guided setup for people who are not the original author,
  that is safe to run again.
- **Date:** 2026-11-28.

### D-23. Shared agent context is in `AGENTS.md` files only

- **Decision:** the repo has `AGENTS.md` files (root and per component) and no
  `CLAUDE.md`. Personal notes go in the gitignored `AGENTS.local.md`; a Claude
  Code user can import `@AGENTS.md` from a gitignored `CLAUDE.local.md`. The
  Node's docs moved from `node/agent/` to this site, deduplicated.
- **Why:** the team uses several AI tools, so one shared source is read by all
  of them. The old files mixed agent instructions, team standards and pi5la docs,
  and held one person's addresses and paths.
- **Date:** 2026-11-28.

### D-24. One feature branch, task branches merged with `--no-ff`

- **Decision:** each task gets its own branch cut from the feature branch
  (`feature/node/BR-MON-01-capture-pipeline`) and is merged back with `--no-ff`
  after its tests pass. The team opens one pull request from the feature branch
  at the end.
- **Why:** each task stays reviewable on its own, and the team reviews the whole
  feature once.
- **Date:** 2026-11-28.

### D-25. Git history is not rewritten

- **Decision:** don't rewrite history to remove the personal addresses that were
  committed in the old `node/agent/` files.
- **Why:** they are private-range or VPN addresses, of no use outside the
  team's network; rewriting shared history would break everyone's clones. They
  are removed from the current tree.
- **Date:** 2026-11-28.
