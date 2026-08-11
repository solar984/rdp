// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

#include <errno.h>

void rdplib_platform_event_init(rdplib_platform_event_t *event)
{
    event->created = 0;
}

int rdplib_platform_event_create(rdplib_platform_event_t *event)
{
    // The Mac uevent_create calls usemaphore_create but discards its result and always returns 0.
    if (sem_init(&event->value, 0, 0) == 0)
    {
        event->created = 1;
    }
    return 0;
}

void rdplib_platform_event_destroy(rdplib_platform_event_t *event)
{
    if (event->created)
    {
        (void)sem_destroy(&event->value);
    }
}

void rdplib_platform_event_signal(rdplib_platform_event_t *event)
{
    (void)sem_post(&event->value);
}

int rdplib_platform_event_wait(rdplib_platform_event_t *event)
{
    int result;

    do
    {
        result = sem_wait(&event->value);
    } while (result != 0 && errno == EINTR);

    return result == 0;
}
