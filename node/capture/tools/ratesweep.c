/*
 * Milestone 5: rate sweep.
 *
 * For each channel count and PIO clock divider, the generator (GPIO 20) is
 * locked to the capture clock with divider half * d, so every level of the
 * square wave lasts exactly `half` samples. Jumper GPIO 20 to the first
 * capture pin (channel 0). Any run on channel 0 whose length is not `half`
 * is a period violation.
 *
 * CSV status: "ok" (0 violations, no stalls or ring overflow), "violations"
 * (any of those) or "nosignal" (no runs at all: check the jumper).
 * violation_pct is empty on nosignal rows.
 *
 * Only integer dividers are swept: fractional dividers add sampling jitter
 * that would show up as violations unrelated to throughput.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
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

#define MAX_LIST 32

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int parse_list(const char *s, uint64_t *out, int max)
{
    char *copy = strdup(s), *tok, *save = NULL;
    int n = 0;

    for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        if (n == max || !parse_si_u64(tok, &out[n])) {
            free(copy);
            return -1;
        }
        n++;
    }
    free(copy);
    return n;
}

static void usage(FILE *f)
{
    fprintf(f,
        "usage: ratesweep [--out FILE.csv] [--samples N] [--channels 1,2,4,8]\n"
        "                 [--dividers 100,40,20,10,8,5,4,2,1] [--half 25]\n"
        "                 [--base-gpio 2] [--dma-buf BYTES]\n"
        "\n"
        "Jumper GPIO %d to the base capture GPIO (channel 0) before running.\n"
        "sample_rate = 200 MHz / divider.\n",
        GENERATOR_DEFAULT_GPIO);
}

int main(int argc, char **argv)
{
    static const struct option opts[] = {
        { "out", required_argument, NULL, 'o' },
        { "samples", required_argument, NULL, 'n' },
        { "channels", required_argument, NULL, 'c' },
        { "dividers", required_argument, NULL, 'd' },
        { "half", required_argument, NULL, 'H' },
        { "base-gpio", required_argument, NULL, 'b' },
        { "dma-buf", required_argument, NULL, 'B' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    uint64_t chans[MAX_LIST] = { 1, 2, 4, 8 };
    uint64_t divs[MAX_LIST] = { 100, 40, 20, 10, 8, 5, 4, 2, 1 };
    int n_chans = 4, n_divs = 9;
    uint64_t samples = 4000000, half = 25, base = 2, dma_buf = CAPTURE_DMA_BUF_DEFAULT;
    double best[9] = { 0 };
    const char *out_path = "ratesweep.csv";
    struct sigaction sa;
    FILE *csv;
    PIO pio;
    int opt, ci, di, ret = 0;

    while ((opt = getopt_long(argc, argv, "o:n:c:d:H:b:B:h", opts, NULL)) != -1) {
        switch (opt) {
        case 'o':
            out_path = optarg;
            break;
        case 'n':
            if (!parse_si_u64(optarg, &samples) || samples == 0)
                goto bad;
            break;
        case 'c':
            n_chans = parse_list(optarg, chans, MAX_LIST);
            if (n_chans <= 0)
                goto bad;
            for (ci = 0; ci < n_chans; ci++)
                if (chans[ci] > 8 || !unpack_channels_valid((unsigned)chans[ci]))
                    goto bad;
            break;
        case 'd':
            n_divs = parse_list(optarg, divs, MAX_LIST);
            if (n_divs <= 0)
                goto bad;
            for (di = 0; di < n_divs; di++)
                if (divs[di] < 1 || divs[di] > 65535)
                    goto bad;
            break;
        case 'H':
            if (!parse_si_u64(optarg, &half) || half < 1)
                goto bad;
            break;
        case 'b':
            if (!parse_si_u64(optarg, &base) || base >= CAPTURE_GPIO_COUNT)
                goto bad;
            break;
        case 'B':
            if (!parse_si_u64(optarg, &dma_buf) || dma_buf < 4 || dma_buf % 4 ||
                dma_buf > UINT32_MAX)
                goto bad;
            break;
        case 'h':
            usage(stdout);
            return 0;
        default:
            goto bad;
        }
    }
    if (optind != argc)
        goto bad;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    pio = pio_open(0);
    if (PIO_IS_ERR(pio)) {
        fprintf(stderr, "pio_open(0) failed: %s\n", strerror(-PIO_ERR_VAL(pio)));
        return 1;
    }

    csv = fopen(out_path, "w");
    if (!csv) {
        fprintf(stderr, "cannot create %s: %s\n", out_path, strerror(errno));
        pio_close(pio);
        return 1;
    }
    fprintf(csv, "channels,sample_rate,samples_captured,violations,violation_pct,stalls,status\n");

    for (ci = 0; ci < n_chans && !g_stop; ci++) {
        for (di = 0; di < n_divs && !g_stop; di++) {
            unsigned ch = (unsigned)chans[ci];
            capture_config_t cfg = {
                .base_gpio = (unsigned)base,
                .channels = ch,
                .div = { .div_int = (uint16_t)divs[di], .div_frac = 0 },
                .samples = samples,
                .mode = CAPTURE_DMA,
                .trig = TRIG_NONE,
                .dma_buf_bytes = (uint32_t)dma_buf,
                .stop = &g_stop,
            };
            clkdiv_t gdiv = { .div_int = 0, .div_frac = 0 };
            capture_result_t res;
            generator_t gen;
            run_stats_t st;
            uint64_t got;
            uint8_t *buf;
            const char *status;
            char pct_str[32];

            if (half * divs[di] > 65535) {
                fprintf(stderr, "skip ch=%u div=%" PRIu64 ": generator divider %" PRIu64
                                " > 65535 (lower --half)\n",
                        ch, divs[di], half * divs[di]);
                continue;
            }
            if (GENERATOR_DEFAULT_GPIO >= base && GENERATOR_DEFAULT_GPIO < base + ch) {
                fprintf(stderr, "generator GPIO overlaps capture pins\n");
                ret = 1;
                goto done;
            }
            gdiv.div_int = (uint16_t)(half * divs[di]);

            if (generator_start_div(pio, GENERATOR_DEFAULT_GPIO, gdiv, &gen)) {
                ret = 1;
                goto done;
            }
            if (capture_run(pio, &cfg, &res)) {
                generator_stop(&gen);
                capture_result_free(&res);
                ret = 1;
                goto done;
            }
            generator_stop(&gen);

            got = (res.bytes / 4) * unpack_samples_per_word(ch);
            if (got > samples)
                got = samples;
            buf = malloc(got ? got : 1);
            if (!buf) {
                capture_result_free(&res);
                ret = 1;
                goto done;
            }
            unpack_words(res.words, (size_t)(res.bytes / 4), ch, buf, got);
            analyze_runs(buf, got, 0, half, &st);
            free(buf);

            if (st.runs == 0)
                status = "nosignal";
            else if (st.violations || res.stall_events || res.overflow)
                status = "violations";
            else
                status = "ok";

            pct_str[0] = '\0';
            if (st.runs)
                snprintf(pct_str, sizeof(pct_str), "%.6f", 100.0 * st.violations / st.runs);
            fprintf(csv, "%u,%.0f,%" PRIu64 ",%" PRIu64 ",%s,%" PRIu64 ",%s\n", ch, res.rate_hz,
                    got, st.violations, pct_str, res.stall_events, status);
            fflush(csv);

            fprintf(stderr,
                    "ch=%u rate=%.0f samples=%" PRIu64 " runs=%" PRIu64 " violations=%" PRIu64
                    " (%s%%) run min/max=%" PRIu64 "/%" PRIu64 " stalls=%" PRIu64
                    " status=%s%s\n",
                    ch, res.rate_hz, got, st.runs, st.violations, st.runs ? pct_str : "-",
                    st.min_run, st.max_run, res.stall_events, status,
                    res.overflow ? "  DMA RING OVERFLOW"
                                 : (st.runs == 0 ? "  NO SIGNAL: check jumper" : ""));

            if (strcmp(status, "ok") == 0 && res.rate_hz > best[ch])
                best[ch] = res.rate_hz;
            capture_result_free(&res);
        }
    }

done:
    fclose(csv);
    pio_close(pio);

    fprintf(stderr, "\nhighest clean rate (0 violations):\n");
    for (ci = 0; ci < n_chans; ci++) {
        unsigned ch = (unsigned)chans[ci];

        if (best[ch] > 0)
            fprintf(stderr, "  %u ch: %.0f Sa/s\n", ch, best[ch]);
        else
            fprintf(stderr, "  %u ch: none\n", ch);
    }
    fprintf(stderr, "CSV written to %s\n", out_path);
    return ret;

bad:
    usage(stderr);
    return 1;
}
