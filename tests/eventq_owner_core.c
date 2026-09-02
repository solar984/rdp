// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "eventq.h"

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
static uint32_t free_calls;
static void *freed_pointer;
static uint32_t mock_now;
static uint32_t time_calls;

static void reset_seams(void)
{
    memset(&allocator_storage, 0, sizeof(allocator_storage));
    malloc_calls = 0;
    malloc_size = 0;
    malloc_fails = 0;
    realloc_calls = 0;
    free_calls = 0;
    freed_pointer = NULL;
    mock_now = 0;
    time_calls = 0;
}

void *eventq_test_malloc(size_t size)
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

void *eventq_test_realloc(void *memory, size_t size)
{
    ++realloc_calls;
    assert(memory == allocator_storage.bytes);
    assert(size <= sizeof(allocator_storage.bytes));
    return memory;
}

void eventq_test_free(void *memory)
{
    ++free_calls;
    freed_pointer = memory;
}

uint32_t eventq_test_time_get_ms(void)
{
    ++time_calls;
    return mock_now;
}

#define malloc eventq_test_malloc
#define realloc eventq_test_realloc
#define free eventq_test_free
#include "../rdplib/src/pqueue.c"
#undef free
#undef realloc
#undef malloc

#define time_get_ms eventq_test_time_get_ms
#include "../rdplib/src/eventq.c"
#undef time_get_ms

_Static_assert(_Generic(&ascending_timeout_data_cmp, int (*)(const void *, const void *): 1, default: 0),
               "ascending_timeout_data_cmp signature");

static void initialize_connection(connection_t *c, uint32_t infinite, uint32_t time)
{
    memset(c, 0, sizeof(*c));
    c->cn_event_time.infinite = infinite;
    c->cn_event_time.time = time;
    c->cn_event_queue_link.item = c;
    c->cn_event_queue_link.key.p = &c->cn_event_time;
}

static void assert_timeout(eventq_t *eq, struct timeval *timeout, uint32_t now, uint32_t seconds, uint32_t microseconds)
{
    struct timeval *result;
    uint32_t calls_before;

    mock_now = now;
    calls_before = time_calls;
    result = eventq_get_event_timeout(eq, timeout);
    assert(result == timeout);
    assert(time_calls == calls_before + 1);
    assert(timeout->tv_sec == (long)seconds);
    assert(timeout->tv_usec == (long)microseconds);
}

static void test_create_allocation_failure(void)
{
    eventq_t eq;

    reset_seams();
    eventq_init(&eq);
    malloc_fails = 1;

    assert(eventq_create(&eq, 4) == 1);
    assert(malloc_calls == 1);
    assert(malloc_size == 4 * sizeof(qlink *));
    assert(realloc_calls == 0);
    assert(free_calls == 0);
    assert(eq.q.array == NULL);
    assert(eq.q.next_element == 0);
    assert(eq.q.array_size == 4);
    assert(eq.q.grow_size == 4);
    assert(eq.q.keycmp == ascending_timeout_data_cmp);
#ifdef _WIN32
#ifdef RDPLIB_DEBUG
    assert(eq.lock.owner == NULL);
#endif
#else
    assert(eq.lock.platform.initialized != 0);
#ifdef RDPLIB_DEBUG
    assert(eq.lock.owned == 0);
#endif
#endif

    eventq_destroy(&eq);
    assert(free_calls == 0);
}

static void test_deterministic_timeout_paths(void)
{
    eventq_t eq;
    connection_t c;
    struct timeval timeout;

    reset_seams();
    eventq_init(&eq);
    assert(eventq_create(&eq, 4) == 0);
    assert(malloc_calls == 1);
    assert(malloc_size == 4 * sizeof(qlink *));
    assert(eq.q.grow_size == 4);

    timeout.tv_sec = 123;
    timeout.tv_usec = 456;
    assert(eventq_get_event_timeout(&eq, &timeout) == NULL);
    assert(time_calls == 0);
    assert(timeout.tv_sec == 123);
    assert(timeout.tv_usec == 456);

    initialize_connection(&c, 1, 0);
    assert(eventq_insert(&eq, &c) == 0);
    assert(eventq_get_event_timeout(&eq, &timeout) == NULL);
    assert(time_calls == 0);
    assert(timeout.tv_sec == 123);
    assert(timeout.tv_usec == 456);

    c.cn_event_time.infinite = 0;
    c.cn_event_time.time = 1000;
    assert_timeout(&eq, &timeout, 1000, 0, 0);

    c.cn_event_time.time = 3501;
    assert_timeout(&eq, &timeout, 1000, 2, 501000);

    c.cn_event_time.time = 999;
    assert_timeout(&eq, &timeout, 1000, 0, 0);

    c.cn_event_time.time = 50;
    assert_timeout(&eq, &timeout, UINT32_MAX - 100u, 0, 151000);

    c.cn_event_time.time = UINT32_C(0x80000000);
    assert_timeout(&eq, &timeout, 0, 0, 0);

    c.cn_event_time.time = UINT32_C(0x7fffffff);
    assert_timeout(&eq, &timeout, 0, 2147483, 647000);

    assert(realloc_calls == 0);
    assert(eventq_remove_head(&eq) == &c);
    eventq_destroy(&eq);
    assert(free_calls == 1);
    assert(freed_pointer == allocator_storage.bytes);
}

int main(void)
{
    test_create_allocation_failure();
    test_deterministic_timeout_paths();
    return 0;
}
