// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "fast.h"

#include <string.h>

#include "container.h"
#include "rdplib_platform.h"

typedef struct rdp_fast_pool_t
{
    uint32_t split_offset;
    uint32_t maximum_payload;
    uint32_t subdivision_count;
    rdp_list_t free_blocks;
    rdplib_platform_mutex_t lock;
} rdp_fast_pool_t;

static uint32_t fast_malloc_initialized_bytes;
static rdp_fast_pool_t fast_malloc_pools[RDP_FAST_POOLED_CLASS_COUNT];
static rdp_list_t fast_malloc_backing_allocations;

void fast_malloc_grow(uint32_t byte_budget)
{
    uint32_t rounded_bytes = ((byte_budget + RDP_FAST_TOP_BLOCK_SIZE - 1u) / RDP_FAST_TOP_BLOCK_SIZE) * RDP_FAST_TOP_BLOCK_SIZE;
    uint8_t *allocation = (uint8_t *)rdplib_platform_malloc((size_t)rounded_bytes + sizeof(rdp_list_link_t));
    uint32_t offset;

    // The clients do not check allocation before clearing and publishing it.
    memset(allocation, 0, (size_t)rounded_bytes + sizeof(rdp_list_link_t));

    ((rdp_list_link_t *)allocation)->value = allocation;
    list_add_tail(&fast_malloc_backing_allocations, (rdp_list_link_t *)allocation);

    for (offset = 0; offset < rounded_bytes; offset += RDP_FAST_TOP_BLOCK_SIZE)
    {
        rdp_list_link_t *block = (rdp_list_link_t *)(allocation + sizeof(rdp_list_link_t) + offset);
        block->value = block;
        list_add_tail(&fast_malloc_pools[RDP_FAST_POOLED_CLASS_COUNT - 1].free_blocks, block);
        ++fast_malloc_pools[RDP_FAST_POOLED_CLASS_COUNT - 1].subdivision_count;
    }
}

void fast_malloc_init(uint32_t initial_byte_budget)
{
    uint32_t block_size = RDP_FAST_TOP_BLOCK_SIZE;
    uint32_t pool_index;

    fast_malloc_initialized_bytes = initial_byte_budget;
    list_init(&fast_malloc_backing_allocations);
    list_create(&fast_malloc_backing_allocations, 0, NULL);

    for (pool_index = RDP_FAST_POOLED_CLASS_COUNT; pool_index-- > 0;)
    {
        rdp_fast_pool_t *pool = &fast_malloc_pools[pool_index];

        list_init(&pool->free_blocks);
        list_create(&pool->free_blocks, 0, NULL);
        rdplib_platform_mutex_init(&pool->lock);
        pool->split_offset = block_size;
        pool->maximum_payload = (block_size - RDP_FAST_ALLOCATION_HEADER_SIZE) & ~UINT32_C(7);
        pool->subdivision_count = 0;
        block_size = (block_size >> 1) & ~UINT32_C(7);
    }

    fast_malloc_grow(initial_byte_budget);
}

void *fast_malloc_raw(uint32_t pool_index)
{
    rdp_fast_pool_t *pool = &fast_malloc_pools[pool_index];
    uint8_t *allocation;

    rdplib_platform_mutex_lock(&pool->lock);
    allocation = (uint8_t *)list_remove_head(&pool->free_blocks);

    if (!allocation)
    {
        if (pool_index == RDP_FAST_POOLED_CLASS_COUNT - 1)
        {
            // Growth after initialization publishes a top size block. The
            // current top pool lock serializes the otherwise unlocked helper.
            fast_malloc_grow(pool->split_offset);
            allocation = (uint8_t *)list_remove_head(&pool->free_blocks);
        }
        else
        {
            rdp_fast_pool_t *larger_pool = &fast_malloc_pools[pool_index + 1];
            rdp_list_link_t *second_half;

            allocation = (uint8_t *)fast_malloc_raw(pool_index + 1);

            // The smaller lock remains held while the larger counter lock is
            // reacquired. All recursive paths use this same low to high order.
            rdplib_platform_mutex_lock(&larger_pool->lock);
            --larger_pool->subdivision_count;
            rdplib_platform_mutex_unlock(&larger_pool->lock);

            pool->subdivision_count += 2;
            second_half = (rdp_list_link_t *)(allocation + pool->split_offset);
            second_half->value = second_half;
            list_add_head(&pool->free_blocks, second_half);
        }
    }

    rdplib_platform_mutex_unlock(&pool->lock);
    return allocation;
}

void *fast_malloc(uint32_t payload_size)
{
    uint32_t pool_index;
    rdp_fast_allocation_header_t *allocation = NULL;

    for (pool_index = 0; pool_index < RDP_FAST_POOLED_CLASS_COUNT; ++pool_index)
    {
        if (payload_size <= fast_malloc_pools[pool_index].maximum_payload)
        {
            allocation = (rdp_fast_allocation_header_t *)fast_malloc_raw(pool_index);
            break;
        }
    }

    if (pool_index == RDP_FAST_DIRECT_CLASS)
    {
        allocation = (rdp_fast_allocation_header_t *)rdplib_platform_malloc((size_t)payload_size + RDP_FAST_ALLOCATION_HEADER_SIZE);
    }

    if (!allocation)
    {
        return NULL;
    }

    allocation->pool_index = pool_index;
    allocation->payload_size = pool_index == RDP_FAST_DIRECT_CLASS ? payload_size : fast_malloc_pools[pool_index].maximum_payload;
    return allocation + 1;
}

void fast_free(void *payload)
{
    rdp_fast_allocation_header_t *allocation = (rdp_fast_allocation_header_t *)payload - 1;
    uint32_t pool_index = allocation->pool_index;

    if (pool_index == RDP_FAST_DIRECT_CLASS)
    {
        rdplib_platform_free(allocation);
    }
    else
    {
        rdp_fast_pool_t *pool = &fast_malloc_pools[pool_index];
        rdp_list_link_t *block = (rdp_list_link_t *)allocation;

        block->value = block;
        rdplib_platform_mutex_lock(&pool->lock);
        list_add_head(&pool->free_blocks, block);
        rdplib_platform_mutex_unlock(&pool->lock);
    }
}

void fast_malloc_destroy(void)
{
    uint32_t pool_index;
    void *allocation;

    while ((allocation = list_remove_head(&fast_malloc_backing_allocations)) != NULL)
    {
        rdplib_platform_free(allocation);
    }

    for (pool_index = 0; pool_index < RDP_FAST_POOLED_CLASS_COUNT; ++pool_index)
    {
        rdplib_platform_mutex_destroy(&fast_malloc_pools[pool_index].lock);
    }

    fast_malloc_initialized_bytes = 0;
}
