// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "eventq.h"
#include "utime.h"

_Static_assert(_Generic(&eventq_create, uint32_t (*)(eventq_t *, uint32_t): 1, default: 0), "eventq_create signature");
_Static_assert(_Generic(&eventq_get_event_timeout, struct timeval *(*)(eventq_t *, struct timeval *): 1, default: 0),
               "eventq_get_event_timeout signature");
_Static_assert(_Generic(&eventq_init, void (*)(eventq_t *): 1, default: 0), "eventq_init signature");
_Static_assert(_Generic(&eventq_destroy, void (*)(eventq_t *): 1, default: 0), "eventq_destroy signature");
_Static_assert(_Generic(&eventq_remove_head, connection_t *(*)(eventq_t *): 1, default: 0), "eventq_remove_head signature");
_Static_assert(_Generic(&eventq_peek_head, connection_t *(*)(eventq_t *): 1, default: 0), "eventq_peek_head signature");
_Static_assert(_Generic(&eventq_insert, uint32_t (*)(eventq_t *, connection_t *): 1, default: 0), "eventq_insert signature");
_Static_assert(_Generic(&eventq_remove_by_ptr, connection_t *(*)(eventq_t *, connection_t *): 1, default: 0),
               "eventq_remove_by_ptr signature");
_Static_assert(_Generic(&eventq_resort_by_ptr, void (*)(eventq_t *, connection_t *): 1, default: 0),
               "eventq_resort_by_ptr signature");
_Static_assert(_Generic(&eventq_lock, void (*)(eventq_t *): 1, default: 0), "eventq_lock signature");
_Static_assert(_Generic(&eventq_unlock, void (*)(eventq_t *): 1, default: 0), "eventq_unlock signature");
_Static_assert(sizeof(timeout_data) == 8, "timeout_data size");
_Static_assert(offsetof(timeout_data, infinite) == 0, "timeout_data.infinite offset");
_Static_assert(offsetof(timeout_data, time) == 4, "timeout_data.time offset");
_Static_assert(offsetof(eventq_t, q) == 0, "eventq_t.q offset");
_Static_assert(offsetof(eventq_t, lock) == sizeof(pqueue_t), "eventq_t.lock offset");
#if defined(_WIN32) && !defined(_WIN64)
_Static_assert(offsetof(eventq_t, lock) == 20, "Win32 eventq_t.lock offset");
_Static_assert(sizeof(eventq_t) == 0x2c + RDP_WIN32_UMUTEX_OWNER_BYTES, "Win32 eventq_t size");
#endif

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

static void initialize_connection(connection_t *c, uint32_t infinite, uint32_t time)
{
    memset(c, 0, sizeof(*c));
    c->cn_event_time.infinite = infinite;
    c->cn_event_time.time = time;
    c->cn_event_queue_link.item = c;
    c->cn_event_queue_link.key.p = &c->cn_event_time;
}

static void test_layout_and_initialization(void)
{
    eventq_t eq;

    memset(&eq, 0xA5, sizeof(eq));
    eventq_init(&eq);
    assert(eq.q.array == NULL);
    assert(eq.q.next_element == 0);
    assert(eq.q.array_size == 0);
    assert(eq.q.grow_size == 0);
    assert(eq.q.keycmp == NULL);
#ifdef _WIN32
#ifdef RDPLIB_DEBUG
    assert(eq.lock.owner == NULL);
#endif
#else
    assert(eq.lock.platform.initialized == 0);
#ifdef RDPLIB_DEBUG
    assert(eq.lock.owned == 0);
#endif
#endif
}

