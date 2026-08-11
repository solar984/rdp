// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// CRC and block transformation used around connected RDP datagrams.
//
// These routines operate on wire bytes. Their behavior is independent of the
// host byte order; the PPC client explicitly converts the same little endian
// block words used directly by the Intel and Windows builds.
#ifndef RDP_FRAMING_H
#define RDP_FRAMING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Computes the reflected CRC 32 used by connected RDP datagrams.
uint32_t rdp_crc(uint32_t seed, const void *data, uint32_t length);

// Applies the client's 4 round Feistel core to an 8 byte block.
void rdp_cypher(void *block, const uint32_t round_keys[8]);

// Decodes 1 or more backward chained 8 byte blocks in place.
void rdp_decode(void *data, int block_count);

// Encodes 1 or more backward chained 8 byte blocks in place.
void rdp_encode(void *data, int block_count);

#ifdef __cplusplus
}
#endif

#endif /* RDP_FRAMING_H */
