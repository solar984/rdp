// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>

#include "uthread.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(_Generic(&uthread_wait_exit_code, uint32_t (*)(uthread_t *, uint32_t *): 1, default: 0), "uthread_wait_exit_code signature");
_Static_assert(_Generic(&uthread_init, void (*)(uthread_t *): 1, default: 0), "uthread_init signature");
_Static_assert(_Generic(&uthread_destroy, void (*)(uthread_t *): 1, default: 0), "uthread_destroy signature");
_Static_assert(_Generic(&uthread_create, uint32_t (*)(uthread_t *, uthread_f, void *): 1, default: 0), "uthread_create signature");
_Static_assert(offsetof(uthread_t, handle) == 0, "uthread_t must begin with handle");
_Static_assert(offsetof(uthread_t, id) >= sizeof(void *), "uthread_t id order");
_Static_assert(offsetof(uthread_t, proc) > offsetof(uthread_t, id), "uthread_t proc order");
_Static_assert(offsetof(uthread_t, data) > offsetof(uthread_t, proc), "uthread_t data order");

#if defined(_WIN32) && UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(uthread_t) == 0x10, "uthread_t Win32 size");
_Static_assert(offsetof(uthread_t, handle) == 0x00, "uthread_t handle offset");
_Static_assert(offsetof(uthread_t, id) == 0x04, "uthread_t id offset");
_Static_assert(offsetof(uthread_t, proc) == 0x08, "uthread_t proc offset");
_Static_assert(offsetof(uthread_t, data) == 0x0c, "uthread_t data offset");
#endif

typedef struct thread_context_t
{
    uint32_t calls;
    void *observed_data;
} thread_context_t;

static void RDP_CDECL record_callback(void *data)
{
    thread_context_t *context;

    context = (thread_context_t *)data;
    ++context->calls;
    context->observed_data = data;
}

static void test_selective_init_and_null_wait(void)
{
    uthread_t thread;
    uint32_t exit_code;
    void *sentinel;

    sentinel = (void *)(uintptr_t)0x1234u;
    thread.handle = sentinel;
    thread.id = UINT32_C(0xA5A5A5A5);
    thread.proc = record_callback;
    thread.data = sentinel;
    uthread_init(&thread);
    assert(thread.handle == NULL);
    assert(thread.id == UINT32_C(0xA5A5A5A5));
    assert(thread.proc == record_callback);
    assert(thread.data == sentinel);

    exit_code = 0;
    assert(uthread_wait_exit_code(&thread, &exit_code) == 0u);
    assert(exit_code == UINT32_MAX);
    uthread_destroy(&thread);
}

static void test_create_wait_destroy(void)
{
    thread_context_t context = {0};
    uthread_t thread;
    uint32_t exit_code;

    uthread_init(&thread);
    assert(uthread_create(&thread, record_callback, &context) == 0u);
    assert(thread.handle != NULL);
    assert(thread.proc == record_callback);
    assert(thread.data == &context);

    exit_code = UINT32_MAX;
    assert(uthread_wait_exit_code(&thread, &exit_code) == 0u);
    assert(exit_code == 0u);
    assert(context.calls == 1u);
    assert(context.observed_data == &context);
    uthread_destroy(&thread);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_selective_init_and_null_wait();
    test_create_wait_destroy();
    return 0;
}
