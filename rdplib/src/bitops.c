// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "bitarray.h"

#include <string.h>

int getbit(const uint8_t *bits, uint32_t bit_index)
{
    return (bits[bit_index >> 3] >> (7u - (bit_index & 7u))) & 1u;
}

int bitarray_clearbit(_bitarray_t *bits, uint32_t bit_index)
{
    uint8_t *byte = &bits->bytes[bit_index >> 3];
    uint32_t shift = 7u - (bit_index & 7u);
    int previous = (*byte >> shift) & 1u;

    *byte &= (uint8_t)~(1u << shift);
    return previous;
}

int bitarray_setbit(_bitarray_t *bits, uint32_t bit_index)
{
    uint8_t *byte = &bits->bytes[bit_index >> 3];
    uint32_t shift = 7u - (bit_index & 7u);
    int previous = (*byte >> shift) & 1u;

    *byte |= (uint8_t)(1u << shift);
    return previous;
}

int bitarray_copy(const _bitarray_t *bits, uint8_t *destination, uint32_t first_bit, uint32_t byte_count)
{
    const uint8_t *source = &bits->bytes[first_bit >> 3];
    uint32_t shift = first_bit & 7u;
    uint32_t available = (RDP_BITARRAY_BITS - first_bit) >> 3;
    uint32_t copied = available < byte_count ? available : byte_count;
    uint32_t remaining = byte_count - copied;
    uint32_t i;

    for (i = 0; i < copied; ++i)
    {
        destination[i] = (uint8_t)((source[i] << shift) | (source[i + 1] >> (8u - shift)));
    }

    destination += copied;
    source += copied;

    if (remaining)
    {
        *destination++ = (uint8_t)(*source << shift);
        --remaining;
    }

    if (remaining)
    {
        memset(destination, 0, remaining);
    }

    return 0;
}

void bitarray_clear(_bitarray_t *bits)
{
    memset(bits->bytes, 0, sizeof(bits->bytes));
    bits->zero_sentinel = 0;
}
