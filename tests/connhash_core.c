// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "connhash.h"

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

static void store_native_u16(uint8_t *destination, uint16_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static void store_native_u32(uint8_t *destination, uint32_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static void test_ipv4_identity_and_hash(void)
{
    rdp_connhash_t hash = {64, NULL};
    uint8_t left[16] = {0};
    uint8_t right[16] = {0};
    uint16_t port = UINT16_C(0x1234);
    uint32_t address = UINT32_C(0x89ABCDEF);
    uint32_t mixed = port ^ address ^ (address >> 12) ^ (address >> 24);

    store_native_u16(left, 2);
    store_native_u16(left + 2, port);
    store_native_u32(left + 4, address);
    memcpy(right, left, sizeof(right));

    assert(sockaddr_cmp(left, right) == 0);
    assert(connhash_hash(&hash, left) == (uint16_t)(mixed & 63u));

    right[15] = 0xFF;
    assert(sockaddr_cmp(left, right) == 0);
    right[4] ^= 1;
    assert(sockaddr_cmp(left, right) != 0);
}

static void test_family6_identity_and_hash(void)
{
    rdp_connhash_t hash = {128, NULL};
    uint8_t endpoint[16] = {0};
    uint8_t other[16] = {0};
    uint16_t final_word = UINT16_C(0x4321);
    int32_t mixed = final_word;
    uint32_t index;

    store_native_u16(endpoint, 6);
    for (index = 2; index < 12; ++index)
    {
        endpoint[index] = (uint8_t)(index == 7 ? 0xFE : index * 9u);
        mixed += (int8_t)endpoint[index];
    }
    store_native_u16(endpoint + 12, final_word);
    memcpy(other, endpoint, sizeof(other));

    assert(sockaddr_cmp(endpoint, other) == 0);
    assert(connhash_hash(&hash, endpoint) == ((uint16_t)mixed & 127u));

    other[14] = 1;
    assert(sockaddr_cmp(endpoint, other) == 0);
    other[10] ^= 1;
    assert(sockaddr_cmp(endpoint, other) != 0);
}

static void test_legacy_identity(void)
{
    uint8_t left[16] = {0};
    uint8_t right[16] = {0};

    store_native_u16(left, 0x45);
    left[2] = 7;
    left[3] = 9;
    memcpy(right, left, sizeof(right));
    right[10] = 1;
    assert(sockaddr_cmp(left, right) == 0);
    right[3] ^= 1;
    assert(sockaddr_cmp(left, right) != 0);
}

int main(void)
{
    test_ipv4_identity_and_hash();
    test_family6_identity_and_hash();
    test_legacy_identity();
    return 0;
}
