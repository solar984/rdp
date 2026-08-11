// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

#include <process.h>
#include <stdlib.h>

typedef struct rdplib_platform_thread_record_t
{
    HANDLE handle;
    rdplib_platform_thread_entry_t entry;
    void *argument;
} rdplib_platform_thread_record_t;

static unsigned __stdcall rdplib_platform_thread_start(void *argument)
{
    rdplib_platform_thread_record_t *thread = (rdplib_platform_thread_record_t *)argument;
    rdplib_platform_thread_entry_t entry = thread->entry;
    void *entry_argument = thread->argument;
    return (unsigned)entry(entry_argument);
}

int rdplib_platform_thread_create(void **thread_output, rdplib_platform_thread_entry_t entry, void *argument)
{
    rdplib_platform_thread_record_t *thread = (rdplib_platform_thread_record_t *)malloc(sizeof(*thread));
    uintptr_t handle;

    if (!thread)
    {
        return 2;
    }
    thread->entry = entry;
    thread->argument = argument;
    handle = _beginthreadex(NULL, 0, rdplib_platform_thread_start, thread, 0, NULL);
    if (!handle)
    {
        free(thread);
        return 1;
    }
    thread->handle = (HANDLE)handle;
    (void)SetThreadPriority(thread->handle, 2);
    *thread_output = thread;
    return 0;
}

void rdplib_platform_thread_wait(void *thread_pointer, uint32_t *exit_code)
{
    rdplib_platform_thread_record_t *thread = (rdplib_platform_thread_record_t *)thread_pointer;
    DWORD code = 0;

    (void)WaitForSingleObject(thread->handle, INFINITE);
    (void)GetExitCodeThread(thread->handle, &code);
    if (exit_code)
    {
        *exit_code = (uint32_t)code;
    }
}

void rdplib_platform_thread_destroy(void *thread_pointer)
{
    rdplib_platform_thread_record_t *thread = (rdplib_platform_thread_record_t *)thread_pointer;
    if (thread)
    {
        (void)CloseHandle(thread->handle);
        free(thread);
    }
}
