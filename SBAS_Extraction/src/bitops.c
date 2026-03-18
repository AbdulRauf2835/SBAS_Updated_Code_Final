#include "bitops.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Helper to read a bit at absolute bit position (0 = MSB of bytes[0]) */
static inline int read_bit(const uint8_t *bytes, size_t bitpos)
{
    size_t byte_idx = bitpos / 8;
    int bit_idx = 7 - (bitpos % 8); // MSB first
    return (bytes[byte_idx] >> bit_idx) & 0x1;
}

uint64_t sbas_bits_get_u64(const uint8_t *bytes, size_t bit_offset, size_t nbits)
{
    if (nbits == 0 || nbits > 64) return 0;
    uint64_t v = 0;
    for (size_t i = 0; i < nbits; ++i) {
        v = (v << 1) | (uint64_t)read_bit(bytes, bit_offset + i);
    }
    return v;
}

int64_t sbas_bits_get_s64(const uint8_t *bytes, size_t bit_offset, size_t nbits)
{
    if (nbits == 0 || nbits > 64) return 0;
    uint64_t u = sbas_bits_get_u64(bytes, bit_offset, nbits);
    /* If sign bit set, sign-extend */
    if (u & (1ULL << (nbits - 1))) {
        uint64_t mask = (~0ULL) << nbits;
        u |= mask;
    }
    return (int64_t)u;
}

