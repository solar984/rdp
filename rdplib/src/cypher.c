// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "framing.h"

#include <stddef.h>

// These are the exact client tables, not regenerated stock DES tables. All
// 512 entries and both schedules are byte identical across EQMac PPC, EQMac
// Intel, and TAKP Windows after accounting for object file byte order.
static const uint32_t rdp_sp1[64] = {0x01010400u, 0x00000000u, 0x00010000u, 0x01010404u, 0x01010004u, 0x00010404u, 0x00000004u, 0x00010000u, 0x00000400u, 0x01010400u, 0x01010404u,
                                     0x00000400u, 0x01000404u, 0x01010004u, 0x01000000u, 0x00000004u, 0x00000404u, 0x01000400u, 0x01000400u, 0x00010400u, 0x00010400u, 0x01010000u,
                                     0x01010000u, 0x01000404u, 0x00010004u, 0x01000004u, 0x01000004u, 0x00010004u, 0x00000000u, 0x00000404u, 0x00010404u, 0x10000000u, 0x00010000u,
                                     0x01010404u, 0x00000004u, 0x01010000u, 0x01010400u, 0x01000000u, 0x01000000u, 0x00000400u, 0x01010004u, 0x00010000u, 0x00010400u, 0x01000004u,
                                     0x00000400u, 0x00000004u, 0x01000404u, 0x00010404u, 0x01010404u, 0x00010004u, 0x01010000u, 0x01000404u, 0x01000004u, 0x00000404u, 0x00010404u,
                                     0x01010400u, 0x00000404u, 0x01000400u, 0x01000400u, 0x00000000u, 0x00010004u, 0x00010400u, 0x00000000u, 0x01010004u};

static const uint32_t rdp_sp2[64] = {0x80108020u, 0x80008000u, 0x00008000u, 0x00108020u, 0x00100000u, 0x00000020u, 0x80100020u, 0x80008020u, 0x80000020u, 0x80108020u, 0x80108000u,
                                     0x80000000u, 0x80008000u, 0x00100000u, 0x00000020u, 0x80100020u, 0x00108000u, 0x00100020u, 0x80008020u, 0x00000001u, 0x80000000u, 0x00080000u,
                                     0x00108020u, 0x80100000u, 0x00100020u, 0x80000020u, 0x00000000u, 0x00108000u, 0x00008020u, 0x80108000u, 0x80100000u, 0x00008020u, 0x00000000u,
                                     0x00108020u, 0x80100020u, 0x00100000u, 0x80008020u, 0x80100000u, 0x80108000u, 0x00008000u, 0x80100000u, 0x80008000u, 0x00000020u, 0x80108020u,
                                     0x00108020u, 0x00000020u, 0x00008000u, 0x80000000u, 0x00008020u, 0x80108000u, 0x00100000u, 0x80000020u, 0x00100020u, 0x80008020u, 0x80000020u,
                                     0x00100020u, 0x00108000u, 0x00000000u, 0x80008000u, 0x00008020u, 0x80000000u, 0x80100020u, 0x80108020u, 0x00108000u};

static const uint32_t rdp_sp3[64] = {0x00000208u, 0x08020200u, 0x00000000u, 0x08020008u, 0x08000200u, 0x00000000u, 0x00020208u, 0x08000200u, 0x00020008u, 0x08000008u, 0x08000008u,
                                     0x00020000u, 0x08020208u, 0x00020008u, 0x08020000u, 0x00000208u, 0x08000000u, 0x00000008u, 0x08022000u, 0x00000200u, 0x00020200u, 0x08020000u,
                                     0x08020008u, 0x00020208u, 0x08000208u, 0x00020200u, 0x00020000u, 0x08000208u, 0x00000008u, 0x80020208u, 0x00000200u, 0x08000000u, 0x08020200u,
                                     0x08000000u, 0x00020008u, 0x00000208u, 0x00020000u, 0x08020200u, 0x08000200u, 0x00000000u, 0x00000200u, 0x00020008u, 0x08020208u, 0x08000200u,
                                     0x08000008u, 0x00000200u, 0x00000000u, 0x08020008u, 0x08000208u, 0x00020000u, 0x08000000u, 0x08020208u, 0x00000008u, 0x00020208u, 0x00020200u,
                                     0x08000008u, 0x08020000u, 0x08000208u, 0x00000208u, 0x08020000u, 0x00020208u, 0x00000008u, 0x08020008u, 0x00020200u};

