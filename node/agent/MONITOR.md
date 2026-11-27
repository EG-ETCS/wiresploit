# Monitor mode (`--monitor`) — detailed reference

Monitor mode records **only the bursts of activity on a bus and throws idle
away**, writing one self-describing `.sr` file per burst plus a directory index.
It is meant for watching a bus for minutes to hours without filling the disk
with unchanging bytes.

> Sampling never stops in monitor mode — every DMA period is captured and
> scanned. What changes is only *what gets written to disk*: idle is discarded,
> bursts are kept with a lead-in and a tail. Nothing on the wire is missed.

Source of truth: `src/record.c` (orchestration), `src/burst.c` (the burst state
machine), `src/idle.c` (activity detection), `src/main.c` (flags/defaults).

---

## 1. Why it exists

A continuous capture writes 1 byte per sample regardless of traffic. Eight
channels at 2 MSa/s is **7.2 GB/hour of nothing** on an idle bus. Monitor mode
writes only the active windows: in the verified 15 s test below, three UART
lines came to **20 kB on disk versus ~33 MB** for the same run captured
continuously — and each burst file still decodes exactly like a normal capture,
because every file is internally continuous.

Use monitor mode when you want to leave a probe running and capture whatever
transactions happen, each on its own timeline, without babysitting it.

---

## 2. The burst state machine

Implemented in `src/burst.c`. Positions are absolute sample numbers since the
capture began.

```
   waiting ──first change──▶ recording ──gap samples with no change──▶ end
      ▲                                                                 │
      └─────────────────────────────────────────────────────────────── ┘
        (idle discarded)         (burst kept, ends `post` after last change)
```

- **waiting** — the bus is quiet; nothing is written. A pre-roll ring in RAM
  keeps the most recent `pre` samples so the file can begin *before* the traffic.
- **change seen** — a burst opens `pre` samples **before** the first changed
  sample (`first_edge`). The pre-roll comes out of the ring, so the lead-in is
  real captured data, not padding.
- **recording** — samples are appended as they arrive. Any change refreshes
  `last_change`.
- **end** — once `gap` samples pass with **no** change, the burst closes,
  keeping up to `post` samples after the last change. If the capture is stopped
  (Ctrl-C) while a burst is open, it is flushed and closed cleanly
  (`burst_flush`).

A single transaction that pauses **longer than `gap`** splits into two burst
files. If one logical message is being cut in half, raise `--gap`.

---

## 3. Flags

```
pi5la --rate RATE --monitor DIR [--gap T] [--pre T] [--post T] [--dma-buf BYTES]
      [--channels N] [--base-gpio G] [--silence-timeout T]
      [--es-url URL [--es-index IDX] [--protocol P] [--decode-channel N]
                    [--baud N] [--uart-type S] [--port S] [--insecure] [--sr2es PATH]]
```

| Flag | Default | Meaning |
|---|---|---|
| `--monitor DIR` | — | Enable monitor mode; write bursts into `DIR` (created if needed). Mutually exclusive with `--out`. |
| `--gap T` | `100ms` | Silence that **ends** a burst. Longer = fewer, larger files; a bus that idles mid-transaction needs this raised. |
| `--pre T` | `10ms` | Lead-in kept **before** the first change (comes from the pre-roll ring). |
| `--post T` | `50ms` | Tail kept **after** the last change. |
| `--dma-buf BYTES` | `4 MiB` (disk streaming) | Cyclic-DMA period size; ring of **4** periods. Governs how soon a burst can appear (§6). Lower it in monitor mode for lower latency. |
| `--channels N` | `8` | 1, 2, 4 or 8. Every captured channel counts toward "idle": tie unused buffer inputs to GND or they may never go quiet. |
| `--base-gpio G` | `2` | First capture GPIO; pins are consecutive (`base+k` = channel `k`). |
| `--silence-timeout T` | off | Wait for the first change, then **stop the whole session** once quiet this long. Rarely what monitoring wants — leave it off to keep recording. Must be ≥ `--gap` (checked at startup). |
| `--rate RATE` | — (required) | Sample rate, `k`/`M` suffix. |

Durations accept `ms` / `s` / `m` suffixes (`500ms`, `2s`, `1.5m`); no suffix
means seconds; must be > 0.

**Silence, gap and size are checked once per DMA period**, so any stop can lag by
up to one period.

---

## 4. Output directory layout

```
DIR/
    index.json               run metadata + every burst so far (rewritten each burst)
    burst-0001.sr            a normal sigrok session file
    burst-0001.sr.meta.json  per-burst sidecar, written AFTER the .sr is final
    burst-0002.sr
    burst-0002.sr.meta.json
    ...
```

Files are numbered `burst-NNNN` (zero-padded to 4 digits).

