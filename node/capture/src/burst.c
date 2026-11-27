#include "burst.h"

#include <string.h>

void burst_init(burst_t *b, uint64_t pre_samples, uint64_t post_samples, uint64_t gap_samples)
{
    memset(b, 0, sizeof(*b));
    b->pre_samples = pre_samples;
    b->post_samples = post_samples;
    b->gap_samples = gap_samples;
}

void burst_feed(burst_t *b, uint64_t n_samples, bool changed, uint64_t first_change,
                uint64_t last_change, burst_action_t *out)
{
    uint64_t base = b->total;
    uint64_t end = base + n_samples;

    memset(out, 0, sizeof(*out));
    b->total = end;

    if (changed)
        b->last_change = base + last_change;

    if (!b->recording) {
        if (!changed)
            return;                       /* still quiet: the caller keeps its pre-roll */

        /* Start `pre` samples before the first change; the caller clamps this
         * to what its ring actually still holds. */
        out->begin = true;
        out->first_edge = base + first_change;
        out->start = out->first_edge;
        if (out->start > b->pre_samples)
            out->start -= b->pre_samples;
        else
            out->start = 0;
        b->recording = true;
        b->burst_start = out->start;
        b->first_edge = out->first_edge;

        out->write = true;
        out->write_from = out->start;
        out->write_to = end;
    } else {
        out->write = true;
        out->write_from = base;
        out->write_to = end;
    }

    /* A burst ends once the line has been quiet for `gap`, which can happen in
     * the same period the burst started in. */
    if (end - b->last_change >= b->gap_samples) {
        out->end = true;
        out->end_at = b->last_change + b->post_samples;
        if (out->end_at > end)
            out->end_at = end;
        b->recording = false;
    }
}

bool burst_flush(burst_t *b, uint64_t *end_at)
{
    if (!b->recording)
        return false;
    *end_at = b->last_change + b->post_samples;
    if (*end_at > b->total)
        *end_at = b->total;
    b->recording = false;
    return true;
}
