// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bitops.h"

_Static_assert(_Generic(&bitarray_clear, void (*)(bitarray_t *): 1, default: 0), "bitarray_clear signature");
_Static_assert(_Generic(&bitarray_copy, void (*)(bitarray_t *, uint8_t *, uint32_t, uint32_t): 1, default: 0), "bitarray_copy signature");
_Static_assert(_Generic(&bitarray_setbit, uint8_t (*)(bitarray_t *, uint32_t): 1, default: 0), "bitarray_setbit signature");
_Static_assert(_Generic(&bitarray_clearbit, uint8_t (*)(bitarray_t *, uint32_t): 1, default: 0), "bitarray_clearbit signature");
_Static_assert(_Generic(&getbit, uint8_t (*)(uint8_t *, uint32_t): 1, default: 0), "getbit signature");
_Static_assert(_Generic(&bitarray_left_shift, void (*)(bitarray_t *, uint32_t): 1, default: 0), "bitarray_left_shift signature");
_Static_assert(_Generic(&bitarray_getbit, uint8_t (*)(bitarray_t *, uint32_t): 1, default: 0), "bitarray_getbit signature");

typedef struct guarded_bitarray_t
{
    uint32_t before;
    bitarray_t bitarray;
    uint32_t after;
} guarded_bitarray_t;

typedef struct guarded_output_t
{
    uint8_t before[4];
    uint8_t bytes[BITARRAY_SIZE];
    uint8_t after[4];
} guarded_output_t;

static void test_layout_and_clear(void)
{
    guarded_bitarray_t guarded;
    bitarray_t *bitarray;
    uint32_t index;

    assert(sizeof(bitarray_t) == 0x204);
    assert(offsetof(bitarray_t, bits) == 0);
    assert(offsetof(bitarray_t, high_byte) == 0x200);

    memset(&guarded, 0xff, sizeof(guarded));
    guarded.before = 0x12345678u;
    guarded.after = 0x9abcdef0u;
    bitarray = &guarded.bitarray;
    bitarray_clear(bitarray);

    for (index = 0; index < BITARRAY_SIZE; ++index)
    {
        assert(bitarray->bits[index] == 0);
    }
    assert(bitarray->high_byte == 0);
    assert(guarded.before == 0x12345678u);
    assert(guarded.after == 0x9abcdef0u);
}

static void test_bit_order_and_previous_values(void)
{
    bitarray_t bitarray;

    bitarray_clear(&bitarray);

    assert(bitarray_setbit(&bitarray, 0) == 0);
    assert(bitarray_setbit(&bitarray, 0) == 1);
    assert(bitarray_setbit(&bitarray, 7) == 0);
    assert(bitarray_setbit(&bitarray, 8) == 0);
    assert(bitarray_setbit(&bitarray, RDP_BITARRAY_BITS - 1u) == 0);

    assert(bitarray.bits[0] == 0x81);
    assert(bitarray.bits[1] == 0x80);
    assert(bitarray.bits[BITARRAY_SIZE - 1u] == 0x01);
    assert(bitarray_getbit(&bitarray, 0) == 1);
    assert(bitarray_getbit(&bitarray, 7) == 1);
    assert(bitarray_getbit(&bitarray, 8) == 1);
    assert(bitarray_getbit(&bitarray, RDP_BITARRAY_BITS - 1u) == 1);

    assert(bitarray_clearbit(&bitarray, 7) == 1);
    assert(bitarray_clearbit(&bitarray, 7) == 0);
    assert(getbit(bitarray.bits, 7) == 0);
}

