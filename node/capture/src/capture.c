#include "capture.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "la8.pio.h"
#include "la8_trig.pio.h"
#include "dmacyclic.h"
#include "fdebug.h"
#include "unpack.h"


static bool same_program(const la_prog_t *built, const pio_program_t *ref, uint wrap_target,
                         uint wrap)
{
    return built->length == ref->length && built->wrap_target == wrap_target &&
           built->wrap == wrap &&
           !memcmp(built->instr, ref->instructions, ref->length * sizeof(uint16_t));
}

bool capture_selftest_programs(void)
{
    la_prog_t p;

    if (!la_build_program(8, TRIG_NONE, 0, &p) ||
        !same_program(&p, &la8_program, la8_wrap_target, la8_wrap)) {
        fprintf(stderr, "capture: runtime program differs from pioasm la8\n");
        return false;
    }
    if (!la_build_program(8, TRIG_HIGH, 0, &p) ||
        !same_program(&p, &la8_trig_program, la8_trig_wrap_target, la8_trig_wrap)) {
        fprintf(stderr, "capture: runtime program differs from pioasm la8_trig\n");
        return false;
    }
    return true;
}

/* 1 if RXSTALL was set for sm (it is then cleared), 0 if not, -1 on error. */
static int take_rxstall(int fd, uint sm)
{
    uint32_t v;

    if (fdebug_read(fd, &v)) {
        fprintf(stderr, "capture: reading FDEBUG failed: %s\n", strerror(errno));
        return -1;
    }
    if (!(v & FDEBUG_RXSTALL(sm)))
        return 0;
    if (fdebug_clear(fd, FDEBUG_RXSTALL(sm))) {
        fprintf(stderr, "capture: clearing FDEBUG failed: %s\n", strerror(errno));
        return -1;
    }
    return 1;
}

static bool stopped(const capture_config_t *cfg)
{
    return cfg->stop && *cfg->stop;
}

static void finish_estimate(const capture_config_t *cfg, capture_result_t *res)
{
    unsigned spw = unpack_samples_per_word(cfg->channels);
    double got, expected;

    if (res->steady_bytes == 0 || res->steady_s <= 0)
        return;
    got = (double)(res->steady_bytes / 4) * spw;
    expected = res->rate_hz * res->steady_s;
    res->est_lost_samples = llround(expected - got);
    res->est_valid = true;
}

/*
 * Cyclic DMA (see dmacyclic.h): the kernel ring is configured and running
 * before the SM is enabled; each read returns the next completed period.
 * Because DMA never stops draining the FIFO, the SM does not stall when the
 * capture ends, so FDEBUG can be checked after the last period without a
 * false positive.
 *
 * This path fills one buffer of a known size. Unbounded capture, silence
 * timeouts and monitor mode run on capture_stream() instead (src/record.c).
 */
static int run_dma(PIO pio, uint sm, int fd, const capture_config_t *cfg,
                   capture_result_t *res, size_t capacity_bytes)
{
    size_t total = res->n_words * 4;
    size_t period = cfg->dma_buf_bytes;
    size_t got = 0;
    double t_enable, t_first = 0, t_last = 0;
    int stall;

    pio_sm_set_enabled(pio, sm, true);
    t_enable = monotonic_s();

    while (got < total) {
        if (got + period > capacity_bytes) {
            fprintf(stderr, "capture: internal error: buffer too small\n");
            return -1;
        }
        if (dmacyclic_read(fd, sm, (uint32_t)period, (uint8_t *)res->words + got)) {
            if (errno == EOVERFLOW) {
                res->overflow = true;
                break;
            }
            if (stopped(cfg)) {
                res->interrupted = true;
                break;
            }
            fprintf(stderr, "capture: DMA receive failed: %s\n", strerror(errno));
            return -1;
        }
        t_last = monotonic_s();
        if (got == 0)
            t_first = t_last;
        got += period;

        stall = take_rxstall(fd, sm);
        if (stall < 0)
            return -1;
        res->stall_events += (uint64_t)stall;

        if (stopped(cfg)) {
            res->interrupted = true;
            break;
        }
    }

    /*
     * The final period may run past the requested samples: report only the
     * requested bytes, but time the throughput over everything received, since
     * t_first..t_last spans whole periods.
     */
    res->bytes = got < total ? got : total;
    res->total_s = t_last > 0 ? t_last - t_enable : 0;
    if (got > period) {
        res->steady_bytes = got - period;
        res->steady_s = t_last - t_first;
    }
    return 0;
}

/*
 * Milestone 2 baseline: one PCIe round trip per FIFO word. Deliberately slow.
 * RXSTALL is read once, straight after the last word.
 */
