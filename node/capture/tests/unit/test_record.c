/*
 * record.c against synthetic capture data.
 *
 * capture_stream() is stubbed here, so the sink sees exactly the periods this
 * test feeds it: no PIO, no DMA, no hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "record.h"

#define PERIOD_WORDS 1024                       /* 4096 samples at 8 channels */
#define PERIOD_SAMPLES (PERIOD_WORDS * 4)

#define BIG_WORDS 65536                         /* 4 x the scan chunk */

static const char *g_pattern;                   /* '.' = idle period, 'x' = busy */
static uint32_t g_words[PERIOD_WORDS];
static uint32_t *g_big;                         /* one oversized period, or NULL */

/* Stub: feeds one period per character of g_pattern. */
int capture_stream(PIO pio, const capture_config_t *cfg, capture_sink_t sink, void *ctx,
                   capture_stream_result_t *res)
{
    const char *p;

    (void)pio;
    (void)cfg;
    memset(res, 0, sizeof(*res));
    res->rate_hz = clkdiv_hz(cfg->div);
    res->start_realtime_ns = realtime_ns();

    if (g_big) {
        res->bytes = BIG_WORDS * 4;
        res->period_realtime_ns = realtime_ns();
        sink(g_big, BIG_WORDS, ctx);
        res->total_s = 1.0;
        return 0;
    }

    for (p = g_pattern; *p; p++) {
        size_t i;

        for (i = 0; i < PERIOD_WORDS; i++)
            g_words[i] = 0x7f7f7f7f;
        if (*p == 'x') {
            /* A change in the middle of the period, on channel 0. */
            g_words[PERIOD_WORDS / 2] = 0x7f7f7e7f;
            g_words[PERIOD_WORDS / 2 + 1] = 0x7e7e7e7e;
        }
        res->bytes += PERIOD_WORDS * 4;
        res->period_realtime_ns = realtime_ns();
        if (sink(g_words, PERIOD_WORDS, ctx))
            break;
    }
    res->total_s = 1.0;
    return 0;
}

static int count_files(const char *dir, const char *prefix)
{
    char path[512];
    int n = 0;

    for (;;) {
        struct stat st;

        snprintf(path, sizeof(path), "%s/%s-%04d.sr", dir, prefix, n + 1);
        if (stat(path, &st) || st.st_size == 0)
            break;
        n++;
    }
    return n;
}

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    buf = calloc(1, (size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf && len)
        *len = (size_t)n;
    return buf;
}

