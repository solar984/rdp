// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

int rdplib_platform_socket_disable_blocking(intptr_t endpoint)
{
    u_long enabled = 1;
    return ioctlsocket((SOCKET)endpoint, FIONBIO, &enabled) == 0 ? 0 : 1;
}
