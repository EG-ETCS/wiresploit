#define _POSIX_C_SOURCE 200809L
#include "record.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "burst.h"
#include "idle.h"
#include "shipper.h"
#include "srwriter.h"
#include "unpack.h"

#define MAX_BURSTS 100000
#define PATH_LEN 1024

/*
 * Activity is scanned in pieces of this many words, independently of the DMA
 * period: 16384 words is 64 KiB, so 65536 samples at 8 channels, about 33 ms
 * at 2 MSa/s and 1.3 ms at 50 MSa/s.
 */
#define RECORD_SCAN_CHUNK_WORDS 16384

typedef struct {
    unsigned index;
    uint64_t start_sample;
    uint64_t samples;
    double start_time;          /* wall clock, seconds since the epoch */
    uint64_t start_ns;          /* the same instant, exactly */
    uint64_t first_edge_sample; /* the change that opened the burst */
    uint64_t first_edge_ns;     /* when that change arrived, stamped on the spot */
    uint64_t edges;             /* samples at which any channel changed */
    uint8_t active_mask;        /* channels that moved during the burst */
    double gap_before_s;
} burst_info_t;

typedef struct {
    const capture_config_t *cfg;
    const record_opts_t *opts;
    record_stats_t *stats;

    unsigned spw;
    double rate_hz;
    double t0;                  /* wall clock at capture start */
    uint64_t t0_ns;             /* the same, as exact nanoseconds */
    char names[8][16];
    const char *name_ptrs[8];

    capture_stream_result_t *sres;  /* read live: the wall-clock anchors */
    uint64_t period_end_sample;     /* absolute sample just past the latest period */
    uint64_t period_end_ns;         /* wall clock when that period landed */
    clocksync_t clock;              /* is the system clock NTP-disciplined? */
    shipper_t ship;

    uint8_t *buf;               /* unpacked period */
    size_t buf_cap;

    idle_scan_t scan;
    uint64_t last_change;       /* absolute sample index just past the last change */
    uint64_t silence_samples;   /* 0 = no silence timeout */
    bool waiting;               /* holding off until the first change */

    uint8_t *ring;              /* the most recent pre_samples, for lead-in */
    uint64_t ring_cap;
    uint64_t ring_len;

    srwriter_t w;
    bool file_open;
    uint64_t file_start;        /* absolute sample of the file's first sample */

    burst_t burst;
    burst_info_t *bursts;
    unsigned index_base;        /* number this run's first burst gets */
    unsigned n_bursts;
    uint64_t prev_burst_end;    /* absolute sample where the previous burst ended */

    /* The open burst, accumulated as it arrives. */
    uint64_t cur_first_edge;
    uint64_t cur_first_edge_ns;
    uint64_t cur_edges;
    uint8_t cur_level;          /* last sample seen, for counting changes */
    uint8_t cur_mask;
    bool cur_level_valid;

    bool failed;
} rec_t;

/* ---------- helpers ---------- */

/*
 * When a given sample happened, in wall-clock nanoseconds.
 *
 * Each period's last sample is pinned to the realtime clock read just after
 * that period landed, and the sample is placed relative to that pin. Anchoring
 * on the capture's own start instead would accumulate the PIO crystal's error
 * (+/-20 ppm is ~72 ms an hour, far more than a bit time) across the long runs
 * this is for, and would not follow NTP's corrections. Before the first period
 * lands there is nothing to pin to, so it falls back to the start anchor.
 */
static uint64_t sample_ns(const rec_t *r, uint64_t sample)
{
    uint64_t back;

    if (!r->period_end_ns || sample > r->period_end_sample)
        return r->t0_ns + (uint64_t)llround(sample / r->rate_hz * 1e9);

    back = (uint64_t)llround((r->period_end_sample - sample) / r->rate_hz * 1e9);
    return back < r->period_end_ns ? r->period_end_ns - back : 0;
}

/*
 * Which channels moved, and how often, while a burst was open. Protocol-blind
 * on purpose: it is what makes a burst describable when no decoder understands
 * the bus. `edges` counts samples at which anything changed, so two channels
 * switching together count once.
 */
