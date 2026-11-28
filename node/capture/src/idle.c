#include "idle.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "unpack.h"

void idle_scan_init(idle_scan_t *s, unsigned channels)
{
    unsigned level, slot, k;

    memset(s, 0, sizeof(*s));
    s->channels = channels;
    s->spw = unpack_samples_per_word(channels);
    for (level = 0; level < (1u << channels); level++)
        for (slot = 0; slot < s->spw; slot++)
            for (k = 0; k < channels; k++)
                if (level & (1u << k))
                    s->rep[level] |= 1u << unpack_bitpos(channels, slot, k);
    for (k = 0; k < channels; k++)
        s->last_pos[k] = (uint8_t)unpack_bitpos(channels, s->spw - 1, k);
}

static uint8_t last_level(const idle_scan_t *s, uint32_t w)
{
    uint8_t level = 0;
    unsigned k;

    for (k = 0; k < s->channels; k++)
        level |= (uint8_t)(((w >> s->last_pos[k]) & 1u) << k);
    return level;
}

bool idle_scan_words(idle_scan_t *s, const uint32_t *words, size_t n, size_t *change_end)
{
    size_t begin;

    return idle_scan_range(s, words, n, &begin, change_end);
}

bool idle_scan_range(idle_scan_t *s, const uint32_t *words, size_t n, size_t *change_begin,
                     size_t *change_end)
{
    bool changed = false;
    size_t i;

    if (n && !s->have_level) {
        /* The first word sets the reference level, so it only counts as a
         * change if it isn't constant. */
        s->level = last_level(s, words[0]);
        s->have_level = true;
    }
    for (i = 0; i < n; i++) {
        if (words[i] == s->rep[s->level])
            continue;
        s->level = last_level(s, words[i]);
        if (!changed) {
            *change_begin = i;
            changed = true;
        }
        *change_end = i + 1;
    }
    return changed;
}

bool parse_duration(const char *str, double *seconds)
{
    char *end;
    double v;

    if (!str || !*str)
        return false;
    v = strtod(str, &end);
    if (end == str)
        return false;
    if (!strcmp(end, "ms"))
        v /= 1e3;
    else if (!strcmp(end, "m"))
        v *= 60;
    else if (strcmp(end, "s") && *end)
        return false;
    if (!isfinite(v) || v <= 0)
        return false;
    *seconds = v;
    return true;
}
