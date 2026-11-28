/*
 * Milestone 4: discover and verify the channel/slot layout of packed FIFO words.
 *
 *   bitprobe --probe [--channels N]      one capture: which word bits toggle,
 *                                        and their chronological order
 *   bitprobe --map [--channels N]        interactive: probe every channel in
 *                                        turn, print a table for src/unpack.c
 *   bitprobe --raw-in FILE --channels N  analyse a file written by pi5la --raw
 *   bitprobe --verify CH [--generator F] unpack with the compiled map and check
 *                                        channel CH is a clean square wave and
 *                                        every other channel is constant
 *
 * For probe/map the generator is locked to the capture clock with a half
 * period of 49 samples. 49 is odd and larger than any samples-per-word count,
 * so edges walk through every slot position and each word holds at most one.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "capture.h"
#include "generator.h"
#include "piolib.h"
#include "unpack.h"
#include "util.h"

#define PROBE_HALF 49

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

typedef struct {
    uint32_t active;            /* bits that are sometimes 0 and sometimes 1 */
    unsigned n_active;
    uint8_t order[32];          /* active bits, chronologically first to last */
    uint64_t transition_words;  /* words used for ordering */
    uint64_t contradictions;    /* bit pairs seen in both orders */
    int ordered;                /* order is a consistent total order */
} probe_t;

static void probe_words(const uint32_t *w, size_t n, probe_t *p)
{
    static uint32_t before[32][32];
    uint32_t and_all = 0xffffffffu, or_all = 0;
    unsigned wins[32] = { 0 }, a, b, k;
    size_t i;

    memset(p, 0, sizeof(*p));
    memset(before, 0, sizeof(before));
    for (i = 0; i < n; i++) {
        and_all &= w[i];
        or_all |= w[i];
    }
    p->active = or_all & ~and_all;
    p->n_active = (unsigned)__builtin_popcount(p->active);
    if (p->n_active < 2 || n < 3)
        return;

    for (i = 1; i + 1 < n; i++) {
        uint32_t prev = w[i - 1] & p->active;
        uint32_t cur = w[i] & p->active;
        uint32_t next = w[i + 1] & p->active;
        uint32_t early, late;

        if (cur == 0 || cur == p->active)
            continue;
        if (prev == 0 && next == p->active) {
            early = p->active & ~cur; /* still low */
            late = cur;
        } else if (prev == p->active && next == 0) {
            early = cur;              /* still high */
            late = p->active & ~cur;
        } else {
            continue;
        }
        p->transition_words++;
        for (a = 0; a < 32; a++)
            if (early & (1u << a))
                for (b = 0; b < 32; b++)
                    if (late & (1u << b))
                        before[a][b]++;
    }

    for (a = 0; a < 32; a++) {
        if (!(p->active & (1u << a)))
            continue;
        for (b = 0; b < 32; b++) {
            if (a == b || !(p->active & (1u << b)))
                continue;
            if (before[a][b] > before[b][a])
                wins[a]++;
            if (a < b && before[a][b] && before[b][a])
                p->contradictions++;
        }
    }

    /* Rank n_active-1 wins = earliest. A consistent order uses every rank once. */
    p->ordered = 1;
    for (k = 0; k < p->n_active; k++) {
        unsigned want = p->n_active - 1 - k, found = 0;

        for (a = 0; a < 32; a++) {
            if ((p->active & (1u << a)) && wins[a] == want) {
                if (found++ == 0)
                    p->order[k] = (uint8_t)a;
            }
        }
        if (found != 1)
            p->ordered = 0;
    }
}

static void print_probe(const probe_t *p, unsigned channels)
{
    unsigned k;

    printf("active bits (%u, expected %u):", p->n_active, unpack_samples_per_word(channels));
    for (k = 0; k < 32; k++)
        if (p->active & (1u << k))
            printf(" %u", k);
    printf("\n");
    printf("transition words used: %" PRIu64 ", contradictions: %" PRIu64 "\n",
           p->transition_words, p->contradictions);
    if (p->ordered) {
        printf("chronological order (slot 0 first):");
        for (k = 0; k < p->n_active; k++)
            printf(" %u", p->order[k]);
        printf("\n");
    } else {
        printf("chronological order: NOT DETERMINED\n");
    }
}

static int capture_locked(PIO pio, unsigned base, unsigned channels, clkdiv_t div,
                          uint64_t samples, uint64_t half, uint32_t dma_buf,
                          capture_result_t *res)
{
    capture_config_t cfg = {
        .base_gpio = base,
        .channels = channels,
        .div = div,
        .samples = samples,
        .mode = CAPTURE_DMA,
        .trig = TRIG_NONE,
        .dma_buf_bytes = dma_buf,
        .stop = &g_stop,
    };
    clkdiv_t gdiv = { 0, 0 };
    generator_t gen;
    int ret;

    if (!clkdiv_is_integer(div) || half * div.div_int > 65535) {
        fprintf(stderr, "rate needs an integer divider <= %" PRIu64 " for a locked generator\n",
                65535 / half);
        return -1;
    }
    gdiv.div_int = (uint16_t)(half * div.div_int);
    if (generator_start_div(pio, GENERATOR_DEFAULT_GPIO, gdiv, &gen))
        return -1;
    ret = capture_run(pio, &cfg, res);
    generator_stop(&gen);
    if (ret == 0 && res->stall_events)
        fprintf(stderr, "warning: %" PRIu64 " stall event(s) during capture\n", res->stall_events);
    if (ret == 0 && res->overflow)
        fprintf(stderr, "warning: DMA ring overflow, capture cut short\n");
    return ret;
}

