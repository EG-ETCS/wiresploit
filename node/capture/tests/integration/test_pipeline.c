/*
 * Monitor-mode pipeline on the committed samples: pack -> unpack -> idle scan
 * -> burst state machine, the same steps src/record.c runs on each piece of a
 * DMA period, and the burst boundaries compared with expected/.
 *
 *   test_pipeline SAMPLES_DIR EXPECTED_DIR
 *
 * Every file in EXPECTED_DIR ending in .bursts is one case:
 *
 *   # comment
 *   sample two_bursts.sr      the .sr in SAMPLES_DIR
 *   period_words 256          words fed per burst_feed() call
 *   pre 100                   burst_init() lengths, in samples
 *   post 200
 *   gap 1992
 *   burst 1900 2000 2304      start, first edge, end: one line per burst
 *
 * A case with no "burst" line expects no burst at all. To add a case, add a
 * file; nothing here changes.
 *
 * The samples hold one byte per sample. They are packed back into FIFO words
 * with the compiled channel map, so the idle scan sees words exactly as the
 * PIO delivers them, and the unpacked bytes must match the sample again.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zip.h>

#include "burst.h"
#include "check.h"
#include "idle.h"
#include "unpack.h"

#define MAX_BURSTS 64
#define PATH_LEN 1024

typedef struct {
    uint64_t start, first_edge, end;
} span_t;

typedef struct {
    char sample[256];
    uint64_t period_words, pre, post, gap;
    span_t bursts[MAX_BURSTS];
    unsigned n_bursts;
} case_t;

static int parse_case(const char *path, case_t *c)
{
    char line[512];
    FILE *f = fopen(path, "r");
    int ok = 1;

    memset(c, 0, sizeof(*c));
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        char key[32];
        span_t s;

        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "sample %255s", c->sample) == 1)
            continue;
        if (sscanf(line, "burst %" SCNu64 " %" SCNu64 " %" SCNu64, &s.start, &s.first_edge,
                   &s.end) == 3) {
            if (c->n_bursts == MAX_BURSTS)
                ok = 0;
            else
                c->bursts[c->n_bursts++] = s;
            continue;
        }
        if (sscanf(line, "%31s", key) == 1) {
            uint64_t *dst = !strcmp(key, "period_words") ? &c->period_words
                          : !strcmp(key, "pre")          ? &c->pre
                          : !strcmp(key, "post")         ? &c->post
                          : !strcmp(key, "gap")          ? &c->gap
                                                         : NULL;

            if (dst && sscanf(line, "%*s %" SCNu64, dst) == 1)
                continue;
        }
        fprintf(stderr, "%s: cannot parse: %s", path, line);
        ok = 0;
    }
    fclose(f);
    if (!c->sample[0] || !c->period_words || !c->gap) {
        fprintf(stderr, "%s: needs sample, period_words and gap\n", path);
        ok = 0;
    }
    return ok ? 0 : -1;
}

/* logic-1 and the channel count from metadata. */
static uint8_t *load_sr(const char *path, size_t *n, unsigned *channels)
{
    zip_t *za = zip_open(path, ZIP_RDONLY, NULL);
    char meta[2048];
    const char *probes;
    uint8_t *buf = NULL;
    zip_stat_t st;
    zip_file_t *zf;
    zip_int64_t got;

    if (!za) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    zf = zip_fopen(za, "metadata", 0);
    got = zf ? zip_fread(zf, meta, sizeof(meta) - 1) : -1;
    if (zf)
        zip_fclose(zf);
    if (got < 0)
        goto out;
    meta[got] = '\0';
    probes = strstr(meta, "total probes=");
    if (!probes || sscanf(probes, "total probes=%u", channels) != 1 ||
        !unpack_channels_valid(*channels))
        goto out;

    if (zip_stat(za, "logic-1", 0, &st) || !(st.valid & ZIP_STAT_SIZE))
        goto out;
    buf = malloc(st.size ? (size_t)st.size : 1);
    zf = zip_fopen(za, "logic-1", 0);
    if (!buf || !zf || zip_fread(zf, buf, st.size) != (zip_int64_t)st.size) {
        free(buf);
        buf = NULL;
    }
    if (zf)
        zip_fclose(zf);
    *n = (size_t)st.size;
out:
    if (!buf)
        fprintf(stderr, "%s: not a readable 1-byte-per-sample .sr\n", path);
    zip_discard(za);
    return buf;
}

/* One byte per sample -> FIFO words, the inverse of unpack_words(). */
static uint32_t *pack(const uint8_t *s, size_t n, unsigned channels, size_t *n_words)
{
    unsigned spw = unpack_samples_per_word(channels), slot, k;
    uint32_t *w;
    size_t i;

    *n_words = n / spw;
    w = calloc(*n_words ? *n_words : 1, sizeof(*w));
    if (!w)
        return NULL;
    for (i = 0; i < *n_words * spw; i++) {
        slot = (unsigned)(i % spw);
        for (k = 0; k < channels; k++)
            if (s[i] & (1u << k))
                w[i / spw] |= 1u << unpack_bitpos(channels, slot, k);
    }
    return w;
}