static int run_naive(PIO pio, uint sm, int fd, const capture_config_t *cfg,
                     capture_result_t *res)
{
    double t_enable, t_first = 0, t_last = 0;
    size_t i;
    int stall;

    pio_sm_set_enabled(pio, sm, true);
    t_enable = monotonic_s();

    for (i = 0; i < res->n_words; i++) {
        if (stopped(cfg)) {
            res->interrupted = true;
            break;
        }
        res->words[i] = pio_sm_get_blocking(pio, sm);
        if (i == 0)
            t_first = monotonic_s();
    }
    t_last = monotonic_s();

    stall = take_rxstall(fd, sm);
    if (stall < 0)
        return -1;
    res->stall_events = (uint64_t)stall;

    res->bytes = (uint64_t)i * 4;
    res->total_s = t_last - t_enable;
    if (i > 1) {
        res->steady_bytes = (uint64_t)(i - 1) * 4;
        res->steady_s = t_last - t_first;
    }
    return 0;
}

/* PIO + DMA resources for one capture, shared by capture_run and capture_stream. */
typedef struct {
    la_prog_t lp;
    pio_program_t program;
    uint offset;
    int sm;
    int fd;
} capture_hw_t;

/*
 * Validates cfg (except the sample count), loads the program, configures DMA
 * (cyclic, when mode is DMA) and initialises the SM, left disabled.
 * hw must stay at the same address until capture_hw_teardown().
 */
static int capture_hw_setup(PIO pio, const capture_config_t *cfg, capture_hw_t *hw)
{
    pio_sm_config c;
    unsigned k;

    memset(hw, 0, sizeof(*hw));
    hw->sm = -1;
    hw->fd = -1;

    if (!unpack_channels_valid(cfg->channels)) {
        fprintf(stderr, "capture: channels must be 1, 2, 4 or 8\n");
        return -1;
    }
    if (cfg->base_gpio + cfg->channels > CAPTURE_GPIO_COUNT) {
        fprintf(stderr, "capture: GPIO %u..%u is outside GPIO 0..%u\n", cfg->base_gpio,
                cfg->base_gpio + cfg->channels - 1, CAPTURE_GPIO_COUNT - 1);
        return -1;
    }
    if (cfg->mode == CAPTURE_DMA && (cfg->dma_buf_bytes < 4 || cfg->dma_buf_bytes % 4)) {
        fprintf(stderr, "capture: DMA buffer size must be a non-zero multiple of 4\n");
        return -1;
    }
    if (!capture_selftest_programs())
        return -1;
    if (!la_build_program(cfg->channels, cfg->trig, cfg->trig_pin, &hw->lp)) {
        fprintf(stderr, "capture: invalid trigger (pin must be < channels)\n");
        return -1;
    }

    hw->program.instructions = hw->lp.instr;
    hw->program.length = hw->lp.length;
    hw->program.origin = -1;
    hw->program.pio_version = 0;

    hw->fd = fdebug_open();
    if (hw->fd < 0) {
        fprintf(stderr, "capture: opening /dev/pio0 for FDEBUG: %s\n", strerror(errno));
        return -1;
    }

    hw->sm = pio_claim_unused_sm(pio, false);
    if (hw->sm < 0) {
        fprintf(stderr, "capture: no free PIO state machine\n");
        fdebug_close(hw->fd);
        return -1;
    }
    hw->offset = pio_add_program(pio, &hw->program);
    if (hw->offset == PIO_ORIGIN_INVALID) {
        fprintf(stderr, "capture: no PIO program space\n");
        pio_sm_unclaim(pio, hw->sm);
        fdebug_close(hw->fd);
        return -1;
    }

    if (cfg->mode == CAPTURE_DMA &&
        dmacyclic_config(hw->fd, (unsigned)hw->sm, cfg->dma_buf_bytes, CAPTURE_DMA_BUF_COUNT) < 0) {
        fprintf(stderr, "capture: cyclic DMA config (%u x %u bytes) failed: %s\n",
                CAPTURE_DMA_BUF_COUNT, cfg->dma_buf_bytes, strerror(errno));
        pio_remove_program(pio, &hw->program, hw->offset);
        pio_sm_unclaim(pio, hw->sm);
        fdebug_close(hw->fd);
        return -1;
    }

    for (k = 0; k < cfg->channels; k++)
        pio_gpio_init(pio, cfg->base_gpio + k);
    pio_sm_set_consecutive_pindirs(pio, hw->sm, cfg->base_gpio, cfg->channels, false);

    c = pio_get_default_sm_config_for_pio(pio);
    sm_config_set_wrap(&c, hw->offset + hw->lp.wrap_target, hw->offset + hw->lp.wrap);
    sm_config_set_in_pins(&c, cfg->base_gpio);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv_int_frac(&c, cfg->div.div_int, cfg->div.div_frac);
    pio_sm_init(pio, hw->sm, hw->offset, &c);

    if (take_rxstall(hw->fd, (uint)hw->sm) < 0) {
        pio_remove_program(pio, &hw->program, hw->offset);
        pio_sm_unclaim(pio, hw->sm);
        fdebug_close(hw->fd);
        return -1;
    }
    return 0;
}