static void test_create_and_comparator(void)
{
    eventq_t eq;
    timeout_data finite_100 = {0, 100};
    timeout_data finite_200 = {0, 200};
    timeout_data finite_zero = {0, 0};
    timeout_data finite_max = {0, UINT32_MAX};
    timeout_data finite_half = {0, UINT32_C(0x80000000)};
    timeout_data infinite_1 = {1, 1};
    timeout_data infinite_2 = {1, UINT32_MAX};

    eventq_init(&eq);
    assert(eventq_create(&eq, 37) == 0);
    assert(eq.q.array != NULL);
    assert(eq.q.next_element == 0);
    assert(eq.q.array_size == 1);
    assert(eq.q.grow_size == 1);
    assert(eq.q.keycmp != NULL);
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

    assert(eq.q.keycmp(&finite_100, &finite_100) == 0);
    assert(eq.q.keycmp(&finite_100, &finite_200) == -1);
    assert(eq.q.keycmp(&finite_200, &finite_100) == 1);
    assert(eq.q.keycmp(&finite_zero, &finite_max) == 1);
    assert(eq.q.keycmp(&finite_max, &finite_zero) == -1);
    assert(eq.q.keycmp(&finite_half, &finite_zero) == -1);
    assert(eq.q.keycmp(&finite_zero, &finite_half) == -1);
    assert(eq.q.keycmp(&finite_100, &infinite_1) < 0);
    assert(eq.q.keycmp(&infinite_1, &finite_100) > 0);
    assert(eq.q.keycmp(&infinite_1, &infinite_2) == 0);

    eventq_lock(&eq);
    eventq_unlock(&eq);
    assert(eventq_peek_head(&eq) == NULL);
    assert(eventq_remove_head(&eq) == NULL);
    eventq_destroy(&eq);
}

static void test_connection_helpers(void)
{
    eventq_t eq;
    connection_t first;
    connection_t second;
    connection_t infinite;

    eventq_init(&eq);
    assert(eventq_create(&eq, 99) == 0);
    initialize_connection(&first, 0, 300);
    initialize_connection(&second, 0, 100);
    initialize_connection(&infinite, 1, 0);

    assert(eventq_insert(&eq, &first) == 0);
    assert(eventq_insert(&eq, &second) == 0);
    assert(eventq_insert(&eq, &infinite) == 0);
    assert(eq.q.next_element == 3);
    assert(eq.q.array_size == 3);
    assert(eq.q.grow_size == 1);
    assert(eventq_peek_head(&eq) == &second);

    first.cn_event_time.time = 50;
    eventq_resort_by_ptr(&eq, &first);
    assert(eventq_peek_head(&eq) == &first);

    first.cn_event_time.time = 400;
    eventq_resort_by_ptr(&eq, &first);
    assert(eventq_peek_head(&eq) == &second);

    assert(eventq_remove_by_ptr(&eq, &second) == &second);
    assert(eventq_peek_head(&eq) == &first);
    assert(eventq_remove_head(&eq) == &first);
    assert(eventq_remove_head(&eq) == &infinite);
    assert(eventq_remove_head(&eq) == NULL);
    eventq_destroy(&eq);
}

static void test_event_timeout(void)
{
    eventq_t eq;
    connection_t c;
    struct timeval timeout;
    struct timeval *result;
    uint32_t before;
    uint32_t after;
    uint32_t represented_ms;
    uint32_t minimum_ms;

    eventq_init(&eq);
    assert(eventq_create(&eq, 1) == 0);

    timeout.tv_sec = 123;
    timeout.tv_usec = 456;
    assert(eventq_get_event_timeout(&eq, &timeout) == NULL);
    assert(timeout.tv_sec == 123);
    assert(timeout.tv_usec == 456);

    initialize_connection(&c, 1, 0);
    assert(eventq_insert(&eq, &c) == 0);
    assert(eventq_get_event_timeout(&eq, &timeout) == NULL);
    assert(timeout.tv_sec == 123);
    assert(timeout.tv_usec == 456);

    c.cn_event_time.infinite = 0;
    c.cn_event_time.time = time_get_ms() - 1u;
    eventq_resort_by_ptr(&eq, &c);
    result = eventq_get_event_timeout(&eq, &timeout);
    assert(result == &timeout);
    assert(timeout.tv_sec == 0);
    assert(timeout.tv_usec == 0);

    before = time_get_ms();
    c.cn_event_time.time = before + 2500u;
    result = eventq_get_event_timeout(&eq, &timeout);
    after = time_get_ms();
    assert(result == &timeout);
    assert(timeout.tv_sec >= 0);
    assert(timeout.tv_usec >= 0 && timeout.tv_usec < 1000000);
    assert((timeout.tv_usec % 1000) == 0);
    represented_ms = (uint32_t)timeout.tv_sec * 1000u + (uint32_t)timeout.tv_usec / 1000u;
    minimum_ms = (int32_t)(c.cn_event_time.time - after) > 0 ? c.cn_event_time.time - after : 0;
    assert(represented_ms >= minimum_ms);
    assert(represented_ms <= c.cn_event_time.time - before);

    assert(eventq_remove_head(&eq) == &c);
    eventq_destroy(&eq);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_layout_and_initialization();
    test_create_and_comparator();
    test_connection_helpers();
    test_event_timeout();
    return 0;
}
