// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>

#include "dpf.h"

#ifdef RDPLIB_DEBUG
_Static_assert(_Generic(&dpf, void (*)(uint32_t, const char *, ...): 1, default: 0), "dpf debug signature");
#endif

int main(void)
{
    uint32_t debug_only;
    uint32_t retail_log;

    debug_only = 0;
    retail_log = 0;
#ifdef RDPLIB_DEBUG
    ++debug_only;
    assert(debug_only == 1);
#else
    assert(debug_only == 0);
#endif

#ifdef RDPLIB_SOURCE_FAITHFUL
    ++retail_log;
    assert(retail_log == 1);
#else
    assert(retail_log == 0);
#endif
    return 0;
}
