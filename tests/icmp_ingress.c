// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "packet.h"
#include "rdp.h"
#include "rdplib_platform.h"
#include "stats.h"
#include "rdpstat.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

enum
{
    TEST_AF_INET = 2,
    TEST_SOCKET_DATAGRAM = 2,
    TEST_PROTOCOL_UDP = 17
};

static rdp_stat test_statistics;

static void store_native_u16(uint8_t *destination, uint16_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static intptr_t create_loopback_receiver(uint8_t address[16])
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

static uint16_t get_network_port(const uint8_t address[16])
{
    return (uint16_t)(((uint16_t)address[2] << 8) | address[3]);
}

static void send_probe(rdp_t *owner, const uint8_t destination[16])
{
    static const uint8_t marker[] = {0x52, 0x44, 0x50, 0x21};

    assert(rdplib_platform_send_datagram(owner->udp_socket, marker, sizeof(marker), destination) == (int32_t)sizeof(marker));
}

int main(void)
{
    uint8_t open_address[16];
    uint8_t closed_address[16];
    rdp_t *owner = NULL;
    connection_t *open_connection = NULL;
    connection_t *closed_connection = NULL;
    msg_arrival_t *arrival;
    intptr_t open_receiver = -1;
    intptr_t closed_receiver = -1;

#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    fast_malloc_init(1024u * 1024u);

    assert(rdp_create(&owner, 0, 2, RDP_CREATE_REQUIRE_IPV4) == 0);
    assert(owner->icmp_socket == -1);
    assert(owner->trace_socket == -1);
    open_receiver = create_loopback_receiver(open_address);
    closed_receiver = create_loopback_receiver(closed_address);
    assert(rdp_connect(owner, &open_connection, "127.0.0.1", get_network_port(open_address), 0) == 0);
    assert(rdp_connect(owner, &closed_connection, "127.0.0.1", get_network_port(closed_address), 0) == 0);

    rdplib_platform_socket_close(closed_receiver);
    closed_receiver = -1;

    send_probe(owner, closed_address);
    arrival = rdp_receive(owner, 3000);
    assert(arrival != NULL);
    assert(msg_arrival_get_sender(arrival) == closed_connection);
    assert(closed_connection->tx_disconnect_reason == RDP_DISCONNECT_REASON_ICMP);
    assert(closed_connection->rx_icmp_type == 3);
    assert(closed_connection->rx_icmp_code == 3);
    assert(connection_connected(open_connection));
    fast_free(arrival);

    // A late diagnostic for the same endpoint must not repeat the abort or
    // publish another disconnect notification.
    send_probe(owner, closed_address);
    assert(rdp_receive(owner, 500) == NULL);
    assert(closed_connection->rx_icmp_received == 1);
    assert(test_statistics.connections_dropped_unreachable[3] == 1);
    assert(connection_connected(open_connection));

    rdplib_platform_socket_close(open_receiver);
    rdp_destroy(owner, 1);
    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return 0;
}
