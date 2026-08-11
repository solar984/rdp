// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

void rdplib_platform_semaphore_init(rdplib_platform_semaphore_t *semaphore)
{
    semaphore->handle = NULL;
}

int rdplib_platform_semaphore_create(rdplib_platform_semaphore_t *semaphore)
{
    semaphore->handle = CreateSemaphoreA(NULL, 0, LONG_MAX, NULL);
    return semaphore->handle ? 0 : 3;
}

void rdplib_platform_semaphore_destroy(rdplib_platform_semaphore_t *semaphore)
{
    if (semaphore->handle)
    {
        CloseHandle(semaphore->handle);
    }
}

int rdplib_platform_semaphore_wait(rdplib_platform_semaphore_t *semaphore, int32_t timeout_ms)
{
    DWORD timeout = timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms;
    return WaitForSingleObject(semaphore->handle, timeout) == WAIT_OBJECT_0;
}

void rdplib_platform_semaphore_signal(rdplib_platform_semaphore_t *semaphore)
{
    (void)ReleaseSemaphore(semaphore->handle, 1, NULL);
}
