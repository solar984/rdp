// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fast.h"

enum
{
    FAST_OWNER_ARENA_SIZE = 8192,
    FAST_OWNER_MAX_CALLS = 8
};

typedef union fast_owner_arena_t
{
    void *pointer_alignment;
    uint64_t integer_alignment;
    unsigned char bytes[FAST_OWNER_ARENA_SIZE];
} fast_owner_arena_t;

static fast_owner_arena_t allocator_arena;
static size_t allocator_offset;
static size_t allocation_sizes[FAST_OWNER_MAX_CALLS];
static void *allocation_results[FAST_OWNER_MAX_CALLS];
static uint32_t allocation_calls;
static uint32_t allocation_fail_call;
static void *freed_pointers[FAST_OWNER_MAX_CALLS];
static uint32_t free_calls;

static size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

void *fast_owner_test_malloc(size_t size)
{
    size_t offset;
    void *result;

    assert(allocation_calls < FAST_OWNER_MAX_CALLS);
    allocation_sizes[allocation_calls] = size;
    ++allocation_calls;
    if (allocation_fail_call == allocation_calls)
    {
        allocation_results[allocation_calls - 1u] = NULL;
        return NULL;
    }

    offset = align_up(allocator_offset, sizeof(void *));
    if (size > sizeof(allocator_arena.bytes) - offset)
    {
        allocation_results[allocation_calls - 1u] = NULL;
        return NULL;
    }

    result = &allocator_arena.bytes[offset];
    allocator_offset = offset + size;
    allocation_results[allocation_calls - 1u] = result;
    return result;
}

void fast_owner_test_free(void *allocation)
{
    assert(free_calls < FAST_OWNER_MAX_CALLS);
    freed_pointers[free_calls] = allocation;
    ++free_calls;
}

#define rdplib_platform_malloc fast_owner_test_malloc
#define rdplib_platform_free fast_owner_test_free
#include "../rdplib/src/fast_malloc.c"
#undef rdplib_platform_free
#undef rdplib_platform_malloc

_Static_assert(sizeof(struct _fast_malloc_block_header_t) == 8, "fast block header size");
_Static_assert(offsetof(struct _fast_malloc_block_header_t, pool) == 0, "fast block pool offset");
_Static_assert(offsetof(struct _fast_malloc_block_header_t, size) == 4, "fast block size offset");
_Static_assert(_Generic(&fast_malloc_grow, void (*)(uint32_t): 1, default: 0), "fast_malloc_grow signature");
_Static_assert(_Generic(&fast_malloc_raw, char *(*)(uint32_t): 1, default: 0), "fast_malloc_raw signature");
_Static_assert(_Generic(&g_fast_malloc_ready, uint32_t *: 1, default: 0), "g_fast_malloc_ready type");
_Static_assert(_Generic(&g_malloc_memory, list_t *: 1, default: 0), "g_malloc_memory type");
_Static_assert(_Generic(&g_fast_malloc_pool, fast_malloc_pool_t (*)[FAST_MALLOC_POOL_COUNT]: 1, default: 0),
               "g_fast_malloc_pool type");

#if defined(_WIN32) && !defined(_WIN64)
_Static_assert(offsetof(fast_malloc_pool_t, raw_size) == 0x00, "fast pool raw_size offset");
_Static_assert(offsetof(fast_malloc_pool_t, avail_size) == 0x04, "fast pool avail_size offset");
_Static_assert(offsetof(fast_malloc_pool_t, chunks) == 0x08, "fast pool chunks offset");
_Static_assert(offsetof(fast_malloc_pool_t, list) == 0x0c, "fast pool list offset");
_Static_assert(offsetof(fast_malloc_pool_t, lock) == 0x20, "fast pool lock offset");
_Static_assert(sizeof(fast_malloc_pool_t) == 0x38 + RDP_WIN32_UMUTEX_OWNER_BYTES, "fast pool Win32 size");
_Static_assert(sizeof(g_fast_malloc_pool) == 4u * (0x38 + RDP_WIN32_UMUTEX_OWNER_BYTES), "fast pool array Win32 size");
#endif

static void reset_allocator(void)
{
    memset(&allocator_arena, 0xa5, sizeof(allocator_arena));
    allocator_offset = 0;
    memset(allocation_sizes, 0, sizeof(allocation_sizes));
    memset(allocation_results, 0, sizeof(allocation_results));
    allocation_calls = 0;
    allocation_fail_call = 0;
    memset(freed_pointers, 0, sizeof(freed_pointers));
    free_calls = 0;
}

static void reset_allocator_state(void)
{
    assert(g_fast_malloc_ready == 0);
    memset(&g_malloc_memory, 0, sizeof(g_malloc_memory));
    memset(g_fast_malloc_pool, 0, sizeof(g_fast_malloc_pool));
    reset_allocator();
}

static void assert_pool_geometry(void)
{
    static const uint32_t raw_sizes[FAST_MALLOC_POOL_COUNT] = {72, 144, 288, 584};
    static const uint32_t avail_sizes[FAST_MALLOC_POOL_COUNT] = {64, 136, 280, 576};
    uint32_t pool;

    for (pool = 0; pool < FAST_MALLOC_POOL_COUNT; ++pool)
    {
        assert(g_fast_malloc_pool[pool].raw_size == raw_sizes[pool]);
        assert(g_fast_malloc_pool[pool].avail_size == avail_sizes[pool]);
    }
}