/*
 * Feed the words period by period, as record.c's process_chunk() does, and
 * collect the bursts. Also checks that the writes the state machine asks for
 * are contiguous and that each burst ends inside what was written.
 */
static unsigned run_case(const case_t *c, const uint32_t *words, size_t n_words,
                         unsigned channels, span_t *out)
{
    unsigned spw = unpack_samples_per_word(channels), n_out = 0;
    uint64_t cursor = 0;
    idle_scan_t scan;
    burst_t b;
    size_t done = 0;
    uint64_t end_at;

    idle_scan_init(&scan, channels);
    burst_init(&b, c->pre, c->post, c->gap);
    while (done < n_words) {
        size_t chunk = n_words - done, cb = 0, ce = 0;
        burst_action_t a;
        bool changed;

        if (chunk > c->period_words)
            chunk = (size_t)c->period_words;
        changed = idle_scan_range(&scan, words + done, chunk, &cb, &ce);
        burst_feed(&b, (uint64_t)chunk * spw, changed, (uint64_t)cb * spw,
                   (uint64_t)ce * spw, &a);
        if (a.begin) {
            CHECK(n_out < MAX_BURSTS);
            if (n_out == MAX_BURSTS)
                return n_out;
            out[n_out].start = a.start;
            out[n_out].first_edge = a.first_edge;
            out[n_out].end = 0;
            cursor = a.start;
            CHECK(a.write && a.write_from == a.start);
        }
        if (a.write) {
            CHECK(a.write_from == cursor);      /* no hole, no overlap */
            CHECK(a.write_to > a.write_from);
            cursor = a.write_to;
        }
        if (a.end) {
            CHECK(a.end_at <= cursor);
            out[n_out++].end = a.end_at;
        }
        done += chunk;
    }
    if (burst_flush(&b, &end_at))
        out[n_out++].end = end_at;
    return n_out;
}

static void print_spans(const char *label, const span_t *s, unsigned n)
{
    unsigned i;

    fprintf(stderr, "  %s: %u burst(s)\n", label, n);
    for (i = 0; i < n; i++)
        fprintf(stderr, "    burst %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", s[i].start,
                s[i].first_edge, s[i].end);
}

static void test_case(const char *samples_dir, const char *case_path)
{
    char path[PATH_LEN];
    span_t got[MAX_BURSTS];
    unsigned channels = 0, n_got, i;
    size_t n = 0, n_words = 0;
    uint8_t *s, *back;
    uint32_t *words;
    case_t c;
    int same;

    if (parse_case(case_path, &c)) {
        CHECK(!"expected file parses");
        return;
    }
    snprintf(path, sizeof(path), "%s/%s", samples_dir, c.sample);
    s = load_sr(path, &n, &channels);
    if (!s) {
        CHECK(!"sample loads");
        return;
    }
    CHECK(n % unpack_samples_per_word(channels) == 0);   /* whole words only */
    words = pack(s, n, channels, &n_words);
    back = malloc(n ? n : 1);
    CHECK(words && back);
    if (!words || !back)
        goto out;

    /* unpack is the inverse of pack: the samples come back unchanged. */
    unpack_words(words, n_words, channels, back, n);
    CHECK(!memcmp(back, s, n));

    n_got = run_case(&c, words, n_words, channels, got);
    same = n_got == c.n_bursts;
    for (i = 0; same && i < n_got; i++)
        same = got[i].start == c.bursts[i].start &&
               got[i].first_edge == c.bursts[i].first_edge && got[i].end == c.bursts[i].end;
    if (!same) {
        fprintf(stderr, "%s: burst boundaries differ\n", case_path);
        print_spans("expected", c.bursts, c.n_bursts);
        print_spans("got", got, n_got);
    }
    CHECK(same);
    printf("%s: %u burst(s)%s\n", case_path, n_got, same ? "" : " MISMATCH");
out:
    free(s);
    free(back);
    free(words);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int main(int argc, char **argv)
{
    char *names[256];
    unsigned n = 0, i;
    struct dirent *e;
    DIR *d;

    if (argc != 3) {
        fprintf(stderr, "usage: test_pipeline SAMPLES_DIR EXPECTED_DIR\n");
        return 1;
    }
    d = opendir(argv[2]);
    if (!d) {
        fprintf(stderr, "cannot open %s\n", argv[2]);
        return 1;
    }
    while ((e = readdir(d)) && n < 256) {
        size_t len = strlen(e->d_name);

        if (len > 7 && !strcmp(e->d_name + len - 7, ".bursts"))
            names[n++] = strdup(e->d_name);
    }
    closedir(d);
    CHECK(n > 0);                               /* an empty folder is a broken setup */
    qsort(names, n, sizeof(names[0]), by_name);

    for (i = 0; i < n; i++) {
        char path[PATH_LEN];

        snprintf(path, sizeof(path), "%s/%s", argv[2], names[i]);
        test_case(argv[1], path);
        free(names[i]);
    }
    CHECK_DONE();
}
