// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

#include <time.h>

uint32_t rdplib_platform_current_time_ms(void)
{
    return GetTickCount();
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
    Sleep(milliseconds);
}
