#ifndef BITOPS_H
#define BITOPS_H

#include <stdint.h>
#include <stddef.h>
#include "sbas_message_structs.h"

/* Extract unsigned bits up to 64 bits. Returns 0 if nbits==0 or nbits>64. */
uint64_t sbas_bits_get_u64(const uint8_t *bytes, size_t bit_offset, size_t nbits);

/* Extract signed bits (two's complement) up to 64 bits. Returns sign-extended int64_t. */
int64_t sbas_bits_get_s64(const uint8_t *bytes, size_t bit_offset, size_t nbits);

#endif /* BITOPS_H */
