// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// _bitarray_t -- 4096 bits plus the 0 sentinel used by bitarray_copy.
//
// bitarray_copy may read a byte beyond the 512 byte bit store when its
// starting bit is not byte aligned. All 3 clients clear the following
// word, making that boundary read produce 0 instead of unrelated state.
#ifndef RDP_BITARRAY_H
#define RDP_BITARRAY_H

#include <stdint.h>
#define RDP_BITARRAY_BITS 4096u
#define RDP_BITARRAY_BYTES (RDP_BITARRAY_BITS / 8u)

typedef struct _bitarray_t
{
    uint8_t bytes[RDP_BITARRAY_BYTES];
    uint32_t zero_sentinel;
} _bitarray_t;

#ifdef __cplusplus
extern "C"
{
#endif

int getbit(const uint8_t *bits, uint32_t bit_index);
int bitarray_clearbit(_bitarray_t *bits, uint32_t bit_index);
int bitarray_setbit(_bitarray_t *bits, uint32_t bit_index);
int bitarray_copy(const _bitarray_t *bits, uint8_t *destination, uint32_t first_bit, uint32_t byte_count);
void bitarray_clear(_bitarray_t *bits);

#ifdef __cplusplus
}
#endif

#endif /* RDP_BITARRAY_H */
