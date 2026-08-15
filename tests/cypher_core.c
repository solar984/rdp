// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>
#include <string.h>

#include "cypher.h"

void xor(char *dst, char *src);

_Static_assert(_Generic(&rdp_cypher, void (*)(void *, const char *): 1, default: 0), "rdp_cypher signature changed");
_Static_assert(_Generic(&rdp_encode, void (*)(void *, int): 1, default: 0), "rdp_encode signature changed");
_Static_assert(_Generic(&rdp_decode, void (*)(void *, int): 1, default: 0), "rdp_decode signature changed");
_Static_assert(_Generic(&xor, void (*)(char *, char *): 1, default: 0), "xor signature changed");

static void test_cypher_unaligned(void)
{
    static const char decode[33] = "who's yo daddy?\0who's yo daddy?";
    static const uint8_t expected[8] = { 0xB4, 0x64, 0xE6, 0x26, 0xFE, 0xD5, 0xB2, 0xCA };
    uint8_t buffer[9] = { 0 };

    rdp_cypher(buffer + 1, decode);
    assert(memcmp(buffer + 1, expected, sizeof(expected)) == 0);
}

static void test_xor_unaligned(void)
{
    uint8_t destination[10] = {0, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0};
    uint8_t source[10] = {0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0};
    static const uint8_t expected[8] = {0x11, 0x22, 0x34, 0x48, 0x40, 0x40, 0x30, 0x00};

    xor((char *)destination + 1, (char *)source + 1);
    assert(memcmp(destination + 1, expected, sizeof(expected)) == 0);
}

static void check_encode_vector(const uint8_t *plain, const uint8_t *expected, size_t size)
{
    uint8_t buffer[24];

    assert(size <= sizeof(buffer));
    memcpy(buffer, plain, size);
    rdp_encode(buffer, (int)(size / 8u));
    assert(memcmp(buffer, expected, size) == 0);
    rdp_decode(buffer, (int)(size / 8u));
    assert(memcmp(buffer, plain, size) == 0);
}

static void test_encode_decode(void)
{
    static const uint8_t plain[24] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
    };
    static const uint8_t cipher_1[8] = { 0xEF, 0x45, 0xC8, 0xC0, 0x01, 0x39, 0xBE, 0x69 };
    static const uint8_t cipher_2[16] = {
        0x91, 0xA7, 0x96, 0xC5, 0xA6, 0xE3, 0x78, 0xA3,
        0x61, 0xC5, 0x11, 0x75, 0xF8, 0x4A, 0xB7, 0xB1
    };
    static const uint8_t cipher_3[24] = {
        0xDA, 0x7E, 0xFD, 0x8E, 0x03, 0x9E, 0xD5, 0xCD,
        0xE8, 0x20, 0xF9, 0x69, 0x8F, 0x03, 0xD0, 0x97,
        0x5F, 0xEB, 0x0D, 0x9D, 0xBA, 0x5E, 0x6B, 0xA8
    };

    check_encode_vector(plain, cipher_1, sizeof(cipher_1));
    check_encode_vector(plain, cipher_2, sizeof(cipher_2));
    check_encode_vector(plain, cipher_3, sizeof(cipher_3));

    {
        uint8_t unaligned[sizeof(cipher_2) + 1];

        memcpy(unaligned + 1, plain, sizeof(cipher_2));
        rdp_encode(unaligned + 1, 2);
        assert(memcmp(unaligned + 1, cipher_2, sizeof(cipher_2)) == 0);
        rdp_decode(unaligned + 1, 2);
        assert(memcmp(unaligned + 1, plain, sizeof(cipher_2)) == 0);
    }
}

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
static void test_checked_invalid_arguments(void)
{
    uint8_t unchanged[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint8_t snapshot[sizeof(unchanged)];

    memcpy(snapshot, unchanged, sizeof(snapshot));
    rdp_encode(NULL, 1);
    rdp_decode(NULL, 1);
    rdp_encode(unchanged, 0);
    rdp_decode(unchanged, 0);
    rdp_encode(unchanged, -1);
    rdp_decode(unchanged, -1);
    assert(memcmp(unchanged, snapshot, sizeof(unchanged)) == 0);
}
#endif

int main(void)
{
    test_cypher_unaligned();
    test_xor_unaligned();
    test_encode_decode();
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    test_checked_invalid_arguments();
#endif
    return 0;
}
