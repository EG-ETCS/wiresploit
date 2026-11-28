#include <string.h>

#include "check.h"
#include "unpack.h"

/* Pack samples into words using the same compiled map unpack_words reads. */
static void pack(const uint8_t *samples, size_t n, unsigned ch, uint32_t *words)
{
    unsigned spw = unpack_samples_per_word(ch);
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned s = (unsigned)(i % spw), k;

        if (s == 0)
            words[i / spw] = 0;
        for (k = 0; k < ch; k++)
            if (samples[i] & (1u << k))
                words[i / spw] |= 1u << unpack_bitpos(ch, s, k);
    }
}

static void map_is_permutation(unsigned ch)
{
    unsigned spw = unpack_samples_per_word(ch), s, k;
    uint64_t seen = 0;

    for (s = 0; s < spw; s++)
        for (k = 0; k < ch; k++) {
            unsigned b = unpack_bitpos(ch, s, k);

            CHECK(b < 32);
            CHECK(!(seen & (1ull << b)));
            seen |= 1ull << b;
        }
    CHECK(seen == 0xffffffffull);
}

static void roundtrip(unsigned ch)
{
    uint8_t in[103], out[103];
    uint32_t words[103];
    unsigned mask = (1u << ch) - 1;
    size_t i;

    for (i = 0; i < sizeof(in); i++)
        in[i] = (uint8_t)((i * 37u + 11u) & mask);
    pack(in, sizeof(in), ch, words);
    memset(out, 0xaa, sizeof(out));
    unpack_words(words, (sizeof(in) + unpack_samples_per_word(ch) - 1) / unpack_samples_per_word(ch),
                 ch, out, sizeof(out));
    CHECK(!memcmp(in, out, sizeof(in)));
}

int main(void)
{
    uint32_t w = 0x04030201u;
    uint8_t out[4];

    map_is_permutation(1);
    map_is_permutation(2);
    map_is_permutation(4);
    map_is_permutation(8);
    roundtrip(1);
    roundtrip(2);
    roundtrip(4);
    roundtrip(8);

    /* Layout measured on hardware in M4: slot 0 in the low byte, channel k at bit k. */
    unpack_words(&w, 1, 8, out, 4);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 4);

    CHECK(unpack_channels_valid(8) && !unpack_channels_valid(3));
    CHECK_DONE();
}
