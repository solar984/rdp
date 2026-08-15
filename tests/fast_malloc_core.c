// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fast.h"
#include "list.h"

_Static_assert(_Generic(&fast_malloc_init, void (*)(uint32_t): 1, default: 0), "fast_malloc_init signature");
_Static_assert(_Generic(&fast_malloc_destroy, void (*)(void): 1, default: 0), "fast_malloc_destroy signature");
_Static_assert(_Generic(&fast_malloc, void *(*)(uint32_t): 1, default: 0), "fast_malloc signature");
_Static_assert(_Generic(&fast_free, void (*)(void *): 1, default: 0), "fast_free signature");
#if defined(RDPLIB_DEBUG) || defined(RDP_DEAD_CODE)
_Static_assert(_Generic(&fast_memory, uint32_t (*)(void *): 1, default: 0), "fast_memory signature");
#endif
#if defined(RDPLIB_DEBUG) || defined(RDPLIB_SOURCE_FAITHFUL) || defined(RDP_DEAD_CODE)
_Static_assert(_Generic(&fast_malloc_usage, uint32_t (*)(void): 1, default: 0), "fast_malloc_usage signature");
#endif
#if defined(RDPLIB_DEBUG) || defined(RDP_DEAD_CODE)
_Static_assert(_Generic(&fast_size, uint32_t (*)(void *): 1, default: 0), "fast_size signature");
#endif

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

enum
{
    FAST_TEST_HEADER_SIZE = 8,
    FAST_TEST_TOP_BLOCK_SIZE = 584
};

typedef struct allocation_case_t
{
    uint32_t requested_size;
    uint32_t expected_pool;
    uint32_t expected_size;
} allocation_case_t;

static uint32_t allocation_header_word(const void *payload, uint32_t word)
{
    uint32_t value;

    assert(word < 2);
    memcpy(&value, (const uint8_t *)payload - FAST_TEST_HEADER_SIZE + word * sizeof(value), sizeof(value));
    return value;
}

#if defined(RDPLIB_DEBUG) || defined(RDP_DEAD_CODE)
static void set_allocation_header_word(void *payload, uint32_t word, uint32_t value)
{
    assert(word < 2);
    memcpy((uint8_t *)payload - FAST_TEST_HEADER_SIZE + word * sizeof(value), &value, sizeof(value));
}
#endif

static void check_allocation(void *payload, uint32_t expected_pool, uint32_t expected_size)
{
    assert(payload != NULL);
    assert(((uintptr_t)payload & (uintptr_t)7u) == 0);
    assert(allocation_header_word(payload, 0) == expected_pool);
    assert(allocation_header_word(payload, 1) == expected_size);
#if defined(RDPLIB_DEBUG) || defined(RDP_DEAD_CODE)
    assert(fast_memory(payload));
#endif
#if defined(RDPLIB_DEBUG) || defined(RDP_DEAD_CODE)
    assert(fast_size(payload) == expected_size);
#endif
}

static int allocations_overlap(const void *first, uint32_t first_size, const void *second, uint32_t second_size)
{
    uintptr_t first_begin = (uintptr_t)first;
    uintptr_t second_begin = (uintptr_t)second;

    return first_begin < second_begin + second_size && second_begin < first_begin + first_size;
}

static void fill_allocation(void *payload, uint32_t size, uint8_t value)
{
    memset(payload, value, size);
}

static void check_allocation_bytes(const void *payload, uint32_t size, uint8_t value)
{
    const uint8_t *bytes = (const uint8_t *)payload;
    uint32_t offset;

    for (offset = 0; offset < size; ++offset)
        assert(bytes[offset] == value);
}

static void check_cleared_backing_payload(const void *payload, uint32_t size)
{
    const uint8_t *bytes = (const uint8_t *)payload;
    size_t offset = sizeof(rdp_link_t) - FAST_TEST_HEADER_SIZE;

    assert(sizeof(rdp_link_t) >= FAST_TEST_HEADER_SIZE);
    assert(offset < size);

    // A fresh pooled block has carried an intrusive free list link. Everything after that metadata retains the backing store clear.
    for (; offset < size; ++offset)
        assert(bytes[offset] == 0);
}

