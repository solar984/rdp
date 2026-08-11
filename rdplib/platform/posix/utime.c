// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

#include <errno.h>
#include <time.h>

uint32_t rdplib_platform_current_time_ms(void)
{
    struct timespec now;

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u + (uint32_t)now.tv_nsec / 1000000u);
}

uint32_t time_get_ms(void)
{
    return rdplib_platform_current_time_ms();
}

uint32_t rdplib_platform_wall_time_seconds(void)
{
    return (uint32_t)time(NULL);
}

void rdplib_platform_sleep_ms(uint32_t milliseconds)
{
    struct timespec request;
    request.tv_sec = milliseconds / 1000u;
    request.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    while (nanosleep(&request, &request) != 0 && errno == EINTR)
    {
    }
}
