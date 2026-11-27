#ifndef PI5LA_UNPACK_H
#define PI5LA_UNPACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Samples packed into one 32-bit FIFO word for a channel count (1, 2, 4, 8). */
static inline unsigned unpack_samples_per_word(unsigned channels)
{
    return 32u / channels;
}

static inline bool unpack_channels_valid(unsigned channels)
{
    return channels == 1 || channels == 2 || channels == 4 || channels == 8;
}

/*
 * Bit position inside the FIFO word of channel `ch` in the `slot`-th sample
 * (slot 0 = chronologically first). Backed by the compiled-in map in unpack.c.
 */
unsigned unpack_bitpos(unsigned channels, unsigned slot, unsigned ch);

/*
 * Convert packed FIFO words into one byte per sample, channel N in bit N.
 * Writes exactly `samples` bytes; `n_words` must cover them.
 */
void unpack_words(const uint32_t *words, size_t n_words, unsigned channels,
                  uint8_t *out, size_t samples);

#endif