static void usage(FILE *f)
{
    fprintf(f,
        "usage: bitprobe --probe  [--channels N] [--rate R] [--samples N] [--base-gpio G]\n"
        "       bitprobe --map    [--channels N] [--rate R] [--samples N] [--base-gpio G]\n"
        "       bitprobe --raw-in FILE --channels N\n"
        "       bitprobe --verify CH [--generator F] [--channels N] [--rate R] [--samples N]\n"
        "\n"
        "Generator output is GPIO %d. Defaults: 8 channels, 10M rate, 1M samples,\n"
        "base GPIO 2, --verify generator 100k.\n",
        GENERATOR_DEFAULT_GPIO);
}

int main(int argc, char **argv)
{
    enum { M_NONE, M_PROBE, M_MAP, M_RAW, M_VERIFY } mode = M_NONE;
    static const struct option opts[] = {
        { "probe", no_argument, NULL, 'p' },
        { "map", no_argument, NULL, 'M' },
        { "raw-in", required_argument, NULL, 'i' },
        { "verify", required_argument, NULL, 'v' },
        { "channels", required_argument, NULL, 'c' },
        { "rate", required_argument, NULL, 'r' },
        { "samples", required_argument, NULL, 'n' },
        { "base-gpio", required_argument, NULL, 'b' },
        { "generator", required_argument, NULL, 'g' },
        { "dma-buf", required_argument, NULL, 'B' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    uint64_t channels = 8, samples = 1000000, base = 2, verify_ch = 0, u;
    uint64_t dma_buf = CAPTURE_DMA_BUF_DEFAULT;
    double rate = 10e6, gen_hz = 100e3;
    const char *raw_in = NULL;
    struct sigaction sa;
    clkdiv_t div;
    PIO pio;
    int opt, ret = 1;

    while ((opt = getopt_long(argc, argv, "pMi:v:c:r:n:b:g:B:h", opts, NULL)) != -1) {
        switch (opt) {
        case 'p': mode = M_PROBE; break;
        case 'M': mode = M_MAP; break;
        case 'i': mode = M_RAW; raw_in = optarg; break;
        case 'v':
            mode = M_VERIFY;
            if (!parse_si_u64(optarg, &verify_ch))
                goto bad;
            break;
        case 'c':
            if (!parse_si_u64(optarg, &channels) || channels > 8 ||
                !unpack_channels_valid((unsigned)channels))
                goto bad;
            break;
        case 'r':
            if (!parse_si(optarg, &rate))
                goto bad;
            break;
        case 'n':
            if (!parse_si_u64(optarg, &samples) || samples == 0)
                goto bad;
            break;
        case 'b':
            if (!parse_si_u64(optarg, &base) || base >= CAPTURE_GPIO_COUNT)
                goto bad;
            break;
        case 'g':
            if (!parse_si(optarg, &gen_hz) || gen_hz <= 0)
                goto bad;
            break;
        case 'B':
            if (!parse_si_u64(optarg, &u) || u < 4 || u % 4 || u > UINT32_MAX)
                goto bad;
            dma_buf = u;
            break;
        case 'h':
            usage(stdout);
            return 0;
        default:
            goto bad;
        }
    }
    if (mode == M_NONE || optind != argc)
        goto bad;

    if (mode == M_RAW) {
        FILE *f = fopen(raw_in, "rb");
        uint32_t *w;
        long len;
        probe_t p;

        if (!f) {
            fprintf(stderr, "cannot open %s: %s\n", raw_in, strerror(errno));
            return 1;
        }
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        rewind(f);
        w = malloc(len > 0 ? (size_t)len : 1);
        if (!w || fread(w, 1, (size_t)len, f) != (size_t)len) {
            fprintf(stderr, "cannot read %s\n", raw_in);
            fclose(f);
            free(w);
            return 1;
        }
        fclose(f);
        probe_words(w, (size_t)len / 4, &p);
        print_probe(&p, (unsigned)channels);
        free(w);
        return 0;
    }

    if (!clkdiv_for_hz(rate, &div)) {
        fprintf(stderr, "invalid rate\n");
        return 1;
    }
    if (GENERATOR_DEFAULT_GPIO >= base && GENERATOR_DEFAULT_GPIO < base + channels) {
        fprintf(stderr, "generator GPIO overlaps capture pins\n");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    pio = pio_open(0);
    if (PIO_IS_ERR(pio)) {
        fprintf(stderr, "pio_open(0) failed: %s\n", strerror(-PIO_ERR_VAL(pio)));
        return 1;
    }

    if (mode == M_PROBE) {
        capture_result_t res = { 0 };
        probe_t p;

        printf("jumper GPIO %d -> one capture pin; capture %u ch @ %.0f Sa/s\n",
               GENERATOR_DEFAULT_GPIO, (unsigned)channels, clkdiv_hz(div));
        if (capture_locked(pio, (unsigned)base, (unsigned)channels, div, samples, PROBE_HALF,
                           (uint32_t)dma_buf, &res) == 0) {
            probe_words(res.words, (size_t)(res.bytes / 4), &p);
            print_probe(&p, (unsigned)channels);
            ret = 0;
        }
        capture_result_free(&res);
    } else if (mode == M_MAP) {
        unsigned spw = unpack_samples_per_word((unsigned)channels), k, s;
        uint8_t map[32][8];
        uint32_t seen = 0;
        int ok = 1;

        for (k = 0; k < channels && !g_stop; k++) {
            capture_result_t res = { 0 };
            probe_t p;
            char line[16];

            printf("\nChannel %u: jumper GPIO %d -> GPIO %u only, leave the other capture "
                   "pins undriven. Press Enter...",
                   k, GENERATOR_DEFAULT_GPIO, (unsigned)base + k);
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) {
                ok = 0;
                break;
            }
            if (capture_locked(pio, (unsigned)base, (unsigned)channels, div, samples,
                               PROBE_HALF, (uint32_t)dma_buf, &res)) {
                capture_result_free(&res);
                ok = 0;
                break;
            }
            probe_words(res.words, (size_t)(res.bytes / 4), &p);
            capture_result_free(&res);
            print_probe(&p, (unsigned)channels);

            if (p.n_active != spw || !p.ordered) {
                printf("channel %u: unexpected result, map incomplete\n", k);
                ok = 0;
                continue;
            }
            if (seen & p.active) {
                printf("channel %u: bits overlap an earlier channel\n", k);
                ok = 0;
            }
            seen |= p.active;
            for (s = 0; s < spw; s++)
                map[s][k] = p.order[s];
        }

        if (ok && !g_stop) {
            printf("\n/* measured by tools/bitprobe --map, %u channels, shift_right = true */\n",
                   (unsigned)channels);
            printf("static const uint8_t map_%uch[%u][%u] = {\n", (unsigned)channels, spw,
                   (unsigned)channels);
            for (s = 0; s < spw; s++) {
                printf("    {");
                for (k = 0; k < channels; k++)
                    printf(" %2u%s", map[s][k], k + 1 < channels ? "," : " ");
                printf("},\n");
            }
            printf("};\n");
            ret = 0;
        } else {
            printf("\nmap not produced\n");
        }
    } else if (mode == M_VERIFY) {
        double half_f = clkdiv_hz(div) / (2.0 * gen_hz);
        uint64_t half = (uint64_t)llround(half_f), got;
        capture_result_t res = { 0 };
        uint8_t *buf;
        unsigned k;
        int pass = 1;

        if (verify_ch >= channels || fabs(half_f - (double)half) > 1e-9 || half == 0) {
            fprintf(stderr, "--verify needs CH < channels and rate / (2 * generator) to be an "
                            "integer (got %.4f)\n",
                    half_f);
            pio_close(pio);
            return 1;
        }
        printf("jumper GPIO %d -> GPIO %u; expecting %" PRIu64 " samples high / %" PRIu64
               " low on channel %u only\n",
               GENERATOR_DEFAULT_GPIO, (unsigned)(base + verify_ch), half, half,
               (unsigned)verify_ch);
        if (capture_locked(pio, (unsigned)base, (unsigned)channels, div, samples, half,
                           (uint32_t)dma_buf, &res)) {
            capture_result_free(&res);
            pio_close(pio);
            return 1;
        }
        got = (res.bytes / 4) * unpack_samples_per_word((unsigned)channels);
        if (got > samples)
            got = samples;
        buf = malloc(got ? got : 1);
        if (!buf) {
            capture_result_free(&res);
            pio_close(pio);
            return 1;
        }
        unpack_words(res.words, (size_t)(res.bytes / 4), (unsigned)channels, buf, got);
        for (k = 0; k < channels; k++) {
            run_stats_t st;

            analyze_runs(buf, got, k, k == verify_ch ? half : 0, &st);
            printf("ch%u: transitions=%" PRIu64 " runs=%" PRIu64 " violations=%" PRIu64
                   " run min/max=%" PRIu64 "/%" PRIu64 "\n",
                   k, st.transitions, st.runs, st.violations, st.min_run, st.max_run);
            if (k == verify_ch ? (st.runs == 0 || st.violations != 0) : st.transitions != 0)
                pass = 0;
        }
        if (res.stall_events || res.overflow)
            pass = 0;
        printf("%s\n", pass ? "PASS" : "FAIL");
        free(buf);
        capture_result_free(&res);
        ret = pass ? 0 : 2;
    }

    pio_close(pio);
    return ret;

bad:
    usage(stderr);
    return 1;
}
