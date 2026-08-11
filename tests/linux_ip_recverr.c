// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

#include "rdplib_platform.h"

enum
{
    TEST_NETWORK_VERSION = 0x0202,
    TEST_AF_INET = 2,
    TEST_SOCKET_DATAGRAM = 2,
    TEST_PROTOCOL_UDP = 17,
    TEST_IO_SOURCE_IPV4 = 1
};

static void store_native_u16(uint8_t *destination, uint16_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static intptr_t create_bound_loopback(uint8_t address[16])
{
    intptr_t endpoint;
    uint32_t address_bytes = 16;

    memset(address, 0, 16);
    store_native_u16(address, TEST_AF_INET);
    address[4] = 127;
    address[7] = 1;

    endpoint = rdplib_platform_socket_create(TEST_AF_INET, TEST_SOCKET_DATAGRAM, TEST_PROTOCOL_UDP);
    assert(endpoint != -1);
    assert(rdplib_platform_socket_bind(endpoint, address, 16) == 0);
    assert(rdplib_platform_socket_get_name(endpoint, address, &address_bytes) == 0);
    assert(address_bytes >= 8);
    return endpoint;
}

static void expect_port_unreachable(intptr_t sender, const uint8_t destination[16])
{
    rdplib_platform_icmp_error_t error;
    uint32_t ready_sources = 0;

    assert(rdplib_platform_wait(sender, -1, -1, TEST_IO_SOURCE_IPV4, 0, 3, 0, &ready_sources) > 0);
    assert((ready_sources & TEST_IO_SOURCE_IPV4) != 0);
    assert(rdplib_platform_receive_icmp_error(sender, &error));
    assert(error.type == 3);
    assert(error.code == 3);
    assert(memcmp(error.remote_address, destination, 8) == 0);
}

int main(void)
{
    const uint8_t marker = 0x5A;
    uint8_t sender_address[16];
    uint8_t first_address[16];
    uint8_t second_address[16];
    intptr_t sender = -1;
    intptr_t first_receiver = -1;
    intptr_t second_receiver = -1;

    assert(rdplib_platform_network_startup(TEST_NETWORK_VERSION) == 0);
    sender = create_bound_loopback(sender_address);
    first_receiver = create_bound_loopback(first_address);
    second_receiver = create_bound_loopback(second_address);
    assert(rdplib_platform_enable_icmp_errors(sender) == 0);

    rdplib_platform_socket_close(first_receiver);
    first_receiver = -1;
    assert(rdplib_platform_send_datagram(sender, &marker, sizeof(marker), first_address) == sizeof(marker));
    expect_port_unreachable(sender, first_address);

    rdplib_platform_socket_close(second_receiver);
    second_receiver = -1;
    assert(rdplib_platform_send_datagram(sender, &marker, sizeof(marker), second_address) == sizeof(marker));
    expect_port_unreachable(sender, second_address);

    rdplib_platform_socket_close(sender);
    rdplib_platform_network_cleanup();
    return 0;
}
