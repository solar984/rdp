// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Recovered fixed class fast allocator.
#ifndef RDP_FAST_H
#define RDP_FAST_H

#include <stdint.h>

enum
{
    RDP_FAST_POOLED_CLASS_COUNT = 4,
    RDP_FAST_DIRECT_CLASS = 4,
    RDP_FAST_ALLOCATION_HEADER_SIZE = 8,
    RDP_FAST_TOP_BLOCK_SIZE = 584,
    RDP_FAST_TOP_PAYLOAD_SIZE = RDP_FAST_TOP_BLOCK_SIZE - RDP_FAST_ALLOCATION_HEADER_SIZE
};

// Stored immediately before every returned payload. fast_free trusts
// pool_index without checking that it names a real pool or the direct class.
typedef struct rdp_fast_allocation_header_t
{
    uint32_t pool_index;
    uint32_t payload_size;
} rdp_fast_allocation_header_t;

#ifdef __cplusplus
extern "C"
{
#endif

void fast_malloc_init(uint32_t initial_byte_budget);
void fast_malloc_destroy(void);
void fast_malloc_grow(uint32_t byte_budget);
void *fast_malloc_raw(uint32_t pool_index);
void *fast_malloc(uint32_t payload_size);
void fast_free(void *payload);

#ifdef __cplusplus
}
#endif

#endif /* RDP_FAST_H */
