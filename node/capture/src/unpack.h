/**
 * @file unpack.h
 * @brief Unpacking 32-bit PIO FIFO words into one byte per sample.
 */
#ifndef PI5LA_UNPACK_H
#define PI5LA_UNPACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Samples packed into one 32-bit FIFO word for a channel count (1, 2, 4, 8).
 *
 * @param channels Channel count.
 * @return 32 / channels.
 */
static inline unsigned unpack_samples_per_word(unsigned channels)
{
    return 32u / channels;
}

/**
 * Check a channel count.
 *
 * @param channels Channel count.
 * @return True for 1, 2, 4 or 8.
 */
static inline bool unpack_channels_valid(unsigned channels)
{
    return channels == 1 || channels == 2 || channels == 4 || channels == 8;
}

/**
 * Bit position inside the FIFO word of channel `ch` in the `slot`-th sample.
 *
 * Slot 0 is the chronologically first sample. Backed by the compiled-in map
 * in unpack.c.
 *
 * @param channels Channel count.
 * @param slot     Sample slot within the word.
 * @param ch       Channel.
 * @return Bit position, 0..31.
 */
unsigned unpack_bitpos(unsigned channels, unsigned slot, unsigned ch);

/**
 * Convert packed FIFO words into one byte per sample, channel N in bit N.
 *
 * Writes exactly `samples` bytes; `n_words` must cover them.
 *
 * @param words    Packed FIFO words.
 * @param n_words  Number of words.
 * @param channels Channel count.
 * @param out      Receives `samples` bytes.
 * @param samples  Number of samples to write.
 */
void unpack_words(const uint32_t *words, size_t n_words, unsigned channels,
                  uint8_t *out, size_t samples);

#endif