static void activity_scan(rec_t *r, const uint8_t *samples, uint64_t n)
{
    uint8_t prev;
    uint64_t i = 0;

    if (!n)
        return;
    if (!r->cur_level_valid) {
        r->cur_level = samples[0];
        r->cur_level_valid = true;
        i = 1;
    }
    prev = r->cur_level;
    for (; i < n; i++) {
        uint8_t diff = (uint8_t)(samples[i] ^ prev);

        if (diff) {
            r->cur_mask |= diff;
            r->cur_edges++;
            prev = samples[i];
        }
    }
    r->cur_level = prev;
}

static uint64_t samples_for(double seconds, double rate_hz)
{
    double n;

    if (seconds <= 0)
        return 0;
    n = ceil(seconds * rate_hz);
    return n > 0 ? (uint64_t)n : 0;
}

static void ring_append(rec_t *r, const uint8_t *samples, uint64_t n)
{
    uint64_t keep;

    if (r->ring_cap == 0)
        return;
    if (n >= r->ring_cap) {
        memcpy(r->ring, samples + (n - r->ring_cap), (size_t)r->ring_cap);
        r->ring_len = r->ring_cap;
        return;
    }
    keep = r->ring_len + n > r->ring_cap ? r->ring_cap - n : r->ring_len;
    memmove(r->ring, r->ring + (r->ring_len - keep), (size_t)keep);
    memcpy(r->ring + keep, samples, (size_t)n);
    r->ring_len = keep + n;
}

/* ---------- file handling ---------- */

static int file_open(rec_t *r, const char *path, uint64_t start_sample)
{
    if (srwriter_open(&r->w, path))
        return -1;
    r->file_open = true;
    r->file_start = start_sample;
    return 0;
}

static int file_write(rec_t *r, const uint8_t *samples, uint64_t n)
{
    if (srwriter_write(&r->w, samples, (size_t)n))
        return -1;
    r->stats->written += n;
    return 0;
}

/* Writes [from, to) of a period that starts at `base`, pulling anything before
   `base` out of the lead-in ring. */
static int file_write_range(rec_t *r, const uint8_t *period, uint64_t base, uint64_t n,
                            uint64_t from, uint64_t to)
{
    if (from < base) {
        uint64_t want = base - from;

        if (want > r->ring_len)     /* the ring no longer reaches that far back */
            want = r->ring_len;
        if (want && file_write(r, r->ring + (r->ring_len - want), want))
            return -1;
        from = base;
    }
    if (to > base + n)
        to = base + n;
    if (to > from && file_write(r, period + (from - base), to - from))
        return -1;
    return 0;
}

static int file_close(rec_t *r, uint64_t keep_until)
{
    if (!r->file_open)
        return 0;
    if (keep_until > r->file_start) {
        uint64_t keep = keep_until - r->file_start;

        if (keep < r->w.samples) {
            r->stats->written -= r->w.samples - keep;
            if (srwriter_truncate(&r->w, keep))
                return -1;
        }
    }
    r->file_open = false;
    return srwriter_finish(&r->w, (uint64_t)llround(r->rate_hz), r->cfg->channels,
                           r->name_ptrs);
}

/* ---------- monitor index ---------- */

/* The protocol is declared before the run starts; nothing here infers it. */
static const char *record_protocol(const record_opts_t *o)
{
    return o->protocol && o->protocol[0] ? o->protocol : "raw";
}

/*
 * How far the capture's own clock has walked away from the system clock, and
 * whether the system clock is itself disciplined. Recorded with every capture
 * so a consumer can weigh a timestamp instead of trusting it blindly.
 */
static void write_clock_fields(rec_t *r, FILE *f)
{
    int64_t drift = 0;

    if (r->period_end_ns) {
        uint64_t derived = r->t0_ns +
                           (uint64_t)llround(r->period_end_sample / r->rate_hz * 1e9);

        drift = (int64_t)r->period_end_ns - (int64_t)derived;
    }
    fprintf(f, "  \"clock_synchronised\": %s,\n  \"clock_max_error_us\": %ld,\n",
            r->clock.synchronised ? "true" : "false", r->clock.max_error_us);
    fprintf(f, "  \"clock_drift_ns\": %" PRId64 ",\n"
               "  \"clock_source\": \"CLOCK_REALTIME\",\n", drift);
}

