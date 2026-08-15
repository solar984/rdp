// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include "protocol_limits.h"
#include "rdplib_constants.h"

_Static_assert(STREAMS_PER_CONNECTION == 20, "STREAMS_PER_CONNECTION changed");
_Static_assert(RDP_MAX_OUTSTANDING_IDS == 4096, "RDP_MAX_OUTSTANDING_IDS changed");
_Static_assert(RDP_FRAGMENT_COUNT_MAX == 100, "RDP_FRAGMENT_COUNT_MAX changed");
_Static_assert(STREAMS_PER_CONNECTION == RDP_STREAM_COUNT, "public stream count diverged");

int main(void)
{
    assert(STREAMS_PER_CONNECTION == 20);
    assert(RDP_MAX_OUTSTANDING_IDS == 4096);
    assert(RDP_FRAGMENT_COUNT_MAX == 100);
    return 0;
}