### 4a. `index.json` (whole-run view)

Rewritten on **every** burst, so a reader must not treat it as stable mid-run.

```json
{
  "samplerate_hz": 2000000,
  "channels": 8,
  "base_gpio": 2,
  "started": 1789986303.512345,
  "started_ns": 1789986303512345678,
  "protocol": "uart",
  "gap_s": 0.1, "pre_s": 0.01, "post_s": 0.05,
  "bursts": [
    { "file": "burst-0001.sr", "start_sample": 20018, "samples": 121552,
      "start_time": 1789986306.512346, "start_time_ns": 1789986306512345678,
      "first_edge_sample": 40018, "first_edge_time_ns": 1789986306522345678,
      "duration_s": 0.060776, "gap_before_s": 2.99 }
  ]
}
```

### 4b. `burst-NNNN.sr.meta.json` (per-burst sidecar)

Written **only after** the `.sr` is finalised — its **presence means the burst
is complete and safe to decode**. A shipper keys off this file, never off
`index.json`. Full field list (from `write_burst_meta`, `src/record.c`):

| Field | Meaning |
|---|---|
| `samplerate_hz` | sample rate of the capture |
| `channels`, `base_gpio` | capture geometry (channel `k` = GPIO `base_gpio+k`) |
| `burst` | the `.sr` filename this describes |
| `capture_started` | wall-clock (float s) when the whole run began |
| `start_sample` | absolute sample index where this file begins (= `first_edge_sample − pre`) |
| `samples` | number of samples in the file |
| `start_time`, `start_time_ns` | wall-clock of `start_sample` (float s **and** integer ns) |
| `first_edge_sample` | absolute sample index of the change that opened the burst |
| `first_edge_time_ns` | **arrival time** of that first edge, integer ns — stamped when seen, not derived |
| `duration_s` | `samples / samplerate_hz` |
| `gap_before_s` | idle time since the previous burst |
| `protocol` | declared protocol (`uart` / `raw` / …) |
| `decode_channel` | present only when applicable (≥ 0) |
| `baud` | present only when the protocol has a baud rate |
| `edges` | total edges counted in the burst (protocol-blind) |
| `active_channels` | list of channels that actually changed (protocol-blind) |
| `clock_synchronised` | `adjtimex(2)`: was anything steering the clock (NTP)? |
| `clock_max_error_us` | kernel's max clock error estimate |
| `clock_drift_ns` | how far the sample count had walked from the wall clock |
| `clock_source` | always `CLOCK_REALTIME` |
| `written_by` | `"pi5la"` |

Continuous mode (`--out`) writes the analogous single sidecar `<out>.meta.json`.

---

## 5. Timestamps — what's honest and why

- **`first_edge_time_ns` is the arrival time**, stamped the moment the edge was
  seen in its DMA period and anchored to the wall clock read as that period
  landed — *not* computed as `t0 + n/rate`, which would drift with the PIO
  crystal (±20 ppm ≈ 72 ms/hour) and would not follow NTP.
- **`start_time_ns` is `--pre` earlier** than the first edge, because the file
  deliberately begins before the traffic.
- **Times are integer nanoseconds.** A `double` holds only ~240 ns of resolution
  at today's epoch values — coarser than a single sample at 50 MSa/s. The float
  `start_time` is kept only for tools that already read it.
- **Clock is `CLOCK_REALTIME`** (NTP-disciplined). pi5la *warns at startup* if
  the clock is free-running but never refuses to capture; `clock_synchronised`
  and `clock_drift_ns` record the state per burst.
- **`edges` and `active_channels` are protocol-blind**, counted from the samples
  themselves — a burst is describable even when no decoder understands the bus.

---

## 6. Latency — how soon a burst appears

Set by the **DMA period**, not by any shipper. A burst cannot be *seen* until
the DMA period holding it fills, then `--gap` of silence must pass, then (if
shipping) `sigrok-cli` has to start.

Period fill time = `period_bytes × 4 samples/word ÷ rate` … more simply, at
4 samples per 32-bit word:

| `--dma-buf` | samples/period | fill time @ 2 MSa/s |
|---|---|---|
| `4 MiB` (default) | 4,194,304 | **~2.1 s** |
| `1 MiB` | 1,048,576 | ~0.52 s |
| `262144` (256 KiB) | 262,144 | **~0.13 s** |

Why the default is large: an SD card can stall for **seconds** under a
*continuous* capture, so the 4 MiB default protects that case. Monitor mode
writes almost nothing, so a smaller period is safe here and gets bursts to disk
sooner — `--dma-buf 262144` is a good monitor setting. **But** if you shrink it
and see `DMA ring overflow`, raise it again: the ring is only 4 periods and a
disk stall that outlasts 4 periods drops samples.

