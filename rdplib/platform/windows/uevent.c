// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

void rdplib_platform_event_init(rdplib_platform_event_t *event)
{
    event->handle = NULL;
}

int rdplib_platform_event_create(rdplib_platform_event_t *event)
{
    event->handle = CreateEventA(NULL, FALSE, FALSE, NULL);
    return event->handle ? 0 : 3;
}

void rdplib_platform_event_destroy(rdplib_platform_event_t *event)
{
    if (event->handle)
    {
        (void)CloseHandle(event->handle);
    }
}

void rdplib_platform_event_signal(rdplib_platform_event_t *event)
{
    (void)SetEvent(event->handle);
}

int rdplib_platform_event_wait(rdplib_platform_event_t *event)
{
    return (int)WaitForSingleObjectEx(event->handle, INFINITE, FALSE);
}