static int write_index(rec_t *r)
{
    char path[PATH_LEN];
    FILE *f;
    unsigned i;

    snprintf(path, sizeof(path), "%s/index.json", r->opts->monitor_dir);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "record: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(f, "{\n  \"samplerate_hz\": %.0f,\n  \"channels\": %u,\n  \"base_gpio\": %u,\n",
            r->rate_hz, r->cfg->channels, r->cfg->base_gpio);
    fprintf(f, "  \"started\": %.6f,\n  \"started_ns\": %" PRIu64 ",\n", r->t0, r->t0_ns);
    fprintf(f, "  \"protocol\": \"%s\",\n", record_protocol(r->opts));
    fprintf(f, "  \"gap_s\": %g,\n  \"pre_s\": %g,\n  \"post_s\": %g,\n",
            r->opts->gap_s, r->opts->pre_s, r->opts->post_s);
    fprintf(f, "  \"bursts\": [\n");
    for (i = 0; i < r->n_bursts; i++) {
        const burst_info_t *b = &r->bursts[i];

        fprintf(f, "    { \"file\": \"burst-%04u.sr\", \"start_sample\": %" PRIu64
                   ", \"samples\": %" PRIu64 ", \"start_time\": %.6f"
                   ", \"start_time_ns\": %" PRIu64
                   ", \"first_edge_sample\": %" PRIu64
                   ", \"first_edge_time_ns\": %" PRIu64
                   ", \"duration_s\": %.6f, \"gap_before_s\": %.6f }%s\n",
                b->index, b->start_sample, b->samples, b->start_time, b->start_ns,
                b->first_edge_sample, b->first_edge_ns,
                b->samples / r->rate_hz, b->gap_before_s, i + 1 < r->n_bursts ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    if (fclose(f)) {
        fprintf(stderr, "record: writing %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * A .sr file has no room for wall-clock time, but documents sent downstream
 * need it, so a continuous capture gets "<out>.meta.json" beside it. Monitor
 * mode records the same thing per burst in index.json.
 */
static int write_meta(rec_t *r, uint64_t start_sample, uint64_t samples)
{
    char path[PATH_LEN];
    FILE *f;

    snprintf(path, sizeof(path), "%s.meta.json", r->opts->out_path);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "record: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(f, "{\n  \"samplerate_hz\": %.0f,\n  \"channels\": %u,\n  \"base_gpio\": %u,\n",
            r->rate_hz, r->cfg->channels, r->cfg->base_gpio);
    fprintf(f, "  \"capture_started\": %.6f,\n  \"start_sample\": %" PRIu64 ",\n",
            r->t0, start_sample);
    fprintf(f, "  \"start_time\": %.6f,\n  \"start_time_ns\": %" PRIu64 ",\n",
            r->t0 + start_sample / r->rate_hz, sample_ns(r, start_sample));
    fprintf(f, "  \"samples\": %" PRIu64 ",\n", samples);
    fprintf(f, "  \"protocol\": \"%s\",\n", record_protocol(r->opts));
    write_clock_fields(r, f);
    fprintf(f, "  \"duration_s\": %.6f\n}\n", samples / r->rate_hz);
    if (fclose(f)) {
        fprintf(stderr, "record: writing %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Everything the shipper needs about one burst, beside the burst itself.
 *
 * Written only once the .sr has been finalised, so its presence is the signal
 * that the burst is complete and safe to decode -- the shipper never has to
 * read index.json, which this process rewrites on every burst.
 */
static int write_burst_meta(rec_t *r, const burst_info_t *b)
{
    char path[PATH_LEN];
    FILE *f;
    unsigned k;
    bool first = true;

    snprintf(path, sizeof(path), "%s/burst-%04u.sr.meta.json", r->opts->monitor_dir,
             b->index);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "record: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(f, "{\n  \"samplerate_hz\": %.0f,\n  \"channels\": %u,\n  \"base_gpio\": %u,\n",
            r->rate_hz, r->cfg->channels, r->cfg->base_gpio);
    fprintf(f, "  \"burst\": \"burst-%04u.sr\",\n  \"capture_started\": %.6f,\n",
            b->index, r->t0);
    fprintf(f, "  \"start_sample\": %" PRIu64 ",\n  \"samples\": %" PRIu64 ",\n",
            b->start_sample, b->samples);
    fprintf(f, "  \"start_time\": %.6f,\n  \"start_time_ns\": %" PRIu64 ",\n",
            b->start_time, b->start_ns);
    fprintf(f, "  \"first_edge_sample\": %" PRIu64 ",\n"
               "  \"first_edge_time_ns\": %" PRIu64 ",\n",
            b->first_edge_sample, b->first_edge_ns);
    fprintf(f, "  \"duration_s\": %.6f,\n  \"gap_before_s\": %.6f,\n",
            b->samples / r->rate_hz, b->gap_before_s);
    fprintf(f, "  \"protocol\": \"%s\",\n", record_protocol(r->opts));
    if (r->opts->decode_channel >= 0)
        fprintf(f, "  \"decode_channel\": %d,\n", r->opts->decode_channel);
    if (r->opts->baud > 0)
        fprintf(f, "  \"baud\": %u,\n", r->opts->baud);
    fprintf(f, "  \"edges\": %" PRIu64 ",\n  \"active_channels\": [", b->edges);
    for (k = 0; k < r->cfg->channels; k++) {
        if (b->active_mask & (1u << k)) {
            fprintf(f, "%s%u", first ? "" : ", ", k);
            first = false;
        }
    }
    fprintf(f, "],\n");
    write_clock_fields(r, f);
    fprintf(f, "  \"written_by\": \"pi5la\"\n}\n");
    if (fclose(f)) {
        fprintf(stderr, "record: writing %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Where this run's numbering should start.
 *
 * A monitor directory is also the shipper's work queue: bursts waiting to go
 * stay in it, and shipped ones leave their sidecar behind as a tombstone.
 * Restarting at 1 would overwrite a burst that had not been sent yet -- losing
 * it outright -- and would give two different bursts the same name. So carry on
 * from the highest number already there.
 */
static unsigned next_burst_index(const char *dir)
{
    unsigned highest = 0;
    struct dirent *e;
    DIR *d = opendir(dir);

    if (!d)
        return 1;
    while ((e = readdir(d))) {
        unsigned n;

        /* Matches burst-0007.sr and its .meta.json / .ndjson alike. */
        if (sscanf(e->d_name, "burst-%u.sr", &n) == 1 && n > highest)
            highest = n;
    }
    closedir(d);
    return highest + 1;
}

static int burst_open(rec_t *r, uint64_t start_sample)
{
    unsigned index = r->index_base + r->n_bursts;
    char path[PATH_LEN];

    if (r->n_bursts >= MAX_BURSTS) {
        fprintf(stderr, "record: too many bursts (%d)\n", MAX_BURSTS);
        return -1;
    }
    snprintf(path, sizeof(path), "%s/burst-%04u.sr", r->opts->monitor_dir, index);
    if (file_open(r, path, start_sample))
        return -1;
    fprintf(stderr, "burst %u starts at sample %" PRIu64 " (%.3f s)\n", index,
            start_sample, start_sample / r->rate_hz);
    return 0;
}

static int burst_close(rec_t *r, uint64_t keep_until)
{
    burst_info_t *info;
    uint64_t samples;

    if (!r->file_open)
        return 0;
    samples = keep_until > r->file_start ? keep_until - r->file_start : 0;
    if (samples > r->w.samples)
        samples = r->w.samples;
    if (file_close(r, keep_until))
        return -1;

    info = &r->bursts[r->n_bursts];
    info->index = r->index_base + r->n_bursts;
    info->start_sample = r->file_start;
    info->samples = samples;
    info->start_ns = sample_ns(r, r->file_start);
    info->start_time = info->start_ns / 1e9;
    /* Stamped when the edge arrived, not now: the decode happens later, and
       this is the moment the traffic actually reached the pin. */
    info->first_edge_sample = r->cur_first_edge;
    info->first_edge_ns = r->cur_first_edge_ns;
    info->edges = r->cur_edges;
    info->active_mask = r->cur_mask;
    info->gap_before_s = (double)(r->file_start - r->prev_burst_end) / r->rate_hz;
    r->prev_burst_end = r->file_start + samples;
    r->n_bursts++;
    r->stats->bursts = r->n_bursts;

    fprintf(stderr, "burst %u: %" PRIu64 " samples (%.3f s)\n", info->index, info->samples,
            info->samples / r->rate_hz);

    /* The .sr is complete; describe it, then index it, then hand it off. */
    if (write_burst_meta(r, info))
        return -1;
    if (write_index(r))
        return -1;
    shipper_notify(&r->ship);
    return 0;
}

/* ---------- the sink ---------- */

static int process_chunk(rec_t *r, const uint32_t *words, size_t n_words)
{
    const record_opts_t *o = r->opts;
    uint64_t n = (uint64_t)n_words * r->spw;
    uint64_t base = r->stats->samples;
    size_t cb = 0, ce = 0;
    bool changed;

    if (n > r->buf_cap) {
        uint8_t *nb = realloc(r->buf, (size_t)n);

        if (!nb) {
            fprintf(stderr, "record: cannot allocate %" PRIu64 " bytes\n", n);
            r->failed = true;
            return -1;
        }
        r->buf = nb;
        r->buf_cap = (size_t)n;
    }
    unpack_words(words, n_words, r->cfg->channels, r->buf, (size_t)n);

    changed = idle_scan_range(&r->scan, words, n_words, &cb, &ce);
    if (changed) {
        r->last_change = base + (uint64_t)ce * r->spw;
        r->stats->activity_seen = true;
    }
    r->stats->samples = base + n;

    if (o->monitor_dir) {
        burst_action_t act;

        burst_feed(&r->burst, n, changed, (uint64_t)cb * r->spw, (uint64_t)ce * r->spw, &act);
        if (act.begin) {
            /* Stamp the arrival now, while the period it came in is still the
               one the clock is pinned to. */
            r->cur_first_edge = act.first_edge;
            r->cur_first_edge_ns = sample_ns(r, act.first_edge);
            r->cur_edges = 0;
            r->cur_mask = 0;
            r->cur_level_valid = false;
            if (burst_open(r, act.start))
                goto failed;
        }
        if (act.write && r->file_open) {
            uint64_t from = act.write_from < base ? base : act.write_from;
            uint64_t to = act.write_to > base + n ? base + n : act.write_to;

            if (file_write_range(r, r->buf, base, n, act.write_from, act.write_to))
                goto failed;
            if (to > from)
                activity_scan(r, r->buf + (from - base), to - from);
        }
        if (act.end && burst_close(r, act.end_at))
            goto failed;
        if (!r->file_open)
            ring_append(r, r->buf, n);
    } else if (r->waiting) {
        /* --silence-timeout without monitor: hold off until the line moves,
           keeping only the lead-in, so waiting costs no disk. */
        if (!changed) {
            ring_append(r, r->buf, n);
        } else {
            uint64_t start = base + (uint64_t)cb * r->spw;

            start = start > r->ring_cap ? start - r->ring_cap : 0;
            r->waiting = false;
            if (file_open(r, o->out_path, start) ||
                file_write_range(r, r->buf, base, n, start, base + n))
                goto failed;
        }
    } else if (file_write(r, r->buf, n)) {
        goto failed;
    }

    if (r->silence_samples && r->stats->activity_seen &&
        r->stats->samples - r->last_change >= r->silence_samples) {
        r->stats->silence_stopped = true;
        return 1;
    }
    if (o->max_samples && r->stats->samples >= o->max_samples) {
        r->stats->samples_cap_hit = true;
        return 1;
    }
    if (o->max_bytes && r->stats->written >= o->max_bytes) {
        r->stats->size_cap_hit = true;
        return 1;
    }
    return 0;

failed:
    r->failed = true;
    r->stats->write_failed = true;
    return -1;
}

/*
 * A DMA period is deliberately large (4 MiB, so an SD card stall can't
 * overflow the ring), but that is 2.1 s of samples at 2 MSa/s on 8 channels.
 * Checking for activity once per period would make every burst at least that
 * long, so each period is scanned in RECORD_SCAN_CHUNK-sized pieces: burst
 * edges and silence timeouts then depend on the chunk, not the ring.
 */
static int sink(const uint32_t *words, size_t n_words, void *arg)
{
    rec_t *r = arg;
    size_t done = 0;

    /* This period has landed: pin its last sample to the wall clock that was
       read the moment it arrived, and place everything in it from there. */
    r->period_end_sample = r->stats->samples + (uint64_t)n_words * r->spw;
    if (r->sres && r->sres->period_realtime_ns)
        r->period_end_ns = r->sres->period_realtime_ns;

    /*
     * Sample 0 is when the state machine was enabled, not when record_run()
     * started: PIO and DMA setup takes milliseconds. Adopt the real anchor as
     * soon as capture_stream() publishes it, so the drift figure a burst
     * records measures the clocks rather than the setup.
     */
    if (r->sres && r->sres->start_realtime_ns)
        r->t0_ns = r->sres->start_realtime_ns;

    while (done < n_words) {
        size_t chunk = n_words - done;
        int ret;

        if (chunk > RECORD_SCAN_CHUNK_WORDS)
            chunk = RECORD_SCAN_CHUNK_WORDS;
        ret = process_chunk(r, words + done, chunk);
        if (ret)
            return ret;
        done += chunk;
    }
    return 0;
}

/* ---------- entry point ---------- */

int record_run(PIO pio, const capture_config_t *cfg, const record_opts_t *opts,
               record_stats_t *stats, capture_stream_result_t *sres)
{
    rec_t r;
    unsigned k;
    int ret;

    memset(&r, 0, sizeof(r));
    memset(stats, 0, sizeof(*stats));
    r.cfg = cfg;
    r.opts = opts;
    r.stats = stats;
    r.spw = unpack_samples_per_word(cfg->channels);
    r.rate_hz = clkdiv_hz(cfg->div);
    r.t0_ns = realtime_ns();
    r.t0 = r.t0_ns / 1e9;
    r.sres = sres;
    r.w.fd = -1;
    clocksync_read(&r.clock);

    for (k = 0; k < cfg->channels; k++) {
        snprintf(r.names[k], sizeof(r.names[k]), "GPIO%u", cfg->base_gpio + k);
        r.name_ptrs[k] = r.names[k];
    }

    idle_scan_init(&r.scan, cfg->channels);
    r.silence_samples = samples_for(opts->silence_s, r.rate_hz);
    r.ring_cap = samples_for(opts->pre_s, r.rate_hz);
    r.waiting = opts->silence_s > 0 && !opts->monitor_dir;

    if (opts->monitor_dir) {
        if (mkdir(opts->monitor_dir, 0755) && errno != EEXIST) {
            fprintf(stderr, "record: cannot create %s: %s\n", opts->monitor_dir,
                    strerror(errno));
            return -1;
        }
        r.index_base = next_burst_index(opts->monitor_dir);
        if (r.index_base > 1)
            fprintf(stderr, "monitor: %s already holds bursts; numbering from %u\n",
                    opts->monitor_dir, r.index_base);
        burst_init(&r.burst, r.ring_cap, samples_for(opts->post_s, r.rate_hz),
                   samples_for(opts->gap_s, r.rate_hz));
        r.bursts = calloc(MAX_BURSTS, sizeof(*r.bursts));
        if (!r.bursts) {
            fprintf(stderr, "record: out of memory\n");
            return -1;
        }
    } else if (!r.waiting && file_open(&r, opts->out_path, 0)) {
        return -1;
    }

    if (shipper_init(&r.ship, opts, stats, cfg->base_gpio)) {
        free(r.bursts);
        return -1;
    }

    if (r.ring_cap) {
        r.ring = malloc((size_t)r.ring_cap);
        if (!r.ring) {
            fprintf(stderr, "record: cannot allocate a %" PRIu64 "-sample lead-in\n",
                    r.ring_cap);
            free(r.bursts);
            return -1;
        }
    }

    ret = capture_stream(pio, cfg, sink, &r, sres);

    /*
     * A ring overflow or a signal ends the capture early, but everything
     * written so far is good: finish the file instead of throwing it away.
     * Only a write error (r.failed) means the payload is worthless.
     */
    if (ret == 0 && !r.failed) {
        if (opts->monitor_dir) {
            uint64_t end_at;

            if (burst_flush(&r.burst, &end_at) && burst_close(&r, end_at))
                ret = -1;
            else if (write_index(&r))
                ret = -1;
        } else if (r.file_open) {
            uint64_t start_sample = r.file_start, samples = r.w.samples;

            if (file_close(&r, UINT64_MAX) || write_meta(&r, start_sample, samples))
                ret = -1;
        } else if (!r.waiting) {
            ret = -1;   /* should not happen: no file and not waiting */
        }
    } else {
        if (r.file_open)
            srwriter_abort(&r.w);
        ret = -1;
    }

    shipper_finish(&r.ship);

    free(r.buf);
    free(r.ring);
    free(r.bursts);
    return ret;
}