---

## 7. Reusing a directory — numbering resumes (important)

A run that finds `burst-0003.sr` starts writing at `burst-0004.sr` and says so.
The directory **doubles as a shipper's work queue**:

- an unshipped burst waiting to be sent, and
- the sidecar "tombstone" of a burst already sent (its `.sr` may be deleted),

both stay behind across runs. Restarting numbering at 1 would **overwrite an
unshipped burst** and give two different bursts the same name. `index.json`
still describes only the run that wrote it; the per-burst sidecars are the
record that spans runs. Covered by `test_record`.

To start clean, point `--monitor` at a fresh (or emptied) directory.

---

## 8. Activity detection (how "idle" is decided)

From `src/idle.c`. A packed 32-bit FIFO word is **idle** when every sample in it
equals the level of the last sample seen before it, on every captured channel;
any other word contains a change. The scan runs on every DMA period at up to
**12.5 M words/s**, so an idle word costs a single comparison. `burst.c` uses the
*first* and *last* changed word within a period to place the lead-in and to know
where traffic started.

Consequence: **every captured channel participates.** A floating unused input
that toggles will keep bursts open or never let the line go quiet — tie unused
buffer inputs to GND.

---

## 9. Live shipping to Elasticsearch (optional)

With `--es-url`, each burst is decoded and uploaded **as it closes**, out of the
capture path (`src/shipper.c`, `posix_spawn` of `sr2es.py`), so nothing slow
runs inside burst close. One shipper runs at a time; a burst that closes while
the shipper is busy is left on disk for the next sweep (disk is the queue),
counted as `ship_deferred`.

```bash
export ES_USER ES_PASSWORD          # or ES_API_KEY — credentials come from the ENV, never argv
pi5la --rate 2M --monitor ~/captures/mon1 \
      --es-url https://es.example:9200 --es-index bus-traffic \
      --protocol uart --decode-channel 0 --baud 115200
```

- `--protocol uart` → one document per decoded UART message.
  `--protocol raw` → one document per burst, no decoder (and `.sr` files are
  kept, since a raw document carries no payload).
- `--uart-type` (default `TTL-3V3`), `--port` (default `chN`) label the line.
- `--insecure` skips TLS verification; `--sr2es PATH` overrides the script.
- **No credential ever reaches argv or a form** — `pi5la`, `sr2es.py` and the web
  UI read `ES_API_KEY` / `ES_USER` / `ES_PASSWORD` from the environment.

You can also ship a directory after the fact: `sr2es.py --ship DIR`.

---

## 10. Decoding a burst

Each burst file is an ordinary continuous `.sr` — decode it like any capture:

```bash
sigrok-cli -i ~/captures/mon1/burst-0001.sr \
           -P uart:rx=GPIO2:baudrate=115200 -B uart=rx
sigrok-cli -i ~/captures/mon1/burst-0001.sr --show      # inspect
```

---

## 11. End-of-run summary

On exit pi5la prints, e.g.:

```
monitor: 3 burst(s) in /home/ansari/captures/mon1, 1.09% of the time recorded
```

- `no change seen on any channel: check the wiring` — nothing ever changed.
- If `--silence-timeout` fired: `silence timeout: stopped after … (leave
  --silence-timeout off to keep monitoring)`.

Exit codes: `0` ok · `1` error · `2` dropped samples. A healthy run reports
`stall events: 0`.

---

## 12. Tuning quick guide

| Symptom / goal | Change |
|---|---|
| Bursts appear too slowly | lower `--dma-buf` (e.g. `262144`), watch for `DMA ring overflow` |
| `DMA ring overflow` seen | raise `--dma-buf` (ring is only 4 periods) |
| One transaction split into two files | raise `--gap` |
| Missing context before the message | raise `--pre` |
| Message tail cut off | raise `--post` |
| Line "never goes quiet" | GND unused inputs; check `active_channels` in a sidecar |
| Want it to stop when the bus dies | add `--silence-timeout` (≥ `--gap`) |

---

## 13. Worked example (verified on the Pi, 2026-09-21)

Three UART lines sent 3 s apart during a 15 s monitor run at 2 MSa/s, 8 channels:

```bash
pi5la --rate 2M --monitor ~/captures/mon1 --gap 100ms --pre 10ms --post 50ms
```
```
burst 1: 121552 samples (0.061 s)   gap_before_s 2.99
burst 2: 121548 samples (0.061 s)   gap_before_s 2.95
burst 3: 121548 samples (0.061 s)   gap_before_s 2.95
monitor: 3 burst(s), 1.09% of the time recorded
```

Directory: **~20 kB** (vs ~33 MB continuous). `burst-0002.sr` decoded to
`burst 2`.
