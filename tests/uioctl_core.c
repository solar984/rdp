// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <sys/socket.h>
#endif

#include "rdplib_platform.h"

uint32_t disable_blocking(intptr_t s);

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(_Generic(&disable_blocking, uint32_t (*)(intptr_t): 1, default: 0), "disable_blocking signature");

#ifdef _WIN32
_Static_assert(sizeof(intptr_t) >= sizeof(SOCKET), "disable_blocking must preserve a native SOCKET");
#endif

static intptr_t create_bound_socket(void)
{
    struct sockaddr_in address;
    intptr_t endpoint;

    endpoint = rdplib_platform_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(endpoint != -1);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
    assert(bind((SOCKET)endpoint, (const struct sockaddr *)&address, (int)sizeof(address)) == 0);
#else
    assert(bind((int)endpoint, (const struct sockaddr *)&address, sizeof(address)) == 0);
#endif
    return endpoint;
}

static void verify_receive_would_block(intptr_t endpoint)
{
    char byte;

#ifdef _WIN32
    assert(recv((SOCKET)endpoint, &byte, 1, 0) == SOCKET_ERROR);
    assert(WSAGetLastError() == WSAEWOULDBLOCK);
#else
    assert(recv((int)endpoint, &byte, 1, 0) == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
#endif
}

static void test_canonical_entry(void)
{
    intptr_t endpoint;

    endpoint = create_bound_socket();
    assert(disable_blocking(endpoint) == 0u);
    verify_receive_would_block(endpoint);
    rdplib_platform_socket_close(endpoint);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    assert(rdplib_platform_network_startup(UINT16_C(0x0202)) == 0);
    assert(disable_blocking(-1) == 1u);
    test_canonical_entry();
    rdplib_platform_network_cleanup();
    return 0;
}
