// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

#ifdef RDP_DEAD_CODE

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "hash.h"

static uint32_t malloc_calls;
static size_t malloc_size;
static uint32_t free_calls;
static uint32_t list_calls;

void *hash_test_malloc(size_t size)
{
    ++malloc_calls;
    malloc_size = size;
    return NULL;
}

void hash_test_free(void *memory)
{
    (void)memory;
    ++free_calls;
}

void list_init(list_t *list)
{
    (void)list;
    ++list_calls;
}

void list_create(list_t *list, uint32_t sorted, keycmp_f keycmp)
{
    (void)list;
    (void)sorted;
    (void)keycmp;
    ++list_calls;
}

void list_destroy(list_t *list)
{
    (void)list;
    ++list_calls;
}

void *list_remove_head(list_t *list)
{
    (void)list;
    ++list_calls;
    return NULL;
}

// GNU debug builds can emit the uncalled plain static list header bodies. If
// either retained dependency becomes live in this allocation only test, fail.
rdp_link_t *list_find_link_by_key(list_t *list, const void *key)
{
    (void)list;
    (void)key;
    assert(0);
    return NULL;
}

void *list_remove_by_link(list_t *list, rdp_link_t *link)
{
    (void)list;
    (void)link;
    assert(0);
    return NULL;
}

#define malloc hash_test_malloc
#define free hash_test_free
#include "../rdplib/src/hash.c"
#undef free
#undef malloc

static uint32_t historical_keyhash(void *key)
{
    return *(uint32_t *)key;
}

static int compare_int_keys(const void *key_1, const void *key_2)
{
    int value_1;
    int value_2;

    value_1 = *(const int *)key_1;
    value_2 = *(const int *)key_2;
    return (value_1 > value_2) - (value_1 < value_2);
}

static void test_allocation_failure(void)
{
    hash_t hash;

    malloc_calls = 0;
    malloc_size = 0;
    free_calls = 0;
    list_calls = 0;
    hash.table = (list_t *)(uintptr_t)UINT32_C(0x1234);
    hash.table_size = UINT32_C(0xA5A5A5A5);
    hash.keyhash = historical_keyhash;

    assert(hash_create(&hash, 4, compare_int_keys) == 2);
    assert(malloc_calls == 1);
    assert(malloc_size == sizeof(list_t) * 4);
    assert(free_calls == 0);
    assert(list_calls == 0);
    assert(hash.table == NULL);
    assert(hash.table_size == UINT32_C(0xA5A5A5A5));
    assert(hash.keyhash == historical_keyhash);
}

#endif /* RDP_DEAD_CODE */

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

#ifdef RDP_DEAD_CODE
    test_allocation_failure();
#endif
    return 0;
}