static void capture_hw_teardown(PIO pio, capture_hw_t *hw)
{
    pio_sm_set_enabled(pio, hw->sm, false);
    pio_remove_program(pio, &hw->program, hw->offset);
    pio_sm_unclaim(pio, hw->sm);
    fdebug_close(hw->fd);
}

int capture_run(PIO pio, const capture_config_t *cfg, capture_result_t *res)
{
    capture_hw_t hw;
    unsigned spw;
    size_t capacity_bytes;
    int ret;

    memset(res, 0, sizeof(*res));

    if (cfg->samples == 0) {
        fprintf(stderr, "capture: sample count must be > 0\n");
        return -1;
    }
    if (!unpack_channels_valid(cfg->channels)) {
        fprintf(stderr, "capture: channels must be 1, 2, 4 or 8\n");
        return -1;
    }
    if (cfg->mode == CAPTURE_DMA && (cfg->dma_buf_bytes < 4 || cfg->dma_buf_bytes % 4)) {
        fprintf(stderr, "capture: DMA buffer size must be a non-zero multiple of 4\n");
        return -1;
    }
    spw = unpack_samples_per_word(cfg->channels);
    res->n_words = (size_t)((cfg->samples + spw - 1) / spw);
    capacity_bytes = res->n_words * 4;
    /* DMA reads whole periods, so round the buffer up to a period multiple. */
    if (cfg->mode == CAPTURE_DMA)
        capacity_bytes = (capacity_bytes + cfg->dma_buf_bytes - 1) / cfg->dma_buf_bytes *
                         cfg->dma_buf_bytes;
    res->words = malloc(capacity_bytes);
    if (!res->words) {
        fprintf(stderr, "capture: cannot allocate %zu words\n", res->n_words);
        return -1;
    }
    res->rate_hz = clkdiv_hz(cfg->div);

    if (capture_hw_setup(pio, cfg, &hw))
        return -1;

    if (cfg->mode == CAPTURE_DMA)
        ret = run_dma(pio, (uint)hw.sm, hw.fd, cfg, res, capacity_bytes);
    else
        ret = run_naive(pio, (uint)hw.sm, hw.fd, cfg, res);

    capture_hw_teardown(pio, &hw);

    if (ret == 0)
        finish_estimate(cfg, res);
    return ret;
}

int capture_stream(PIO pio, const capture_config_t *cfg, capture_sink_t sink, void *ctx,
                   capture_stream_result_t *res)
{
    size_t period = cfg->dma_buf_bytes;
    capture_hw_t hw;
    uint32_t *buf;
    double t_enable, t_last = 0;
    int stall, ret = 0;

    memset(res, 0, sizeof(*res));

    if (cfg->mode != CAPTURE_DMA) {
        fprintf(stderr, "capture: streaming needs DMA mode\n");
        return -1;
    }
    if (period < 4 || period % 4) {
        fprintf(stderr, "capture: DMA buffer size must be a non-zero multiple of 4\n");
        return -1;
    }
    buf = malloc(period);
    if (!buf) {
        fprintf(stderr, "capture: cannot allocate %zu bytes\n", period);
        return -1;
    }
    res->rate_hz = clkdiv_hz(cfg->div);

    if (capture_hw_setup(pio, cfg, &hw)) {
        free(buf);
        return -1;
    }

    pio_sm_set_enabled(pio, (uint)hw.sm, true);
    t_enable = monotonic_s();
    res->start_realtime_ns = realtime_ns();     /* sample 0 in wall-clock time */

    while (!stopped(cfg)) {
        if (dmacyclic_read(hw.fd, (unsigned)hw.sm, (uint32_t)period, buf)) {
            if (errno == EOVERFLOW) {
                res->overflow = true;
                break;
            }
            if (stopped(cfg))
                break;
            fprintf(stderr, "capture: DMA receive failed: %s\n", strerror(errno));
            ret = -1;
            break;
        }
        t_last = monotonic_s();
        res->period_realtime_ns = realtime_ns();    /* pins this period's last sample */
        res->bytes += period;

        stall = take_rxstall(hw.fd, (uint)hw.sm);
        if (stall < 0) {
            ret = -1;
            break;
        }
        if (stall) {
            res->stall_events++;
            fprintf(stderr, "capture: RXSTALL at %.3f s: samples dropped\n", t_last - t_enable);
        }

        if (sink(buf, period / 4, ctx)) {
            res->sink_stopped = true;
            break;
        }
    }
    if (stopped(cfg))
        res->interrupted = true;
    res->total_s = t_last > 0 ? t_last - t_enable : 0;

    capture_hw_teardown(pio, &hw);
    free(buf);
    return ret;
}

void capture_result_free(capture_result_t *res)
{
    free(res->words);
    res->words = NULL;
    res->n_words = 0;
}