static void test_copy(void)
{
    bitarray_t bitarray;
    bitarray_t original;
    uint8_t output[4];

    bitarray_clear(&bitarray);
    bitarray.bits[0] = 0xab;
    bitarray.bits[1] = 0xcd;
    bitarray.bits[2] = 0xef;

    memset(output, 0xff, sizeof(output));
    bitarray_copy(&bitarray, output, 0, 3);
    assert(output[0] == 0xab);
    assert(output[1] == 0xcd);
    assert(output[2] == 0xef);
    assert(output[3] == 0xff);

    memset(output, 0xff, sizeof(output));
    bitarray_copy(&bitarray, output, 4, 3);
    assert(output[0] == 0xbc);
    assert(output[1] == 0xde);
    assert(output[2] == 0xf0);
    assert(output[3] == 0xff);

    memset(output, 0xff, sizeof(output));
    bitarray_copy(&bitarray, output, 0, 0);
    assert(output[0] == 0xff);

    memcpy(&original, &bitarray, sizeof(original));
    bitarray_left_shift(&bitarray, 0);
    assert(memcmp(&bitarray, &original, sizeof(bitarray)) == 0);

    memcpy(&bitarray, &original, sizeof(bitarray));
    bitarray_left_shift(&bitarray, 8);
    assert(bitarray.bits[0] == 0xcd);
    assert(bitarray.bits[1] == 0xef);
    assert(bitarray.bits[2] == 0);

    memcpy(&bitarray, &original, sizeof(bitarray));
    bitarray_left_shift(&bitarray, 4);
    assert(bitarray.bits[0] == 0xbc);
    assert(bitarray.bits[1] == 0xde);
    assert(bitarray.bits[2] == 0xf0);
    assert(bitarray.bits[BITARRAY_SIZE - 1u] == 0);
}

static void test_copy_boundary(void)
{
    bitarray_t bitarray;
    uint8_t output[3];

    bitarray_clear(&bitarray);
    bitarray_setbit(&bitarray, RDP_BITARRAY_BITS - 1u);

    memset(output, 0xff, sizeof(output));
    bitarray_copy(&bitarray, output, RDP_BITARRAY_BITS - 1u, sizeof(output));
    assert(output[0] == 0x80);
    assert(output[1] == 0);
    assert(output[2] == 0);

    memset(output, 0xff, sizeof(output));
    bitarray_copy(&bitarray, output, RDP_BITARRAY_BITS, sizeof(output));
    assert(output[0] == 0);
    assert(output[1] == 0);
    assert(output[2] == 0);
}

static void test_exact_fit_and_sentinel(void)
{
    guarded_bitarray_t guarded;
    bitarray_t *bitarray;
    guarded_output_t output;
    uint32_t index;

    memset(&guarded, 0, sizeof(guarded));
    guarded.before = 0x13579bdfu;
    guarded.after = 0x2468ace0u;
    bitarray = &guarded.bitarray;
    bitarray_clear(bitarray);
    for (index = 0; index < BITARRAY_SIZE; ++index)
    {
        bitarray->bits[index] = (uint8_t)(index * 37u + 11u);
    }

    memset(&output, 0xa5, sizeof(output));
    bitarray_copy(bitarray, output.bytes, 0, BITARRAY_SIZE);
    assert(memcmp(output.bytes, bitarray->bits, BITARRAY_SIZE) == 0);
    for (index = 0; index < sizeof(output.before); ++index)
    {
        assert(output.before[index] == 0xa5);
        assert(output.after[index] == 0xa5);
    }
    assert(guarded.before == 0x13579bdfu);
    assert(guarded.after == 0x2468ace0u);

    bitarray->bits[BITARRAY_SIZE - 1u] = 0x5a;
    output.bytes[0] = 0;
    bitarray_copy(bitarray, output.bytes, RDP_BITARRAY_BITS - 8u, 1);
    assert(output.bytes[0] == 0x5a);
    assert(bitarray_getbit(bitarray, RDP_BITARRAY_BITS) == 0);

    memset(bitarray->bits, 0xff, sizeof(bitarray->bits));
    bitarray_left_shift(bitarray, RDP_BITARRAY_BITS);
    for (index = 0; index < BITARRAY_SIZE; ++index)
    {
        assert(bitarray->bits[index] == 0);
    }
    assert(bitarray->high_byte == 0);
    assert(guarded.before == 0x13579bdfu);
    assert(guarded.after == 0x2468ace0u);
}

int main(void)
{
    test_layout_and_clear();
    test_bit_order_and_previous_values();
    test_copy();
    test_copy_boundary();
    test_exact_fit_and_sentinel();
    return 0;
}
