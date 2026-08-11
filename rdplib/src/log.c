// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "net_error.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

// This was recovered from the clients but we omit the original call sites so this is essentially dead code just here for historical interest.
void discard_log_append(const char *format, ...)
{
    static FILE *discard_log;
    char timestamp[40];
    struct tm *local;
    time_t now;
    va_list arguments;

    if (!discard_log)
    {
        discard_log = fopen("Logs:discard.log", "a");
    }
    if (!discard_log)
    {
        return;
    }

    now = time(NULL);
    local = localtime(&now);
    sprintf(timestamp, "[%02u/%02u/%02u %02u:%02u:%02u] ", (uint32_t)local->tm_mon + 1u, (uint32_t)local->tm_mday, (uint32_t)(local->tm_year % 100), (uint32_t)local->tm_hour, (uint32_t)local->tm_min,
            (uint32_t)local->tm_sec);
    fputs(timestamp, discard_log);

    va_start(arguments, format);
    vfprintf(discard_log, format, arguments);
    va_end(arguments);
    fflush(discard_log);
}
