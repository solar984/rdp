// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>
#include <string.h>

#include "dpf.h"

#ifdef RDPLIB_DEBUG

_Static_assert(_Generic(&data_format, uint32_t (*)(char *, const uint8_t *, uint32_t): 1, default: 0), "data_format signature");
_Static_assert(_Generic(&dpf, void (*)(uint32_t, const char *, ...): 1, default: 0), "dpf signature");

static void test_empty_and_partial_group(void)
{
    static const uint8_t data[] = {0x00, 0x01, 0xAB};
    char output[16];

    memset(output, 0x5A, sizeof(output));
    assert(data_format(output + 1, data, 0) == 0);
    assert(output[0] == 0x5A && output[1] == '\0' && output[2] == 0x5A);

    assert(data_format(output + 1, data, sizeof(data)) == 6);
    assert(strcmp(output + 1, "0001AB") == 0);
    assert(output[0] == 0x5A && output[8] == 0x5A);
}

static void test_complete_groups(void)
{
    static const uint8_t data[] = {0x00, 0x01, 0xAB, 0xFF, 0x10, 0x2F};
    char output[24];

    memset(output, 0x5A, sizeof(output));
    assert(data_format(output + 1, data, 4) == 9);
    assert(strcmp(output + 1, "0001ABFF ") == 0);
    assert(output[0] == 0x5A && output[11] == 0x5A);

    memset(output, 0x5A, sizeof(output));
    assert(data_format(output + 1, data, sizeof(data)) == 13);
    assert(strcmp(output + 1, "0001ABFF 102F") == 0);
    assert(output[0] == 0x5A && output[15] == 0x5A);
}

static void test_eight_groups_end_in_newline(void)
{
    static const char expected[] = "00010203 04050607 08090A0B 0C0D0E0F 10111213 14151617 18191A1B 1C1D1E1F\n";
    uint8_t data[32];
    char output[80];
    uint32_t index;
    uint32_t chars;

    for (index = 0; index < sizeof(data); ++index)
    {
        data[index] = (uint8_t)index;
    }
    memset(output, 0x5A, sizeof(output));
    chars = data_format(output + 1, data, sizeof(data));
    assert(chars == sizeof(expected) - 1u);
    assert(strcmp(output + 1, expected) == 0);
    assert(output[0] == 0x5A && output[chars + 2u] == 0x5A);
}

#endif

int main(void)
{
#ifdef RDPLIB_DEBUG
    test_empty_and_partial_group();
    test_complete_groups();
    test_eight_groups_end_in_newline();
#endif
    return 0;
}
