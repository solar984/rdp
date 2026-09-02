// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>
#include <string.h>

#include "iov.h"
#include "usend.h"

enum
{
    TEST_WOULD_BLOCK = 10035,
    TEST_NO_BUFFER_SPACE = 10055
};

static int32_t test_send_result;
static uint32_t test_socket_error;
static uint32_t test_send_calls;
static uint32_t test_error_calls;
static intptr_t test_socket;
static uint8_t test_packet[32];
static uint32_t test_packet_size;
static uint8_t test_destination[16];

int32_t usend_test_send_datagram(intptr_t socket, const uint8_t *packet, uint32_t packet_size, const uint8_t destination[16])
{
    assert(packet_size <= sizeof(test_packet));
    ++test_send_calls;
    test_socket = socket;
    test_packet_size = packet_size;
    memcpy(test_packet, packet, packet_size);
    memcpy(test_destination, destination, sizeof(test_destination));
    return test_send_result;
}

uint32_t usend_test_last_socket_error(void)
{
    ++test_error_calls;
    return test_socket_error;
}

static struct sockaddr *as_sockaddr(uint8_t address[16])
{
    return (struct sockaddr *)(void *)address;
}

static void reset_backend(int32_t send_result, uint32_t socket_error)
{
    test_send_result = send_result;
    test_socket_error = socket_error;
    test_send_calls = 0;
    test_error_calls = 0;
    test_socket = 0;
    test_packet_size = 0;
    memset(test_packet, 0, sizeof(test_packet));
    memset(test_destination, 0, sizeof(test_destination));
}

int main(void)
{
    static const uint8_t payload[] = {'r', 'd', 'p'};
    uint8_t destination[16];
    iov_t iov;

    memset(destination, 0x5a, sizeof(destination));
    iov.data = (void *)payload;
    iov.size = sizeof(payload);

    reset_backend((int32_t)sizeof(payload), 0);
    assert(usend(123, &iov, 1, as_sockaddr(destination), 0, 0) == 0);
    assert(test_send_calls == 1);
    assert(test_error_calls == 0);
    assert(test_socket == 123);
    assert(test_packet_size == sizeof(payload));
    assert(memcmp(test_packet, payload, sizeof(payload)) == 0);
    assert(memcmp(test_destination, destination, sizeof(destination)) == 0);

    reset_backend((int32_t)sizeof(payload) - 1, TEST_WOULD_BLOCK);
    assert(usend(123, &iov, 1, as_sockaddr(destination), 0, 0) == 5);
    assert(test_send_calls == 1);
    assert(test_error_calls == 1);

    reset_backend(-1, TEST_NO_BUFFER_SPACE);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(usend(123, &iov, 1, as_sockaddr(destination), 0, 0) == 1);
#else
    assert(usend(123, &iov, 1, as_sockaddr(destination), 0, 0) == 5);
#endif
    assert(test_send_calls == 1);
    assert(test_error_calls == 1);

    reset_backend(-1, 10054);
    assert(usend(123, &iov, 1, as_sockaddr(destination), 0, 0) == 1);
    assert(test_send_calls == 1);
    assert(test_error_calls == 1);
    return 0;
}
