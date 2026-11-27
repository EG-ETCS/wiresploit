#include "unpack.h"

/*
 * Channel/slot -> word bit map, indexed [slot][channel].
 *
 * With sm_config_set_in_shift(shift_right = true, autopush, 32), each
 * `in pins, N` shifts the ISR right by N and inserts the pins at the top with
 * pin order preserved, so after 32/N samples the oldest sample occupies bits
 * 0..N-1 and channel k of slot s lands at bit s*N + k.
 *
 * 8 channels: MEASURED on a Pi 5 (Milestone 4, 2026-09-16). tools/bitprobe
 * --probe with the generator jumpered to each of GPIO 2..9 in turn found
 * channel k active in bits k, k+8, k+16, k+24 in that chronological order,
 * matching the table below. The 1/2/4-channel tables follow the same rule but
 * have not been measured.
 */
static const uint8_t map_8ch[4][8] = {
    {  0,  1,  2,  3,  4,  5,  6,  7 },
    {  8,  9, 10, 11, 12, 13, 14, 15 },
    { 16, 17, 18, 19, 20, 21, 22, 23 },
    { 24, 25, 26, 27, 28, 29, 30, 31 },
};

static const uint8_t map_4ch[8][4] = {
    {  0,  1,  2,  3 }, {  4,  5,  6,  7 }, {  8,  9, 10, 11 }, { 12, 13, 14, 15 },
    { 16, 17, 18, 19 }, { 20, 21, 22, 23 }, { 24, 25, 26, 27 }, { 28, 29, 30, 31 },
};

static const uint8_t map_2ch[16][2] = {
    {  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 }, {  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
    { 16, 17 }, { 18, 19 }, { 20, 21 }, { 22, 23 }, { 24, 25 }, { 26, 27 }, { 28, 29 }, { 30, 31 },
};

static const uint8_t map_1ch[32][1] = {
    {  0 }, {  1 }, {  2 }, {  3 }, {  4 }, {  5 }, {  6 }, {  7 },
    {  8 }, {  9 }, { 10 }, { 11 }, { 12 }, { 13 }, { 14 }, { 15 },
    { 16 }, { 17 }, { 18 }, { 19 }, { 20 }, { 21 }, { 22 }, { 23 },
    { 24 }, { 25 }, { 26 }, { 27 }, { 28 }, { 29 }, { 30 }, { 31 },
};

unsigned unpack_bitpos(unsigned channels, unsigned slot, unsigned ch)
{
    switch (channels) {
    case 8: return map_8ch[slot][ch];
    case 4: return map_4ch[slot][ch];
    case 2: return map_2ch[slot][ch];
    default: return map_1ch[slot][ch];
    }
}

void unpack_words(const uint32_t *words, size_t n_words, unsigned channels,
                  uint8_t *out, size_t samples)
{
    unsigned spw = unpack_samples_per_word(channels);
    uint8_t pos[32][8];
    size_t w, o = 0;
    unsigned s, k;

    for (s = 0; s < spw; s++)
        for (k = 0; k < channels; k++)
            pos[s][k] = (uint8_t)unpack_bitpos(channels, s, k);

    for (w = 0; w < n_words && o < samples; w++) {
        uint32_t word = words[w];

        for (s = 0; s < spw && o < samples; s++) {
            uint8_t v = 0;

            for (k = 0; k < channels; k++)
                v |= (uint8_t)(((word >> pos[s][k]) & 1u) << k);
            out[o++] = v;
        }
    }
}
