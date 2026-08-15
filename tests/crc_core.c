// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>

#include "crc.h"

static uint32_t reference_crc(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    uint32_t index;

    crc = ~crc;
    for (index = 0; index < len; ++index)
    {
        uint32_t bit;

        crc ^= buf[index];
        for (bit = 0; bit < 8; ++bit)
        {
            if (crc & 1u)
            {
                crc = (crc >> 1) ^ UINT32_C(0xEDB88320);
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static void test_signature(void)
{
    uint32_t (*function)(uint32_t, const char *, uint32_t) = rdp_crc;

    assert(function == rdp_crc);
}

static void test_known_vectors(void)
{
    static const uint8_t high_bytes[] = { 0x00, 0x7F, 0x80, 0xFF, 0x55, 0xAA };

    assert(rdp_crc(0, NULL, 0) == 0);
    assert(rdp_crc(UINT32_C(0x12345678), NULL, 0) == UINT32_C(0x12345678));
    assert(rdp_crc(0, "123456789", 9) == UINT32_C(0xCBF43926));
    assert(rdp_crc(UINT32_C(0x12345678), "client-rdp", 10) == UINT32_C(0x0A226CF0));
    assert(rdp_crc(0, (const char *)high_bytes, (uint32_t)sizeof(high_bytes)) == UINT32_C(0x684EB78C));
}

static void test_unrolled_loop_boundaries(void)
{
    static const char bytes[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

    assert(rdp_crc(0, bytes, 7) == UINT32_C(0xAD5809F9));
    assert(rdp_crc(0, bytes, 8) == UINT32_C(0x88AA689F));
    assert(rdp_crc(0, bytes, 9) == UINT32_C(0xBCE14302));
    assert(rdp_crc(0, bytes, 16) == UINT32_C(0xCECEE288));
}

static void test_incremental_crc(void)
{
    uint32_t crc = rdp_crc(0, "1234", 4);

    crc = rdp_crc(crc, "56789", 5);
    assert(crc == UINT32_C(0xCBF43926));
}

static void test_against_bitwise_reference(void)
{
    static const uint32_t seeds[] = {
        0,
        UINT32_MAX,
        UINT32_C(0x12345678),
        UINT32_C(0x89ABCDEF),
    };
    uint8_t bytes[256];
    uint32_t byte_value;
    size_t seed_index;

    for (byte_value = 0; byte_value < 256; ++byte_value)
    {
        bytes[byte_value] = (uint8_t)byte_value;
    }

    for (seed_index = 0; seed_index < sizeof(seeds) / sizeof(seeds[0]); ++seed_index)
    {
        uint32_t len;

        for (byte_value = 0; byte_value < 256; ++byte_value)
        {
            uint8_t byte = (uint8_t)byte_value;

            assert(rdp_crc(seeds[seed_index], (const char *)&byte, 1) ==
                   reference_crc(seeds[seed_index], &byte, 1));
        }

        for (len = 0; len <= (uint32_t)sizeof(bytes); ++len)
        {
            assert(rdp_crc(seeds[seed_index], (const char *)bytes, len) ==
                   reference_crc(seeds[seed_index], bytes, len));
        }
    }
}

int main(void)
{
    test_signature();
    test_known_vectors();
    test_unrolled_loop_boundaries();
    test_incremental_crc();
    test_against_bitwise_reference();
    return 0;
}
