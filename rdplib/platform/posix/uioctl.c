// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

#include <fcntl.h>

int rdplib_platform_socket_disable_blocking(intptr_t endpoint)
{
    int flags = fcntl((int)endpoint, F_GETFL, 0);
    if (flags < 0 || fcntl((int)endpoint, F_SETFL, flags | O_NONBLOCK) != 0)
    {
        return 1;
    }
    return 0;
}
