/*
 * Builds the synthetic captures in tests/integration/samples/.
 *
 *   make_samples OUT_DIR          write every sample into OUT_DIR
 *   make_samples --check DIR      rebuild them in a temporary folder and
 *                                 compare with the ones in DIR
 *
 * Every sample is 8 channels at 1 MSa/s, probes named GPIO2..GPIO9 as pi5la
 * names them, written with the same srzip / srwriter code pi5la uses. Each
 * .sr gets a .sr.meta.json sidecar shaped like the one pi5la writes for a
 * burst, with a fixed start time, so sr2es gives the same documents every run.
 *
 * The idle level is 0x01: channel 0 high (an idle UART line), the rest low.
 *
 *   uart_hello.sr   4096 samples. "hello" as UART 8N1 at 115200 baud on
 *                   channel 0, back to back, start bit at sample 1000.
 *                   Declared protocol "uart", decode channel 0.
 *   two_bursts.sr   8192 samples. Two 100-sample low pulses on channel 0:
 *                   samples 2000..2099 and 4200..4299. Declared "raw".
 *   all_idle.sr     4096 samples, nothing moves. Declared "uart".
 *
 * Why --check compares contents, not bytes: a zip member stores the time it
 * was written, so two runs never give byte-identical .sr files. --check
 * compares each zip member's name and data, and the sidecars byte for byte.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zip.h>

#include "srwriter.h"
#include "srzip.h"

#define RATE_HZ 1000000u
#define CHANNELS 8u
#define BASE_GPIO 2u
#define IDLE 0x01u
#define UART_BAUD 115200u
#define START_NS UINT64_C(1767225600000000000)    /* 2026-01-01T00:00:00Z */
#define PATH_LEN 1024

typedef struct {
    const char *name;           /* file name in the samples folder */
    size_t samples;
    const char *protocol;       /* declared protocol, recorded in the sidecar */
    int uart;                   /* 1: decode channel 0 at UART_BAUD */
    int streamed;               /* 1: written with srwriter, in pieces */
    void (*build)(uint8_t *s, size_t n);
} sample_t;

static const char *const probe_names[CHANNELS] = {
    "GPIO2", "GPIO3", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO8", "GPIO9",
};

static void fill(uint8_t *s, size_t from, size_t to, uint8_t v)
{
    memset(s + from, v, to - from);
}

/* First sample of UART bit k, for a frame stream starting at `start`. Rounded
   to the nearest sample: bit time is 8.68 samples at 1 MSa/s and 115200 baud. */
static size_t uart_bit_edge(size_t start, unsigned k)
{
    return start + (size_t)(((uint64_t)k * RATE_HZ * 2 + UART_BAUD) / (2 * UART_BAUD));
}

/* 8N1, LSB first, frames back to back, on channel 0. */
static void uart_text(uint8_t *s, size_t start, const char *text)
{
    size_t len = strlen(text), i;
    unsigned k = 0;

    for (i = 0; i < len; i++) {
        unsigned bit;

        for (bit = 0; bit < 10; bit++, k++) {
            int level = bit == 0 ? 0 : bit == 9 ? 1 : ((unsigned char)text[i] >> (bit - 1)) & 1;

            fill(s, uart_bit_edge(start, k), uart_bit_edge(start, k + 1),
                 (uint8_t)(level ? IDLE : IDLE & ~1u));
        }
    }
}

static void build_uart_hello(uint8_t *s, size_t n)
{
    fill(s, 0, n, IDLE);
    uart_text(s, 1000, "hello");
}

static void build_two_bursts(uint8_t *s, size_t n)
{
    fill(s, 0, n, IDLE);
    fill(s, 2000, 2100, IDLE & ~1u);
    fill(s, 4200, 4300, IDLE & ~1u);
}

static void build_all_idle(uint8_t *s, size_t n)
{
    fill(s, 0, n, IDLE);
}

static const sample_t samples[] = {
    { "uart_hello.sr", 4096, "uart", 1, 0, build_uart_hello },
    { "two_bursts.sr", 8192, "raw", 0, 1, build_two_bursts },
    { "all_idle.sr", 4096, "uart", 1, 0, build_all_idle },
};
#define N_SAMPLES (sizeof(samples) / sizeof(samples[0]))

/* ---------- writing ---------- */

