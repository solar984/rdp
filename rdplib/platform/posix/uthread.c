// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

#include <stdlib.h>

typedef struct rdplib_platform_thread_record_t
{
    pthread_t thread;
    rdplib_platform_thread_entry_t entry;
    void *argument;
} rdplib_platform_thread_record_t;

static void *rdplib_platform_thread_start(void *argument)
{
    rdplib_platform_thread_record_t *thread = (rdplib_platform_thread_record_t *)argument;
    rdplib_platform_thread_entry_t entry = thread->entry;
    void *entry_argument = thread->argument;
    return (void *)(uintptr_t)entry(entry_argument);
}

int rdplib_platform_thread_create(void **thread_output, rdplib_platform_thread_entry_t entry, void *argument)
{
    rdplib_platform_thread_record_t *thread = (rdplib_platform_thread_record_t *)malloc(sizeof(*thread));
    int result;

    if (!thread)
    {
        return 2;
    }
    thread->entry = entry;
    thread->argument = argument;
    result = pthread_create(&thread->thread, NULL, rdplib_platform_thread_start, thread);
    if (result != 0)
    {
        free(thread);
        return 1;
    }
    *thread_output = thread;
    return 0;
}

void rdplib_platform_thread_wait(void *thread_pointer, uint32_t *exit_code)
{
    rdplib_platform_thread_record_t *thread = (rdplib_platform_thread_record_t *)thread_pointer;
    void *result = NULL;
    (void)pthread_join(thread->thread, &result);
    if (exit_code)
    {
        *exit_code = (uint32_t)(uintptr_t)result;
    }
}

void rdplib_platform_thread_destroy(void *thread_pointer)
{
    rdplib_platform_thread_record_t *thread = (rdplib_platform_thread_record_t *)thread_pointer;
    if (!thread)
    {
        return;
    }
    if (pthread_equal(thread->thread, pthread_self()))
    {
        (void)pthread_detach(thread->thread);
    }
    free(thread);
}