static void test_class_boundaries_and_direct_allocations(void)
{
    static const allocation_case_t cases[] = {
        {0, 0, 64},
        {64, 0, 64},
        {65, 1, 136},
        {136, 1, 136},
        {137, 2, 280},
        {280, 2, 280},
        {281, 3, 576},
        {576, 3, 576},
        {577, 4, 577},
        {4096, 4, 4096}
    };
    void *allocations[sizeof(cases) / sizeof(cases[0])];
    size_t allocation_index;
    size_t other_index;

    fast_malloc_init(FAST_TEST_TOP_BLOCK_SIZE);
#ifdef RDP_DEAD_CODE
    assert(fast_malloc_usage() == FAST_TEST_TOP_BLOCK_SIZE);
#endif

    for (allocation_index = 0; allocation_index < sizeof(cases) / sizeof(cases[0]); ++allocation_index)
    {
        allocations[allocation_index] = fast_malloc(cases[allocation_index].requested_size);
        check_allocation(allocations[allocation_index], cases[allocation_index].expected_pool, cases[allocation_index].expected_size);

        for (other_index = 0; other_index < allocation_index; ++other_index)
            assert(!allocations_overlap(allocations[allocation_index], cases[allocation_index].expected_size, allocations[other_index], cases[other_index].expected_size));
    }

#if defined(RDPLIB_DEBUG) || defined(RDP_DEAD_CODE)
    set_allocation_header_word(allocations[8], 1, 576);
    assert(!fast_memory(allocations[8]));
    set_allocation_header_word(allocations[8], 1, 577);
    set_allocation_header_word(allocations[8], 0, 5);
    assert(!fast_memory(allocations[8]));
    set_allocation_header_word(allocations[8], 0, 4);
    assert(fast_memory(allocations[8]));
#endif

    for (allocation_index = 0; allocation_index < sizeof(cases) / sizeof(cases[0]); ++allocation_index)
        fill_allocation(allocations[allocation_index], cases[allocation_index].expected_size, (uint8_t)(0x31u + allocation_index));

    for (allocation_index = 0; allocation_index < sizeof(cases) / sizeof(cases[0]); ++allocation_index)
        check_allocation_bytes(allocations[allocation_index], cases[allocation_index].expected_size, (uint8_t)(0x31u + allocation_index));

    for (allocation_index = sizeof(cases) / sizeof(cases[0]); allocation_index-- > 0;)
        fast_free(allocations[allocation_index]);

    fast_malloc_destroy();
}

static void test_recursive_splitting_and_lifo_reuse(void)
{
    void *first;
    void *second;
    void *reused_second;
    void *reused_first;

    fast_malloc_init(FAST_TEST_TOP_BLOCK_SIZE);

    first = fast_malloc(64);
    second = fast_malloc(64);
    check_allocation(first, 0, 64);
    check_allocation(second, 0, 64);
    assert(first != second);
    assert(!allocations_overlap(first, 64, second, 64));

    fill_allocation(first, 64, 0xA1);
    fill_allocation(second, 64, 0xB2);
    check_allocation_bytes(first, 64, 0xA1);
    check_allocation_bytes(second, 64, 0xB2);

    fast_free(first);
    fast_free(second);

    reused_second = fast_malloc(64);
    reused_first = fast_malloc(64);
    assert(reused_second == second);
    assert(reused_first == first);
    check_allocation(reused_second, 0, 64);
    check_allocation(reused_first, 0, 64);

    fill_allocation(reused_second, 64, 0xC3);
    fill_allocation(reused_first, 64, 0xD4);
    check_allocation_bytes(reused_second, 64, 0xC3);
    check_allocation_bytes(reused_first, 64, 0xD4);

    fast_free(reused_first);
    fast_free(reused_second);
    fast_malloc_destroy();
}

static void test_top_pool_growth(void)
{
    void *first;
    void *second;
#ifdef RDP_DEAD_CODE
    uint32_t usage_before_growth;
#endif

    fast_malloc_init(FAST_TEST_TOP_BLOCK_SIZE);
#ifdef RDP_DEAD_CODE
    usage_before_growth = fast_malloc_usage();
#endif

    first = fast_malloc(576);
    check_allocation(first, 3, 576);
    check_cleared_backing_payload(first, 576);
    second = fast_malloc(576);
    check_allocation(second, 3, 576);
    check_cleared_backing_payload(second, 576);
#ifdef RDP_DEAD_CODE
    assert(fast_malloc_usage() == usage_before_growth + FAST_TEST_TOP_BLOCK_SIZE);
#endif
    assert(first != second);
    assert(!allocations_overlap(first, 576, second, 576));

    fill_allocation(first, 576, 0x5A);
    fill_allocation(second, 576, 0xA5);
    check_allocation_bytes(first, 576, 0x5A);
    check_allocation_bytes(second, 576, 0xA5);

    fast_free(second);
    fast_free(first);
    fast_malloc_destroy();
}

static void test_destroy_and_reinitialize(void)
{
    void *pooled;
    void *direct;

    fast_malloc_init(FAST_TEST_TOP_BLOCK_SIZE);
    pooled = fast_malloc(136);
    direct = fast_malloc(577);
    check_allocation(pooled, 1, 136);
    check_allocation(direct, 4, 577);
    fill_allocation(pooled, 136, 0x16);
    fill_allocation(direct, 577, 0x57);
    check_allocation_bytes(pooled, 136, 0x16);
    check_allocation_bytes(direct, 577, 0x57);
    fast_free(pooled);
    fast_free(direct);
    fast_malloc_destroy();

    fast_malloc_init(FAST_TEST_TOP_BLOCK_SIZE * 2u);
    pooled = fast_malloc(280);
    direct = fast_malloc(1024);
    check_allocation(pooled, 2, 280);
    check_allocation(direct, 4, 1024);
    fill_allocation(pooled, 280, 0x28);
    fill_allocation(direct, 1024, 0x10);
    check_allocation_bytes(pooled, 280, 0x28);
    check_allocation_bytes(direct, 1024, 0x10);
    fast_free(direct);
    fast_free(pooled);
    fast_malloc_destroy();
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_class_boundaries_and_direct_allocations();
    test_recursive_splitting_and_lifo_reuse();
    test_top_pool_growth();
    test_destroy_and_reinitialize();
    return 0;
}