static const uint32_t rdp_sp4[64] = {0x00802001u, 0x00002081u, 0x00002081u, 0x00000080u, 0x00802080u, 0x00800081u, 0x00800001u, 0x00002001u, 0x00000000u, 0x00802000u, 0x00802000u,
                                     0x00802081u, 0x00000081u, 0x00000000u, 0x00800080u, 0x00800001u, 0x00000001u, 0x00002000u, 0x00800000u, 0x00802001u, 0x00000080u, 0x00800000u,
                                     0x00002001u, 0x00002080u, 0x00800081u, 0x00000001u, 0x00002080u, 0x00800080u, 0x00002000u, 0x00802080u, 0x00802081u, 0x00000081u, 0x00800080u,
                                     0x00800001u, 0x00802000u, 0x00802081u, 0x00000081u, 0x00000000u, 0x00000000u, 0x00802000u, 0x00002080u, 0x00800080u, 0x00800081u, 0x00000001u,
                                     0x00802001u, 0x00002081u, 0x00002081u, 0x00000080u, 0x00802081u, 0x00000081u, 0x00000001u, 0x00002001u, 0x00800001u, 0x00002001u, 0x00802080u,
                                     0x00800081u, 0x00002001u, 0x00002080u, 0x00800000u, 0x00802001u, 0x00000080u, 0x00800000u, 0x00002000u, 0x00802080u};

static const uint32_t rdp_sp5[64] = {0x00000100u, 0x02080100u, 0x02080000u, 0x42000100u, 0x00080000u, 0x00000100u, 0x40000000u, 0x02080000u, 0x40080100u, 0x00080000u, 0x02000100u,
                                     0x40080100u, 0x42000100u, 0x42080000u, 0x00080100u, 0x40000000u, 0x02000000u, 0x40080000u, 0x40080000u, 0x00000000u, 0x40000100u, 0x42080100u,
                                     0x42080100u, 0x02000100u, 0x42080000u, 0x40000100u, 0x00000000u, 0x42000000u, 0x02080100u, 0x02000000u, 0x42000000u, 0x00080100u, 0x00080000u,
                                     0x42000100u, 0x00001000u, 0x02000000u, 0x40000000u, 0x02080000u, 0x42000100u, 0x40080100u, 0x02000100u, 0x40000000u, 0x42080000u, 0x02080100u,
                                     0x40080100u, 0x00000100u, 0x02000000u, 0x42080000u, 0x42080100u, 0x00080100u, 0x42000000u, 0x42080100u, 0x02080000u, 0x00000000u, 0x40080000u,
                                     0x42000000u, 0x00080100u, 0x02000100u, 0x40000100u, 0x00080000u, 0x00000000u, 0x40080000u, 0x02080100u, 0x40000100u};

static const uint32_t rdp_sp6[64] = {0x20000010u, 0x20400000u, 0x00004000u, 0x20404010u, 0x20400000u, 0x00000010u, 0x20404010u, 0x00400000u, 0x20004000u, 0x00404010u, 0x00400000u,
                                     0x20000010u, 0x00400010u, 0x20004000u, 0x20000000u, 0x00004010u, 0x00000000u, 0x00400010u, 0x20004010u, 0x00004000u, 0x00404000u, 0x20004010u,
                                     0x00000010u, 0x20400010u, 0x20400010u, 0x00000000u, 0x00404010u, 0x20404000u, 0x00004010u, 0x00404000u, 0x20404000u, 0x20000000u, 0x20004000u,
                                     0x00000010u, 0x20400010u, 0x00404000u, 0x20404010u, 0x00400000u, 0x00004010u, 0x20000010u, 0x00400000u, 0x20004000u, 0x20000000u, 0x00004010u,
                                     0x20000010u, 0x20404010u, 0x00404000u, 0x20400000u, 0x00404010u, 0x20404000u, 0x00000000u, 0x20400010u, 0x00000010u, 0x00004000u, 0x20400000u,
                                     0x00404010u, 0x00004000u, 0x00400010u, 0x20004010u, 0x00000000u, 0x20404000u, 0x20000000u, 0x00400010u, 0x20004010u};

static const uint32_t rdp_sp7[64] = {0x00200000u, 0x04200002u, 0x04000802u, 0x00000000u, 0x00000800u, 0x04000802u, 0x00200802u, 0x04200800u, 0x40200802u, 0x00200000u, 0x00000000u,
                                     0x04000002u, 0x00000002u, 0x04000000u, 0x04200002u, 0x00000802u, 0x04000800u, 0x00200802u, 0x00200002u, 0x04000800u, 0x04000002u, 0x04200000u,
                                     0x04200800u, 0x00200002u, 0x04200000u, 0x00000800u, 0x00000802u, 0x04200802u, 0x00200800u, 0x00000002u, 0x04000000u, 0x00200800u, 0x04000000u,
                                     0x00200800u, 0x00200000u, 0x04000802u, 0x04000802u, 0x04200002u, 0x04200002u, 0x00000002u, 0x00200002u, 0x04000000u, 0x04000800u, 0x00200000u,
                                     0x04200800u, 0x00000802u, 0x00200802u, 0x04200800u, 0x00000802u, 0x04000002u, 0x04200802u, 0x04200000u, 0x00200800u, 0x00000000u, 0x00000002u,
                                     0x04200802u, 0x00000000u, 0x00200802u, 0x04200000u, 0x00000800u, 0x04000002u, 0x04000800u, 0x00000800u, 0x00200002u};

