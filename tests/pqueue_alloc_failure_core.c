// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pqueue.h"

typedef union allocator_storage_t
{
    void *pointer_alignment;
    uint64_t integer_alignment;
    unsigned char bytes[8 * sizeof(void *)];
} allocator_storage_t;

static allocator_storage_t allocator_storage;
static uint32_t malloc_calls;
static size_t malloc_size;
static int malloc_fails;
static uint32_t realloc_calls;
static void *realloc_pointer;
static size_t realloc_size;
static int realloc_fails;
static uint32_t free_calls;
static void *freed_pointer;
static uint32_t keycmp_calls;

static void reset_allocator(void)
{
    memset(&allocator_storage, 0, sizeof(allocator_storage));
    malloc_calls = 0;
    malloc_size = 0;
    malloc_fails = 0;
    realloc_calls = 0;
    realloc_pointer = NULL;
    realloc_size = 0;
    realloc_fails = 0;
    free_calls = 0;
    freed_pointer = NULL;
    keycmp_calls = 0;
}

void *pqueue_test_malloc(size_t size)
{
    ++malloc_calls;
    malloc_size = size;
    if (malloc_fails)
    {
        return NULL;
    }

    assert(size <= sizeof(allocator_storage.bytes));
    return allocator_storage.bytes;
}

void *pqueue_test_realloc(void *memory, size_t size)
{
    ++realloc_calls;
    realloc_pointer = memory;
    realloc_size = size;
    if (realloc_fails)
    {
        return NULL;
    }

    assert(memory == allocator_storage.bytes);
    assert(size <= sizeof(allocator_storage.bytes));
    return memory;
}

void pqueue_test_free(void *memory)
{
    ++free_calls;
    freed_pointer = memory;
}

#define malloc pqueue_test_malloc
#define realloc pqueue_test_realloc
#define free pqueue_test_free
#include "../rdplib/src/pqueue.c"
#undef free
#undef realloc
#undef malloc

static int compare_integer_keys(const void *left, const void *right)
{
    int left_value;
    int right_value;

    ++keycmp_calls;
    left_value = *(const int *)left;
    right_value = *(const int *)right;
    return (left_value > right_value) - (left_value < right_value);
}

static void initialize_link(qlink *link, void *item, void *key)
{
    memset(link, 0, sizeof(*link));
    link->item = item;
    link->key.p = key;
}

static void test_create_allocation_failure(void)
{
    pqueue_t q;

    reset_allocator();
    memset(&q, 0xa5, sizeof(q));
    malloc_fails = 1;

    assert(pqueue_create(&q, 3, compare_integer_keys) == 1);
    assert(malloc_calls == 1);
    assert(malloc_size == 3 * sizeof(qlink *));
    assert(realloc_calls == 0);
    assert(free_calls == 0);
    assert(keycmp_calls == 0);
    assert(q.array == NULL);
    assert(q.next_element == 0);
    assert(q.array_size == 3);
    assert(q.grow_size == 3);
    assert(q.keycmp == compare_integer_keys);
}

static void test_growth_allocation_failure_preserves_state(void)
{
    pqueue_t q;
    qlink first;
    qlink second;
    qlink rejected;
    qlink **array_before;
    qlink *slot_0_before;
    qlink *slot_1_before;
    uint32_t keycmp_calls_before;
    int items[] = {10, 20, 5};
    int keys[] = {10, 20, 5};

    reset_allocator();
    assert(pqueue_create(&q, 2, compare_integer_keys) == 0);
    assert(q.array == (qlink **)allocator_storage.bytes);
    initialize_link(&first, &items[0], &keys[0]);
    initialize_link(&second, &items[1], &keys[1]);
    initialize_link(&rejected, &items[2], &keys[2]);
    rejected.index = UINT32_C(0xA5A5A5A5);
    assert(pqueue_insert(&q, &first) == 0);
    assert(pqueue_insert(&q, &second) == 0);

    array_before = q.array;
    slot_0_before = q.array[0];
    slot_1_before = q.array[1];
    keycmp_calls_before = keycmp_calls;
    realloc_fails = 1;

    assert(pqueue_insert(&q, &rejected) == 1);
    assert(malloc_calls == 1);
    assert(malloc_size == 2 * sizeof(qlink *));
    assert(realloc_calls == 1);
    assert(realloc_pointer == array_before);
    assert(realloc_size == 4 * sizeof(qlink *));
    assert(free_calls == 0);
    assert(keycmp_calls == keycmp_calls_before);
    assert(q.array == array_before);
    assert(q.next_element == 2);
    assert(q.array_size == 2);
    assert(q.grow_size == 2);
    assert(q.keycmp == compare_integer_keys);
    assert(q.array[0] == slot_0_before);
    assert(q.array[1] == slot_1_before);
    assert(first.index == 0);
    assert(second.index == 1);
    assert(rejected.index == UINT32_C(0xA5A5A5A5));
    assert(rejected.item == &items[2]);
    assert(rejected.key.p == &keys[2]);

    assert(pqueue_remove_head(&q) == &items[0]);
    assert(pqueue_remove_head(&q) == &items[1]);
    pqueue_destroy(&q);
    assert(free_calls == 1);
    assert(freed_pointer == array_before);
    assert(q.array == NULL);
}

int main(void)
{
    test_create_allocation_failure();
    test_growth_allocation_failure_preserves_state();
    return 0;
}
