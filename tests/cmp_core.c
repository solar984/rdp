// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>

#include "cmp.h"

_Static_assert(_Generic(&uint16_cmp, int (*)(const void *, const void *): 1, default: 0), "uint16_cmp signature changed");
_Static_assert(_Generic(&uint8_cmp, int (*)(const void *, const void *): 1, default: 0), "uint8_cmp signature changed");

#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&uint64_cmp, int (*)(const void *, const void *): 1, default: 0), "uint64_cmp signature changed");
_Static_assert(_Generic(&uint32_cmp, int (*)(const void *, const void *): 1, default: 0), "uint32_cmp signature changed");
#endif

static void test_uint8_cmp(void)
{
    const uint8_t zero = 0;
    const uint8_t one = 1;
    const uint8_t maximum = UINT8_MAX;
    const uint8_t below_half = UINT8_C(0x7F);
    const uint8_t half = UINT8_C(0x80);
    const uint8_t above_half = UINT8_C(0x81);

    assert(uint8_cmp(&zero, &zero) == 0);
    assert(uint8_cmp(&one, &zero) == 1);
    assert(uint8_cmp(&zero, &one) == -1);
    assert(uint8_cmp(&zero, &maximum) == 1);
    assert(uint8_cmp(&maximum, &zero) == -1);
    assert(uint8_cmp(&zero, &below_half) == -1);
    assert(uint8_cmp(&zero, &above_half) == 1);
    assert(uint8_cmp(&zero, &half) == -1);
    assert(uint8_cmp(&half, &zero) == -1);
}

static void test_uint16_cmp(void)
{
    const uint16_t zero = 0;
    const uint16_t one = 1;
    const uint16_t maximum = UINT16_MAX;
    const uint16_t below_half = UINT16_C(0x7FFF);
    const uint16_t half = UINT16_C(0x8000);
    const uint16_t above_half = UINT16_C(0x8001);

    assert(uint16_cmp(&zero, &zero) == 0);
    assert(uint16_cmp(&one, &zero) == 1);
    assert(uint16_cmp(&zero, &one) == -1);
    assert(uint16_cmp(&zero, &maximum) == 1);
    assert(uint16_cmp(&maximum, &zero) == -1);
    assert(uint16_cmp(&zero, &below_half) == -1);
    assert(uint16_cmp(&zero, &above_half) == 1);
    assert(uint16_cmp(&zero, &half) == -1);
    assert(uint16_cmp(&half, &zero) == -1);
}

#ifdef RDP_DEAD_CODE
static void test_uint32_cmp(void)
{
    const uint32_t zero = 0;
    const uint32_t one = 1;
    const uint32_t maximum = UINT32_MAX;
    const uint32_t below_half = UINT32_C(0x7FFFFFFF);
    const uint32_t half = UINT32_C(0x80000000);
    const uint32_t above_half = UINT32_C(0x80000001);

    assert(uint32_cmp(&zero, &zero) == 0);
    assert(uint32_cmp(&one, &zero) == 1);
    assert(uint32_cmp(&zero, &one) == -1);
    assert(uint32_cmp(&zero, &maximum) == 1);
    assert(uint32_cmp(&maximum, &zero) == -1);
    assert(uint32_cmp(&zero, &below_half) == -1);
    assert(uint32_cmp(&zero, &above_half) == 1);
    assert(uint32_cmp(&zero, &half) == -1);
    assert(uint32_cmp(&half, &zero) == -1);
}

static void test_uint64_cmp(void)
{
    const uint64_t zero = 0;
    const uint64_t one = 1;
    const uint64_t maximum = UINT64_MAX;
    const uint64_t below_half = UINT64_C(0x7FFFFFFFFFFFFFFF);
    const uint64_t half = UINT64_C(0x8000000000000000);
    const uint64_t above_half = UINT64_C(0x8000000000000001);

    assert(uint64_cmp(&zero, &zero) == 0);
    assert(uint64_cmp(&one, &zero) == 1);
    assert(uint64_cmp(&zero, &one) == -1);
    assert(uint64_cmp(&zero, &maximum) == 1);
    assert(uint64_cmp(&maximum, &zero) == -1);
    assert(uint64_cmp(&zero, &below_half) == -1);
    assert(uint64_cmp(&zero, &above_half) == 1);
    assert(uint64_cmp(&zero, &half) == -1);
    assert(uint64_cmp(&half, &zero) == -1);
}
#endif

int main(void)
{
    test_uint8_cmp();
    test_uint16_cmp();
#ifdef RDP_DEAD_CODE
    test_uint32_cmp();
    test_uint64_cmp();
#endif
    return 0;
}