static const uint32_t rdp_sp8[64] = {0x10001040u, 0x00001000u, 0x00040000u, 0x10041040u, 0x10000000u, 0x10001040u, 0x00000040u, 0x10000000u, 0x00040040u, 0x10040000u, 0x10041040u,
                                     0x00041000u, 0x10041000u, 0x00041040u, 0x00001000u, 0x00000040u, 0x10040000u, 0x10000040u, 0x10001000u, 0x00001040u, 0x00041000u, 0x00040040u,
                                     0x10040040u, 0x10041000u, 0x00001040u, 0x00000000u, 0x00000000u, 0x10040040u, 0x10000040u, 0x10001000u, 0x00041040u, 0x00040000u, 0x00041040u,
                                     0x00040000u, 0x10041000u, 0x00001000u, 0x00000040u, 0x10040040u, 0x00001000u, 0x00041040u, 0x10001000u, 0x00000040u, 0x10000040u, 0x10040000u,
                                     0x10040040u, 0x10000000u, 0x00040000u, 0x10001040u, 0x00000000u, 0x10041040u, 0x00040040u, 0x10000040u, 0x10040000u, 0x10001000u, 0x10001040u,
                                     0x00000000u, 0x10041040u, 0x00041000u, 0x00041000u, 0x00001040u, 0x00001040u, 0x00040040u, 0x10000000u, 0x10041000u};

static const uint32_t rdp_decode_keys[8] = {0x276F6877u, 0x6F792073u, 0x64616420u, 0x003F7964u, 0x276F6877u, 0x6F792073u, 0x64616420u, 0x003F7964u};

static const uint32_t rdp_encode_keys[8] = {0x64616420u, 0x003F7964u, 0x276F6877u, 0x6F792073u, 0x64616420u, 0x003F7964u, 0x276F6877u, 0x6F792073u};

static uint32_t load_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void store_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t rotate_right_4(uint32_t value)
{
    return (value >> 4) | (value << 28);
}

// Expands a half block with the 2 precomputed client round words.
static uint32_t rdp_feistel(uint32_t half, uint32_t rotated_key, uint32_t direct_key)
{
    uint32_t work = rotate_right_4(half) ^ rotated_key;
    uint32_t result = rdp_sp7[work & 0x3Fu] | rdp_sp5[(work >> 8) & 0x3Fu] | rdp_sp3[(work >> 16) & 0x3Fu] | rdp_sp1[(work >> 24) & 0x3Fu];

    work = half ^ direct_key;
    return result | rdp_sp8[work & 0x3Fu] | rdp_sp6[(work >> 8) & 0x3Fu] | rdp_sp4[(work >> 16) & 0x3Fu] | rdp_sp2[(work >> 24) & 0x3Fu];
}

void rdp_cypher(void *block, const uint32_t round_keys[8])
{
    uint8_t *bytes = (uint8_t *)block;
    uint32_t left = load_le32(bytes);
    uint32_t right = load_le32(bytes + 4);
    uint32_t round;

    for (round = 0; round < 8; round += 4)
    {
        left ^= rdp_feistel(right, round_keys[round], round_keys[round + 1]);
        right ^= rdp_feistel(left, round_keys[round + 2], round_keys[round + 3]);
    }

    // The client writes the final Feistel halves in swapped order.
    store_le32(bytes, right);
    store_le32(bytes + 4, left);
}

void rdp_decode(void *data, int block_count)
{
    uint8_t *block = (uint8_t *)data;
    int block_index;

    // Original callers guarantee at least 1 complete block. Returning here
    // preserves memory safety for the portable interface without changing a
    // valid client call.
    if (block == NULL || block_count <= 0)
    {
        return;
    }

    for (block_index = 0; block_index + 1 < block_count; ++block_index)
    {
        uint32_t left;
        uint32_t right;

        rdp_cypher(block, rdp_decode_keys);
        left = load_le32(block) ^ load_le32(block + 8);
        right = load_le32(block + 4) ^ load_le32(block + 12);
        store_le32(block, left);
        store_le32(block + 4, right);
        block += 8;
    }

    rdp_cypher(block, rdp_decode_keys);
}

void rdp_encode(void *data, int block_count)
{
    uint8_t *block;

    // The original routine underflows when block_count is 0.
    // The caller pads encrypted datagrams first, so 0 cannot reach this path.
    if (data == NULL || block_count <= 0)
    {
        return;
    }

    block = (uint8_t *)data + ((size_t)(block_count - 1) * 8u);
    rdp_cypher(block, rdp_encode_keys);

    while (--block_count > 0)
    {
        uint32_t left;
        uint32_t right;

        block -= 8;
        left = load_le32(block) ^ load_le32(block + 8);
        right = load_le32(block + 4) ^ load_le32(block + 12);
        store_le32(block, left);
        store_le32(block + 4, right);
        rdp_cypher(block, rdp_encode_keys);
    }
}
