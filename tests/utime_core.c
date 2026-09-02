// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>

#include "rdplib_platform.h"
#include "utime.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(_Generic(&time_get_ms, uint32_t (*)(void): 1, default: 0), "time_get_ms signature");
_Static_assert(_Generic(&sleep_ms, void (*)(uint32_t): 1, default: 0), "sleep_ms signature");

#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&time_gettimeofday, void (*)(struct timeval *): 1, default: 0), "time_gettimeofday signature");
#endif

static int tick_is_between(uint32_t begin, uint32_t value, uint32_t end)
{
    return value - begin <= end - begin;
}

static void test_clock(void)
{
    uint32_t begin;
    uint32_t end;

    begin = time_get_ms();
    end = time_get_ms();
    assert(end - begin < 5000u);
}

static void test_sleep(void)
{
    uint32_t begin;
    uint32_t elapsed;

    begin = time_get_ms();
    sleep_ms(20u);
    elapsed = time_get_ms() - begin;
    assert(elapsed >= 1u);
    assert(elapsed < 5000u);

}

#ifdef RDP_DEAD_CODE
static void test_timeval_conversion(void)
{
    struct timeval tv;
    uint32_t begin;
    uint32_t converted;
    uint32_t end;

    begin = time_get_ms();
    time_gettimeofday(&tv);
    end = time_get_ms();
    assert(tv.tv_usec >= 0 && tv.tv_usec < 1000000);
    assert(tv.tv_usec % 1000 == 0);
    converted = (uint32_t)((uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u);
    assert(tick_is_between(begin, converted, end));
}
#endif

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_clock();
    test_sleep();
#ifdef RDP_DEAD_CODE
    test_timeval_conversion();
#endif
    return 0;
}
