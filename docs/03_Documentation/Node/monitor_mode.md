# Monitor mode

`--monitor` records **only the bursts of activity on a bus and throws idle
away**, writing one self-describing `.sr` file per burst plus a folder index. It
is meant for watching a bus for minutes to hours without filling the disk with
unchanging bytes.

!!! note "Nothing on the wire is missed"
    Sampling never stops in monitor mode: every DMA period is captured and
    scanned. What changes is only what gets written to disk. Idle is discarded;
    bursts are kept with a lead-in and a tail.

!!! danger "Hardware safety"
    A bus you monitor for hours is usually an external target. Pi 5 GPIO is
    **3.3 V and not 5 V tolerant**: connect a target only through a **74LVC245
    buffer with series resistors and a shared ground**. See
    [capture guide, hardware safety](capture_guide.md#hardware-safety).

Source of truth in the code: `node/capture/src/record.c` (orchestration),
`src/burst.c` (the burst state machine), `src/idle.c` (activity detection) and
`src/main.c` (flags and defaults).

In the examples, `<install_dir>` is the folder pi5la is installed in on the Pi,
and `<install_dir>/bin` is on your `PATH`.

---

## 1. Why it exists

A continuous capture writes 1 byte per sample regardless of traffic. Eight
channels at 2 MSa/s is **7.2 GB/hour of nothing** on an idle bus. Monitor mode
writes only the active windows, and each burst file still decodes exactly like
a normal capture, because every file is internally continuous. How much it
saves in practice is in the
[findings report, monitor run](findings_report.md#b4-monitor-mode).

Use monitor mode when you want to leave a capture running and record whatever
transactions happen, each on its own timeline, without watching over it.

---

## 2. The burst state machine

Implemented in `src/burst.c`. Positions are absolute sample numbers since the
capture began.

```
   waiting ──first change──▶ recording ──gap samples with no change──▶ end
      ▲                                                                 │
      └─────────────────────────────────────────────────────────────────┘
        (idle discarded)         (burst kept, ends `post` after last change)
```

- **waiting**: the bus is quiet; nothing is written. A pre-roll ring in RAM
  keeps the most recent `pre` samples so the file can begin *before* the
  traffic.
- **change seen**: a burst opens `pre` samples **before** the first changed
  sample (`first_edge`). The pre-roll comes out of the ring, so the lead-in is
  real captured data, not padding.
- **recording**: samples are appended as they arrive. Any change refreshes
  `last_change`.
- **end**: once `gap` samples pass with **no** change, the burst closes,
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
| `--monitor DIR` | — | Enable monitor mode; write bursts into `DIR` (created if needed). Cannot be combined with `--out`. |
| `--gap T` | `100ms` | The silence that **ends** a burst: once no channel has changed for this long, the burst closes and its file is written. Longer means fewer, larger files; a bus that idles mid-transaction needs this raised. |
| `--pre T` | `10ms` | Lead-in kept **before** the first change (comes from the pre-roll ring). |
| `--post T` | `50ms` | Tail kept **after** the last change. |
| `--dma-buf BYTES` | `4 MiB` (disk streaming) | Cyclic DMA period size; the ring holds **4** periods. Governs how soon a burst can appear (§6). Lower it in monitor mode for lower latency. |
| `--channels N` | `8` | 1, 2, 4 or 8. Every captured channel counts toward "idle": tie unused buffer inputs to GND or they may never go quiet. |
| `--base-gpio G` | `2` | First capture GPIO; pins are consecutive (`base+k` = channel `k`). |
| `--silence-timeout T` | off | Wait for the first change, then **stop the whole session** once quiet this long. Rarely what monitoring wants: leave it off to keep recording. Must be ≥ `--gap` (checked at startup). |
| `--rate RATE` | — (required) | Sample rate, `k` or `M` suffix. |

Durations accept `ms`, `s` and `m` suffixes (`500ms`, `2s`, `1.5m`); no suffix
means seconds; the value must be > 0.

**Silence, gap and size are checked once per DMA period**, so any stop can lag
by up to one period.

The `--es-*` flags are described in §9.

---

## 4. Output folder layout

```
DIR/
    index.json               run metadata and every burst so far (rewritten each burst)
    burst-0001.sr            a normal sigrok session file
    burst-0001.sr.meta.json  per-burst sidecar, written AFTER the .sr is final
    burst-0002.sr
    burst-0002.sr.meta.json
    ...
```

Files are numbered `burst-NNNN` (zero-padded to 4 digits).

### 4a. `index.json` (whole-run view)

Rewritten on **every** burst, so a reader must not treat it as stable mid-run.
The silence between bursts is recorded here as `gap_before_s`, rather than as
gigabytes of unchanging bytes.

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

Written **only after** the `.sr` is finalised, so its **presence means the burst
is complete and safe to decode**. A shipper keys off this file, never off
`index.json`.

```json
{
  "samplerate_hz": 2000000, "channels": 8, "base_gpio": 2,
  "burst": "burst-0001.sr", "capture_started": 1789986303.512345,
  "start_sample": 20018, "samples": 121552,
  "start_time": 1789986306.512346, "start_time_ns": 1789986306512345678,
  "first_edge_sample": 40018,
  "first_edge_time_ns": 1789986306522345678,
  "duration_s": 0.060776, "gap_before_s": 2.99,
  "protocol": "uart", "decode_channel": 0, "baud": 115200,
  "edges": 214, "active_channels": [0],
  "clock_synchronised": true, "clock_max_error_us": 1234,
  "clock_drift_ns": -41200, "clock_source": "CLOCK_REALTIME",
  "written_by": "pi5la"
}
```

Full field list (from `write_burst_meta`, `src/record.c`):

| Field | Meaning |
|---|---|
| `samplerate_hz` | Sample rate of the capture |
| `channels`, `base_gpio` | Capture geometry (channel `k` = GPIO `base_gpio+k`) |
| `burst` | The `.sr` file name this describes |
| `capture_started` | Wall-clock time (float seconds) when the whole run began |
| `start_sample` | Absolute sample index where this file begins (= `first_edge_sample − pre`) |
| `samples` | Number of samples in the file |
| `start_time`, `start_time_ns` | Wall-clock time of `start_sample` (float seconds **and** integer ns) |
| `first_edge_sample` | Absolute sample index of the change that opened the burst |
| `first_edge_time_ns` | **Arrival time** of that first edge, integer ns: stamped when seen, not derived |
| `duration_s` | `samples / samplerate_hz` |
| `gap_before_s` | Idle time since the previous burst |
| `protocol` | Declared protocol (`uart`, `raw`, …) |
| `decode_channel` | Present only when applicable (≥ 0) |
| `baud` | Present only when the protocol has a baud rate |
| `edges` | Total edges counted in the burst (protocol-blind) |
| `active_channels` | List of channels that actually changed (protocol-blind) |
| `clock_synchronised` | From `adjtimex(2)`: was anything (NTP) steering the clock? |
| `clock_max_error_us` | The kernel's maximum clock error estimate |
| `clock_drift_ns` | How far the sample count had walked from the wall clock |
| `clock_source` | Always `CLOCK_REALTIME` |
| `written_by` | `"pi5la"` |

When live shipping confirms an upload, `sr2es` adds `"shipped": true` to the
sidecar (§9); pi5la itself never writes that field.

Continuous mode (`--out`) writes the matching single sidecar `<out>.meta.json`.

---

## 5. Timestamps: what is honest, and why

- **`first_edge_time_ns` is the arrival time**, stamped the moment the edge was
  seen in its DMA period and anchored to the wall clock read as that period
  landed. It is *not* computed as `t0 + n/rate`, which would drift with the PIO
  crystal (±20 ppm is about 72 ms/hour) and would not follow NTP.
- **`start_time_ns` is `--pre` earlier** than the first edge, because the file
  deliberately begins before the traffic.
- **Times are integer nanoseconds.** A `double` holds only about 240 ns of
  resolution at today's epoch values, coarser than a single sample at 50 MSa/s
  and coarser than a bit on any bus worth capturing. The float `start_time` is
  kept only for tools that already read it.
- **The clock is `CLOCK_REALTIME`**, which is what NTP disciplines. pi5la
  *warns at startup* if the clock is free-running but never refuses to capture;
  `clock_synchronised` and `clock_drift_ns` record the state per burst.
- **`edges` and `active_channels` are protocol-blind**, counted from the samples
  themselves, so a burst is describable even when no decoder understands the
  bus.

---

## 6. Latency: how soon a burst appears

Set by the **DMA period**, not by any shipper. A burst cannot be *seen* until the
DMA period holding it fills, then `--gap` of silence must pass, then (if
shipping) `sigrok-cli` has to start.

Samples per period = `period_bytes × 8 ÷ channels` (one bit per channel per
sample); fill time = samples per period ÷ rate. At 8 channels that is one sample
per byte:

| `--dma-buf` | Samples per period (8 channels) | Fill time at 2 MSa/s |
|---|---|---|
| `4 MiB` (default) | 4,194,304 | **about 2.1 s** |
| `1 MiB` | 1,048,576 | about 0.52 s |
| `262144` (256 KiB) | 262,144 | **about 0.13 s** |

Why the default is large: an SD card can stall for **seconds** under a
*continuous* capture, so the 4 MiB default protects that case. Monitor mode
writes almost nothing, so a smaller period is safe here and gets bursts to disk
sooner: `--dma-buf 262144` is a good monitor setting. **But** if you shrink it
and see `DMA ring overflow`, raise it again: the ring is only 4 periods, and a
disk stall that outlasts 4 periods drops samples.

---

## 7. Reusing a folder: numbering resumes

A run that finds `burst-0003.sr` starts writing at `burst-0004.sr` and says so.
The folder **doubles as a shipper's work queue**:

- an unshipped burst waiting to be sent, and
- the sidecar "tombstone" of a burst already sent (its `.sr` may be deleted),

both stay behind across runs. Restarting numbering at 1 would **overwrite an
unshipped burst** and give two different bursts the same name. `index.json`
still describes only the run that wrote it; the per-burst sidecars are the
record that spans runs. Covered by `test_record`.

To start clean, point `--monitor` at a new or emptied folder.

---

## 8. Activity detection: how "idle" is decided

From `src/idle.c`. A packed 32-bit FIFO word is **idle** when every sample in it
equals the level of the last sample seen before it, on every captured channel;
any other word contains a change. The scan runs on every DMA period at up to
**12.5 M words/s**, so an idle word costs a single comparison. `burst.c` uses the
*first* and *last* changed word within a period to place the lead-in and to know
where traffic started.

Consequence: **every captured channel takes part.** A floating unused input that
toggles will keep bursts open or never let the line go quiet: tie unused buffer
inputs to GND.

---

## 9. Live shipping to Elasticsearch

Optional. Waiting for a monitor session to end before anything reaches the
database is no good for a bus you are watching for hours. With `--es-url`, pi5la
hands each burst to `sr2es` the moment it closes:

```bash
export ES_USER ES_PASSWORD          # or ES_API_KEY, or set them in node.conf; never on argv
pi5la --rate 2M --monitor <install_dir>/captures/mon1 \
      --es-url https://es.example:9200 --es-index bus-traffic \
      --protocol uart --decode-channel 0 --baud 115200
```

A message is in the index about `--gap` + `--post` + decode time after the line
falls quiet (roughly a second at the defaults, plus the DMA period fill time
from §6), rather than whenever the run is stopped.

- **Nothing slow runs in the capture path.** Decoding and uploading always
  happen in a separate process (`sr2es --ship DIR`, started by `src/shipper.c`
  with `posix_spawn`), never inside the capture loop: a `sigrok-cli` run or a
  stalled POST there would overrun the four-period DMA ring and drop samples.
- **One shipper at a time.** A burst that closes while one is still working is
  left on disk, counted as `ship_deferred`; the next sweep finds it. There is no
  queue to lose: what is on disk *is* the work list.
- **A failed upload is retried.** The NDJSON is written before the upload, so a
  cluster that was down comes back to find its backlog waiting, and the next
  burst's shipper sends it. Nothing already decoded is decoded again.
- **Confirmed data is deleted.** Once the cluster accepts a burst, its `.sr` and
  `.ndjson` go, and the sidecar is kept as the tombstone (`"shipped": true`).
  `--protocol raw` is the exception: its document carries no payload, so the
  `.sr` is the only copy of the waveform and stays.
- **Credentials are never on the command line or in a form.** `ES_API_KEY`, or
  `ES_USER` and `ES_PASSWORD`, come from the environment or from `node.conf`;
  the environment wins. `sr2es` inherits pi5la's environment and also loads
  `node.conf` from its own location, filling in only what the environment
  lacks.

Flags:

- `--protocol uart`: one document per decoded UART message.
  `--protocol raw`: one document per burst, no decoder.
- `--es-index` picks the index; `--decode-channel` and `--baud` configure the
  decoder.
- `--uart-type` (default `TTL-3V3`) and `--port` (default `chN`) label the line.
- `--insecure` skips TLS verification; `--sr2es PATH` overrides the script.

You can also ship a folder after the fact: `sr2es --ship DIR`. Its `--es-url`
and `--sigrok` default to `ES_URL` and `SIGROK_CLI` in `node.conf`. The document
shape is described in the
[capture guide](capture_guide.md#sending-captures-to-elasticsearch).

---

## 10. Decoding a burst

Each burst file is an ordinary continuous `.sr`. Decode it like any capture:

```bash
sigrok-cli -i <install_dir>/captures/mon1/burst-0001.sr \
           -P uart:rx=GPIO2:baudrate=115200 -B uart=rx
sigrok-cli -i <install_dir>/captures/mon1/burst-0001.sr --show      # inspect
```

---

## 11. End-of-run summary

On exit pi5la prints, for example:

```
monitor: 3 burst(s) in <install_dir>/captures/mon1, 1.09% of the time recorded
```

- `no change seen on any channel: check the wiring`: nothing ever changed.
- If `--silence-timeout` fired: `silence timeout: stopped after … (leave
  --silence-timeout off to keep monitoring)`.

Exit codes: `0` OK, `1` error, `2` dropped samples. A healthy run reports
`stall events: 0`.

---

## 12. Tuning quick guide

| Symptom or goal | Change |
|---|---|
| Bursts appear too slowly | Lower `--dma-buf` (for example `262144`), and watch for `DMA ring overflow` |
| `DMA ring overflow` seen | Raise `--dma-buf` (the ring is only 4 periods) |
| One transaction split into two files | Raise `--gap` |
| Missing context before the message | Raise `--pre` |
| Message tail cut off | Raise `--post` |
| Line "never goes quiet" | Tie unused inputs to GND; check `active_channels` in a sidecar |
| Want it to stop when the bus dies | Add `--silence-timeout` (≥ `--gap`) |

A verified example run, with its numbers, is in the
[findings report](findings_report.md#b4-monitor-mode).
