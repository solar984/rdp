// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#ifdef RDP_DEAD_CODE

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cmp.h"
#include "dpc.h"
#include "utime.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(_Generic(&dpcq_create, uint32_t (*)(dpcq_t *): 1, default: 0), "dpcq_create signature");
_Static_assert(_Generic(&dpcq_get_next_timeout, uint32_t (*)(dpcq_t *): 1, default: 0), "dpcq_get_next_timeout signature");
_Static_assert(_Generic(&dpcq_peek_head, dpc_t *(*)(dpcq_t *): 1, default: 0), "dpcq_peek_head signature");
_Static_assert(_Generic((dpc_f)0, uint32_t (*)(dpc_t *): 1, default: 0), "dpc_f signature");

_Static_assert(offsetof(dpc_t, dpcq_link) == 0, "dpc_t::dpcq_link layout");
_Static_assert(offsetof(dpc_t, time) == sizeof(qlink), "dpc_t::time layout");
_Static_assert(offsetof(dpc_t, type) == sizeof(qlink) + sizeof(uint32_t), "dpc_t::type layout");
_Static_assert(offsetof(dpc_t, func) == sizeof(qlink) + 2 * sizeof(uint32_t), "dpc_t::func layout");
_Static_assert(offsetof(dpc_t, data) == sizeof(qlink) + 2 * sizeof(uint32_t) + sizeof(void *), "dpc_t::data layout");
_Static_assert(sizeof(dpc_t) == sizeof(qlink) + 2 * sizeof(uint32_t) + 2 * sizeof(void *), "dpc_t size");
_Static_assert(offsetof(dpcq_t, q) == 0, "dpcq_t::q layout");
_Static_assert(sizeof(dpcq_t) == sizeof(pqueue_t), "dpcq_t size");

#if defined(_WIN32) && UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(dpc_t, time) == 0x0c, "dpc_t::time Win32 layout");
_Static_assert(offsetof(dpc_t, type) == 0x10, "dpc_t::type Win32 layout");
_Static_assert(offsetof(dpc_t, func) == 0x14, "dpc_t::func Win32 layout");
_Static_assert(offsetof(dpc_t, data) == 0x18, "dpc_t::data Win32 layout");
_Static_assert(sizeof(dpc_t) == 0x1c, "dpc_t Win32 size");
_Static_assert(sizeof(dpcq_t) == 0x14, "dpcq_t Win32 size");
#endif

enum dpc_test_call
{
    DPC_TEST_CALL_CREATE = 1,
    DPC_TEST_CALL_PEEK = 2,
    DPC_TEST_CALL_TIME = 3
};

static enum dpc_test_call calls[4];
static uint32_t call_count;
static uint32_t create_result;
static pqueue_t *create_queue;
static uint32_t create_grow_size;
static keycmp_f create_keycmp;
static pqueue_t *peek_queue;
static void *peek_item;
static uint32_t now;

static void record_call(enum dpc_test_call call)
{
    assert(call_count < sizeof(calls) / sizeof(calls[0]));
    calls[call_count++] = call;
}

static void reset_stubs(void)
{
    memset(calls, 0, sizeof(calls));
    call_count = 0;
    create_result = 0;
    create_queue = NULL;
    create_grow_size = 0;
    create_keycmp = NULL;
    peek_queue = NULL;
    peek_item = NULL;
    now = 0;
}

uint32_t pqueue_create(pqueue_t *q, uint32_t grow_size, keycmp_f keycmp)
{
    record_call(DPC_TEST_CALL_CREATE);
    create_queue = q;
    create_grow_size = grow_size;
    create_keycmp = keycmp;
    return create_result;
}

void *pqueue_peek_head(pqueue_t *q)
{
    record_call(DPC_TEST_CALL_PEEK);
    peek_queue = q;
    return peek_item;
}

uint32_t time_get_ms(void)
{
    record_call(DPC_TEST_CALL_TIME);
    return now;
}

static uint32_t test_dpc_callback(dpc_t *dpc)
{
    return dpc->type;
}

static void test_create_forwarding(void)
{
    dpcq_t dpcq;

    memset(&dpcq, 0xa5, sizeof(dpcq));
    reset_stubs();
    assert(dpcq_create(&dpcq) == 0);
    assert(call_count == 1);
    assert(calls[0] == DPC_TEST_CALL_CREATE);
    assert(create_queue == &dpcq.q);
    assert(create_grow_size == 0x800u);
    assert(create_keycmp == uint32_cmp);

    reset_stubs();
    create_result = 1;
    assert(dpcq_create(&dpcq) == 1);
    assert(call_count == 1);
    assert(calls[0] == DPC_TEST_CALL_CREATE);
    assert(create_queue == &dpcq.q);
    assert(create_grow_size == 0x800u);
    assert(create_keycmp == uint32_cmp);
}

static void test_peek_helper(void)
{
    dpcq_t dpcq;
    dpc_t dpc;

    memset(&dpcq, 0, sizeof(dpcq));
    memset(&dpc, 0, sizeof(dpc));
    dpc.func = test_dpc_callback;
    dpc.type = 17;
    assert(dpc.func(&dpc) == 17);

    reset_stubs();
    peek_item = &dpc;
    assert(dpcq_peek_head(&dpcq) == &dpc);
    assert(call_count == 1);
    assert(calls[0] == DPC_TEST_CALL_PEEK);
    assert(peek_queue == &dpcq.q);

    reset_stubs();
    assert(dpcq_peek_head(&dpcq) == NULL);
    assert(call_count == 1);
    assert(calls[0] == DPC_TEST_CALL_PEEK);
    assert(peek_queue == &dpcq.q);
}

static void test_empty_timeout(void)
{
    dpcq_t dpcq;

    memset(&dpcq, 0, sizeof(dpcq));
    reset_stubs();
    now = 0x12345678u;
    assert(dpcq_get_next_timeout(&dpcq) == UINT32_MAX);
    assert(call_count == 1);
    assert(calls[0] == DPC_TEST_CALL_PEEK);
    assert(peek_queue == &dpcq.q);
}

static void test_timeout_case(uint32_t clock_value, uint32_t deadline, uint32_t expected)
{
    dpcq_t dpcq;
    dpc_t dpc;

    memset(&dpcq, 0, sizeof(dpcq));
    memset(&dpc, 0, sizeof(dpc));
    reset_stubs();
    now = clock_value;
    dpc.time = deadline;
    peek_item = &dpc;

    assert(dpcq_get_next_timeout(&dpcq) == expected);
    assert(call_count == 2);
    assert(calls[0] == DPC_TEST_CALL_PEEK);
    assert(calls[1] == DPC_TEST_CALL_TIME);
    assert(peek_queue == &dpcq.q);
}

static void test_modular_timeout_boundaries(void)
{
    test_timeout_case(100u, 100u, 0u);
    test_timeout_case(100u, 101u, 1u);
    test_timeout_case(100u, 99u, 0u);
    test_timeout_case(1u, 0x80000000u, 0x7fffffffu);
    test_timeout_case(1u, 0x80000001u, 0u);
    test_timeout_case(0xfffffff0u, 0x00000010u, 0x20u);
    test_timeout_case(0x80000010u, 0x00000000u, 0x7ffffff0u);
}

#endif /* RDP_DEAD_CODE */

int main(void)
{
#ifdef RDP_DEAD_CODE
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_create_forwarding();
    test_peek_helper();
    test_empty_timeout();
    test_modular_timeout_boundaries();
#endif
    return 0;
}