static void test_backing_storage_is_cleared(void)
{
    const uint8_t *payload;
    size_t offset;

    reset_allocator_state();
    fast_malloc_init(584);
    payload = (const uint8_t *)fast_malloc(576);
    assert(payload != NULL);

    // The free list link overwrites the beginning of a fresh block. Everything
    // after that metadata must retain the allocator's backing store clear.
    offset = sizeof(rdp_link_t) - FAST_MALLOC_BLOCK_HEADER_SIZE;
    assert(offset < 576);
    for (; offset < 576; ++offset)
    {
        assert(payload[offset] == 0);
    }

    fast_free((void *)payload);
    fast_malloc_destroy();
}

static void test_initial_allocation_size(uint32_t budget, uint32_t rounded_budget)
{
    reset_allocator_state();
    fast_malloc_init(budget);

    assert(g_fast_malloc_ready == budget);
    assert(allocation_calls == 1);
    assert(allocation_sizes[0] == (size_t)rounded_budget + sizeof(rdp_link_t));
    assert(allocation_results[0] != NULL);
    assert(g_malloc_memory.size == 1);
    assert(g_fast_malloc_pool[3].chunks == rounded_budget / 584u);
    assert(g_fast_malloc_pool[3].list.size == rounded_budget / 584u);
    assert_pool_geometry();

    fast_malloc_destroy();
    assert(g_fast_malloc_ready == 0);
    assert(free_calls == 1);
    assert(freed_pointers[0] == allocation_results[0]);
}

static void test_exact_growth_sizes(void)
{
    test_initial_allocation_size(1, 584);
    test_initial_allocation_size(584, 584);
    test_initial_allocation_size(585, 1168);
}

static void test_direct_allocation_failure_and_exact_limit(void)
{
    void *allocation;
    uint32_t calls_before;

    reset_allocator_state();
    fast_malloc_init(584);

    allocation_fail_call = 2;
    allocation = fast_malloc(577);
    assert(allocation == NULL);
    assert(allocation_calls == 2);
    assert(allocation_sizes[1] == 585);

    allocation_fail_call = 3;
    allocation = fast_malloc(UINT32_MAX - FAST_MALLOC_BLOCK_HEADER_SIZE);
    assert(allocation == NULL);
    assert(allocation_calls == 3);
    assert(allocation_sizes[2] == (size_t)UINT32_MAX);

    calls_before = allocation_calls;
    allocation_fail_call = calls_before + 1u;
    allocation = fast_malloc(UINT32_MAX);
    assert(allocation == NULL);
#if SIZE_MAX <= UINT32_MAX && !defined(RDPLIB_SOURCE_FAITHFUL)
    assert(allocation_calls == calls_before);
#elif SIZE_MAX <= UINT32_MAX
    assert(allocation_calls == calls_before + 1u);
    assert(allocation_sizes[calls_before] == 7);
#else
    assert(allocation_calls == calls_before + 1u);
    assert(allocation_sizes[calls_before] == (size_t)UINT32_MAX + FAST_MALLOC_BLOCK_HEADER_SIZE);
#endif

    fast_malloc_destroy();
}

static void test_reinitialize_chunk_accounting(void)
{
    void *allocation;

    reset_allocator_state();
    fast_malloc_init(584);
    allocation = fast_malloc(64);
    assert(allocation != NULL);
    fast_free(allocation);
    assert(g_fast_malloc_pool[0].chunks == 2);
    assert(g_fast_malloc_pool[1].chunks == 1);
    assert(g_fast_malloc_pool[2].chunks == 1);
    assert(g_fast_malloc_pool[3].chunks == 0);
    fast_malloc_destroy();

    reset_allocator();
    fast_malloc_init(584);
#if defined(RDPLIB_DEBUG) || defined(RDPLIB_SOURCE_FAITHFUL)
    assert(g_fast_malloc_pool[0].chunks == 2);
    assert(g_fast_malloc_pool[1].chunks == 1);
    assert(g_fast_malloc_pool[2].chunks == 1);
    assert(g_fast_malloc_pool[3].chunks == 1);
#else
    assert(g_fast_malloc_pool[0].chunks == 0);
    assert(g_fast_malloc_pool[1].chunks == 0);
    assert(g_fast_malloc_pool[2].chunks == 0);
    assert(g_fast_malloc_pool[3].chunks == 1);
#endif
#if defined(RDPLIB_DEBUG) || defined(RDPLIB_SOURCE_FAITHFUL) || defined(RDP_DEAD_CODE)
#if defined(RDPLIB_DEBUG) || defined(RDPLIB_SOURCE_FAITHFUL)
    assert(fast_malloc_usage() == 1168);
#else
    assert(fast_malloc_usage() == 584);
#endif
#endif
    fast_malloc_destroy();
}

int main(void)
{
    test_backing_storage_is_cleared();
    test_exact_growth_sizes();
    test_direct_allocation_failure_and_exact_limit();
    test_reinitialize_chunk_accounting();
    return 0;
}