static void rm_dir(const char *dir)
{
    char path[512];
    int i;

    for (i = 1; i <= 20; i++) {
        snprintf(path, sizeof(path), "%s/burst-%04d.sr", dir, i);
        unlink(path);
        snprintf(path, sizeof(path), "%s/burst-%04d.sr.meta.json", dir, i);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/index.json", dir);
    unlink(path);
    rmdir(dir);
}

int main(void)
{
    capture_config_t cfg = {
        .base_gpio = 2,
        .channels = 8,
        .div = { .div_int = 200, .div_frac = 0 },   /* 1 MHz: 1 sample = 1 us */
        .mode = CAPTURE_DMA,
        .dma_buf_bytes = PERIOD_WORDS * 4,
    };
    static const char dir_template[] = "/tmp/pi5la_rec_XXXXXX";
    char dir[sizeof(dir_template)];
    char out[] = "/tmp/pi5la_out_XXXXXXXX.sr";
    capture_stream_result_t sres;
    record_stats_t st;
    record_opts_t opts;
    char path[512];
    struct stat fst;
    char *index;
    int fd;

    memcpy(dir, dir_template, sizeof(dir));
    CHECK(mkdtemp(dir) != NULL);

    /* --- monitor: two bursts separated by silence ------------------------ */
    memset(&opts, 0, sizeof(opts));
    opts.monitor_dir = dir;
    opts.gap_s = 0.004;          /* 4000 samples: about one period */
    opts.pre_s = 0.001;          /* 1000 samples */
    opts.post_s = 0.002;         /* 2000 samples */
    g_pattern = "..x..x..";      /* 8 periods, 2 with traffic */

    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    CHECK(st.samples == 8 * PERIOD_SAMPLES);
    CHECK(st.activity_seen);
    CHECK(st.bursts == 2);
    CHECK(count_files(dir, "burst") == 2);

    /* Idle is never written: each burst keeps only pre + the busy part + post. */
    CHECK(st.written < st.samples / 2);

    snprintf(path, sizeof(path), "%s/index.json", dir);
    index = slurp(path, NULL);
    CHECK(index != NULL);
    if (index) {
        CHECK(strstr(index, "\"file\": \"burst-0001.sr\"") != NULL);
        CHECK(strstr(index, "\"file\": \"burst-0002.sr\"") != NULL);
        CHECK(strstr(index, "\"samplerate_hz\": 1000000") != NULL);
        CHECK(strstr(index, "\"channels\": 8") != NULL);
        CHECK(strstr(index, "burst-0003") == NULL);
        CHECK(strstr(index, "gap_before_s") != NULL);
        free(index);
    }

    /*
     * Each burst describes itself in a sidecar, written only once its .sr is
     * final. That is what lets a shipper decode a burst while the capture is
     * still running, without reading index.json underneath a rewrite.
     *
     * The pattern puts the change at word 512 of period 2, so the first edge is
     * at sample 2 * 4096 + 512 * 4 = 10240, and the pre-roll makes the file
     * itself start 1000 samples earlier.
     */
    snprintf(path, sizeof(path), "%s/burst-0001.sr.meta.json", dir);
    index = slurp(path, NULL);
    CHECK(index != NULL);
    if (index) {
        CHECK(strstr(index, "\"first_edge_sample\": 10240") != NULL);
        CHECK(strstr(index, "\"start_sample\": 9240") != NULL);
        CHECK(strstr(index, "\"first_edge_time_ns\": 17") != NULL);   /* a real epoch */
        CHECK(strstr(index, "\"start_time_ns\": 17") != NULL);
        CHECK(strstr(index, "\"burst\": \"burst-0001.sr\"") != NULL);
        CHECK(strstr(index, "\"protocol\": \"raw\"") != NULL);       /* none declared */
        CHECK(strstr(index, "\"active_channels\": [0]") != NULL);    /* only ch0 moved */
        CHECK(strstr(index, "\"clock_synchronised\":") != NULL);
        CHECK(strstr(index, "\"edges\": 0,") == NULL);               /* it did move */
        CHECK(strstr(index, "\"shipped\"") == NULL);                 /* nothing ships it */
        free(index);
    }

    /* index.json carries the same instants, exactly, not rounded to a double. */
    snprintf(path, sizeof(path), "%s/index.json", dir);
    index = slurp(path, NULL);
    CHECK(index != NULL);
    if (index) {
        CHECK(strstr(index, "\"first_edge_sample\": 10240") != NULL);
        CHECK(strstr(index, "\"start_time_ns\": 17") != NULL);
        CHECK(strstr(index, "\"protocol\": \"raw\"") != NULL);
        free(index);
    }

    /* The payload files are cleaned up, leaving only .sr files. */
    snprintf(path, sizeof(path), "%s/burst-0001.sr.logic", dir);
    CHECK(stat(path, &fst) != 0);
    rm_dir(dir);

    /*
     * One DMA period is much larger than the gap, so activity has to be
     * scanned in chunks: two bursts inside a single period must still come out
     * as two files, not one.
     */
    memcpy(dir, dir_template, sizeof(dir));
    CHECK(mkdtemp(dir) != NULL);
    opts.monitor_dir = dir;
    g_big = calloc(BIG_WORDS, sizeof(*g_big));
    CHECK(g_big != NULL);
    if (g_big) {
        size_t i;

        for (i = 0; i < BIG_WORDS; i++)
            g_big[i] = 0x7f7f7f7f;
        g_big[1000] = 0x7e7e7e7f;           /* first burst, sample ~4000 */
        g_big[1001] = 0x7e7e7e7e;
        g_big[40000] = 0x7f7f7f7e;          /* second, ~144 000 samples later */
        g_big[40001] = 0x7f7f7f7f;

        g_pattern = NULL;
        CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
        CHECK(st.bursts == 2);
        CHECK(count_files(dir, "burst") == 2);
        free(g_big);
        g_big = NULL;
    }
    rm_dir(dir);

    /* --- monitor: a quiet line writes nothing at all --------------------- */
    memcpy(dir, dir_template, sizeof(dir));
    CHECK(mkdtemp(dir) != NULL);
    opts.monitor_dir = dir;
    g_pattern = "........";
    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    CHECK(st.bursts == 0 && st.written == 0 && !st.activity_seen);
    CHECK(count_files(dir, "burst") == 0);
    rm_dir(dir);

    /*
     * --- a reused directory must never overwrite what is still in it -------
     *
     * The directory is the shipper's work queue, so a burst waiting to go, and
     * the sidecar of one already sent, both stay behind. Numbering that
     * restarted at 1 would clobber an unshipped burst and give two different
     * bursts the same name.
     */
    memcpy(dir, dir_template, sizeof(dir));
    CHECK(mkdtemp(dir) != NULL);
    opts.monitor_dir = dir;
    g_pattern = "..x..";
    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    CHECK(st.bursts == 1);
    CHECK(count_files(dir, "burst") == 1);          /* burst-0001.sr */

    /* Second run over the same directory: the first burst must survive. */
    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    CHECK(st.bursts == 1);
    CHECK(count_files(dir, "burst") == 2);          /* 0001 kept, 0002 added */

    /* A shipped burst leaves only its sidecar, and must not be reused either. */
    snprintf(path, sizeof(path), "%s/burst-0002.sr", dir);
    CHECK(unlink(path) == 0);
    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    snprintf(path, sizeof(path), "%s/burst-0003.sr", dir);
    CHECK(stat(path, &fst) == 0 && fst.st_size > 0);
    snprintf(path, sizeof(path), "%s/burst-0002.sr", dir);
    CHECK(stat(path, &fst) != 0);                   /* the tombstone's number is not retaken */
    rm_dir(dir);

    /* --- the declared protocol is recorded, and no --es-url ships nothing -- */
    memcpy(dir, dir_template, sizeof(dir));
    CHECK(mkdtemp(dir) != NULL);
    opts.monitor_dir = dir;
    opts.protocol = "uart";
    opts.decode_channel = 0;
    opts.baud = 115200;
    g_pattern = "..x..";
    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    CHECK(st.bursts == 1);
    CHECK(st.shipped == 0 && st.ship_deferred == 0);    /* no --es-url, no shipper */

    snprintf(path, sizeof(path), "%s/burst-0001.sr.meta.json", dir);
    index = slurp(path, NULL);
    CHECK(index != NULL);
    if (index) {
        CHECK(strstr(index, "\"protocol\": \"uart\"") != NULL);
        CHECK(strstr(index, "\"decode_channel\": 0") != NULL);
        CHECK(strstr(index, "\"baud\": 115200") != NULL);
        free(index);
    }
    opts.protocol = NULL;
    opts.baud = 0;
    rm_dir(dir);

    /* --- continuous with a silence timeout ------------------------------- */
    fd = mkstemps(out, 3);
    CHECK(fd >= 0);
    close(fd);

    memset(&opts, 0, sizeof(opts));
    opts.out_path = out;
    opts.silence_s = 0.004;
    opts.pre_s = 0.001;
    g_pattern = "..x.....";       /* silence after the burst stops the capture */

    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    CHECK(st.activity_seen && st.silence_stopped);
    CHECK(st.samples < 8 * PERIOD_SAMPLES);     /* stopped early */
    CHECK(st.written > 0 && st.written < st.samples);   /* the wait was not written */
    CHECK(stat(out, &fst) == 0 && fst.st_size > 0);
    unlink(out);

    /* --- caps ------------------------------------------------------------ */
    memset(&opts, 0, sizeof(opts));
    opts.out_path = out;
    opts.max_samples = 2 * PERIOD_SAMPLES;
    g_pattern = "xxxxxxxx";
    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    CHECK(st.samples_cap_hit && st.samples == 2 * PERIOD_SAMPLES);
    unlink(out);

    memset(&opts, 0, sizeof(opts));
    opts.out_path = out;
    opts.max_bytes = 3 * PERIOD_SAMPLES;
    g_pattern = "xxxxxxxx";
    CHECK(record_run((PIO)0, &cfg, &opts, &st, &sres) == 0);
    CHECK(st.size_cap_hit && st.written == 3 * PERIOD_SAMPLES);
    unlink(out);

    CHECK_DONE();
}