static int write_streamed(const char *path, const uint8_t *s, size_t n)
{
    srwriter_t w;
    size_t done = 0;

    if (srwriter_open(&w, path))
        return -1;
    while (done < n) {                  /* in pieces, the way record.c appends */
        size_t chunk = n - done < 1000 ? n - done : 1000;

        if (srwriter_write(&w, s + done, chunk)) {
            srwriter_abort(&w);
            return -1;
        }
        done += chunk;
    }
    return srwriter_finish(&w, RATE_HZ, CHANNELS, probe_names);
}

static int write_sidecar(const char *path, const sample_t *smp, const uint8_t *s,
                         uint64_t start_ns)
{
    uint64_t edges = 0, first_edge = 0;
    uint8_t mask = 0;
    unsigned k;
    size_t i;
    int first = 1;
    FILE *f;

    for (i = 1; i < smp->samples; i++) {
        if (s[i] != s[i - 1]) {
            if (!edges)
                first_edge = i;
            mask |= (uint8_t)(s[i] ^ s[i - 1]);
            edges++;
        }
    }

    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "make_samples: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(f, "{\n  \"samplerate_hz\": %u,\n  \"channels\": %u,\n  \"base_gpio\": %u,\n",
            RATE_HZ, CHANNELS, BASE_GPIO);
    fprintf(f, "  \"burst\": \"%s\",\n  \"capture_started\": %" PRIu64 ".%06" PRIu64 ",\n",
            smp->name, start_ns / 1000000000u, start_ns % 1000000000u / 1000u);
    fprintf(f, "  \"start_sample\": 0,\n  \"samples\": %zu,\n", smp->samples);
    fprintf(f, "  \"start_time\": %" PRIu64 ".%06" PRIu64 ",\n  \"start_time_ns\": %" PRIu64
               ",\n",
            start_ns / 1000000000u, start_ns % 1000000000u / 1000u, start_ns);
    if (edges)
        fprintf(f, "  \"first_edge_sample\": %" PRIu64 ",\n"
                   "  \"first_edge_time_ns\": %" PRIu64 ",\n",
                first_edge, start_ns + first_edge * (1000000000u / RATE_HZ));
    fprintf(f, "  \"duration_s\": %.6f,\n  \"gap_before_s\": 0.000000,\n",
            (double)smp->samples / RATE_HZ);
    fprintf(f, "  \"protocol\": \"%s\",\n", smp->protocol);
    if (smp->uart)
        fprintf(f, "  \"decode_channel\": 0,\n  \"baud\": %u,\n", UART_BAUD);
    fprintf(f, "  \"edges\": %" PRIu64 ",\n  \"active_channels\": [", edges);
    for (k = 0; k < CHANNELS; k++) {
        if (mask & (1u << k)) {
            fprintf(f, "%s%u", first ? "" : ", ", k);
            first = 0;
        }
    }
    fprintf(f, "],\n");
    fprintf(f, "  \"clock_synchronised\": true,\n  \"clock_max_error_us\": 0,\n"
               "  \"clock_drift_ns\": 0,\n  \"clock_source\": \"CLOCK_REALTIME\",\n");
    fprintf(f, "  \"written_by\": \"make_samples\"\n}\n");
    if (fclose(f)) {
        fprintf(stderr, "make_samples: writing %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int write_all(const char *dir)
{
    unsigned i;

    for (i = 0; i < N_SAMPLES; i++) {
        const sample_t *smp = &samples[i];
        char sr[PATH_LEN], meta[PATH_LEN];
        uint8_t *s = malloc(smp->samples);
        int ret;

        if (!s) {
            fprintf(stderr, "make_samples: out of memory\n");
            return -1;
        }
        smp->build(s, smp->samples);
        snprintf(sr, sizeof(sr), "%s/%s", dir, smp->name);
        snprintf(meta, sizeof(meta), "%s/%s.meta.json", dir, smp->name);
        ret = smp->streamed ? write_streamed(sr, s, smp->samples)
                            : srzip_write(sr, s, smp->samples, RATE_HZ, CHANNELS, probe_names);
        /* Samples are spread one second apart, so each has its own timestamp. */
        if (!ret)
            ret = write_sidecar(meta, smp, s, START_NS + (uint64_t)i * 1000000000u);
        free(s);
        if (ret)
            return -1;
    }
    return 0;
}

/* ---------- checking ---------- */

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
    buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf)
        *len = (size_t)n;
    return buf;
}

static uint8_t *zip_member(zip_t *za, zip_uint64_t idx, size_t *len)
{
    zip_stat_t st;
    zip_file_t *zf;
    uint8_t *buf;

    if (zip_stat_index(za, idx, 0, &st) || !(st.valid & ZIP_STAT_SIZE))
        return NULL;
    buf = malloc(st.size ? (size_t)st.size : 1);
    zf = zip_fopen_index(za, idx, 0);
    if (!buf || !zf || zip_fread(zf, buf, st.size) != (zip_int64_t)st.size) {
        if (zf)
            zip_fclose(zf);
        free(buf);
        return NULL;
    }
    zip_fclose(zf);
    *len = (size_t)st.size;
    return buf;
}

/* Same member names, in the same order, with the same contents. */
static int same_zip(const char *a, const char *b)
{
    zip_t *za = zip_open(a, ZIP_RDONLY, NULL), *zb = zip_open(b, ZIP_RDONLY, NULL);
    zip_int64_t n, i;
    int same = 0;

    if (!za || !zb) {
        fprintf(stderr, "make_samples: cannot open %s\n", !za ? a : b);
        goto out;
    }
    n = zip_get_num_entries(za, 0);
    if (n != zip_get_num_entries(zb, 0)) {
        fprintf(stderr, "make_samples: %s: member count differs\n", b);
        goto out;
    }
    for (i = 0; i < n; i++) {
        const char *name = zip_get_name(za, (zip_uint64_t)i, 0);
        const char *other = zip_get_name(zb, (zip_uint64_t)i, 0);
        size_t la = 0, lb = 0;
        uint8_t *da, *db;
        int eq;

        if (!name || !other || strcmp(name, other)) {
            fprintf(stderr, "make_samples: %s: member %" PRId64 " differs in name\n", b, i);
            goto out;
        }
        da = zip_member(za, (zip_uint64_t)i, &la);
        db = zip_member(zb, (zip_uint64_t)i, &lb);
        eq = da && db && la == lb && !memcmp(da, db, la);
        free(da);
        free(db);
        if (!eq) {
            fprintf(stderr, "make_samples: %s: member %s differs\n", b, name);
            goto out;
        }
    }
    same = 1;
out:
    if (za)
        zip_discard(za);
    if (zb)
        zip_discard(zb);
    return same;
}

static int same_file(const char *a, const char *b)
{
    size_t la = 0, lb = 0;
    char *da = slurp(a, &la), *db = slurp(b, &lb);
    int same = da && db && la == lb && !memcmp(da, db, la);

    if (!same)
        fprintf(stderr, "make_samples: %s differs from a fresh build\n", b);
    free(da);
    free(db);
    return same;
}

static int check(const char *dir)
{
    const char *tmp = getenv("TMPDIR");
    char work[PATH_LEN / 2];
    unsigned i;
    int ok = 1;

    snprintf(work, sizeof(work), "%s/make_samples.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(work)) {
        fprintf(stderr, "make_samples: mkdtemp: %s\n", strerror(errno));
        return 1;
    }
    if (write_all(work))
        ok = 0;
    for (i = 0; ok && i < N_SAMPLES; i++) {
        char a[PATH_LEN], b[PATH_LEN];

        snprintf(a, sizeof(a), "%s/%s", work, samples[i].name);
        snprintf(b, sizeof(b), "%s/%s", dir, samples[i].name);
        if (!same_zip(a, b))
            ok = 0;
        snprintf(a, sizeof(a), "%s/%s.meta.json", work, samples[i].name);
        snprintf(b, sizeof(b), "%s/%s.meta.json", dir, samples[i].name);
        if (!same_file(a, b))
            ok = 0;
    }
    for (i = 0; i < N_SAMPLES; i++) {
        char p[PATH_LEN];

        snprintf(p, sizeof(p), "%s/%s", work, samples[i].name);
        unlink(p);
        snprintf(p, sizeof(p), "%s/%s.meta.json", work, samples[i].name);
        unlink(p);
    }
    rmdir(work);

    if (!ok) {
        fprintf(stderr, "make_samples: the committed samples are stale; rebuild them with\n"
                        "  make_samples %s\n", dir);
        return 1;
    }
    printf("make_samples: %u samples in %s match a fresh build\n", (unsigned)N_SAMPLES, dir);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--check"))
        return check(argv[2]);
    if (argc == 2 && argv[1][0] != '-') {
        if (mkdir(argv[1], 0755) && errno != EEXIST) {
            fprintf(stderr, "make_samples: cannot create %s: %s\n", argv[1], strerror(errno));
            return 1;
        }
        if (write_all(argv[1]))
            return 1;
        printf("make_samples: wrote %u samples to %s\n", (unsigned)N_SAMPLES, argv[1]);
        return 0;
    }
    fprintf(stderr, "usage: make_samples OUT_DIR\n       make_samples --check DIR\n");
    return 1;
}
