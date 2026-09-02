// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>

static uint32_t malloc_calls;
static size_t malloc_size;
static uint32_t free_calls;

void *connhash_test_malloc(size_t size);
void connhash_test_free(void *memory);

#define rdplib_platform_malloc connhash_test_malloc
#define rdplib_platform_free connhash_test_free
#include "../rdplib/src/connhash.c"
#undef rdplib_platform_free
#undef rdplib_platform_malloc

void *connhash_test_malloc(size_t size)
{
    ++malloc_calls;
    malloc_size = size;
    return NULL;
}

void connhash_test_free(void *memory)
{
    (void)memory;
    ++free_calls;
}

static void test_initialized_failure_state(void)
{
    connhash_t ch;

    malloc_calls = 0;
    malloc_size = 0;
    free_calls = 0;
    connhash_init(&ch);

    assert(connhash_create(&ch, 3) == 1);
    assert(malloc_calls == 1);
    assert(malloc_size == 4 * sizeof(hashbin_t));
    assert(free_calls == 0);
    assert(ch.table_size == 0);
    assert(ch.table == NULL);
}

static void test_live_state_is_transactional_on_failure(void)
{
    connhash_t ch;
    hashbin_t *const original_table = (hashbin_t *)(uintptr_t)UINT32_C(0x1234);

    malloc_calls = 0;
    malloc_size = 0;
    free_calls = 0;
    ch.table_size = UINT16_C(0x1234);
    ch.table = original_table;

    assert(connhash_create(&ch, 4) == 1);
    assert(malloc_calls == 1);
    assert(malloc_size == 8 * sizeof(hashbin_t));
    assert(free_calls == 0);
    assert(ch.table_size == UINT16_C(0x1234));
    assert(ch.table == original_table);
}

int main(void)
{
    test_initialized_failure_state();
    test_live_state_is_transactional_on_failure();
    return 0;
}
