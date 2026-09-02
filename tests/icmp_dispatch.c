// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "packet.h"
#include "rdp.h"
#include "rdplib_rdp.h"
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

_Static_assert(_Generic(&rdplib_rdp_handle_reported_icmp,
                        void (*)(rdp_t *, struct sockaddr *, uint8_t, uint8_t, uint8_t, uint8_t, struct sockaddr_in *): 1,
                        default: 0),
               "rdplib_rdp_handle_reported_icmp internal signature");

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

int main(void)
{
    struct sockaddr_in unknown_source;
    uint8_t first_address[16];
    uint8_t second_address[16];
    rdp_t *owner = NULL;
    connection_t *first = NULL;
    connection_t *second = NULL;
    msg_arrival_t *arrival;
    intptr_t first_receiver = -1;
    intptr_t second_receiver = -1;
    uint16_t first_port;
    uint16_t second_port;

#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    fast_malloc_init(1024u * 1024u);
    memset(&unknown_source, 0, sizeof(unknown_source));

    assert(rdp_create(&owner, 0, 2, RDP_CREATE_REQUIRE_IPV4) == 0);
    first_receiver = create_loopback_receiver(first_address);
    second_receiver = create_loopback_receiver(second_address);
    first_port = (uint16_t)(((uint16_t)first_address[2] << 8) | first_address[3]);
    second_port = (uint16_t)(((uint16_t)second_address[2] << 8) | second_address[3]);

    assert(rdp_connect(owner, &first, "127.0.0.1", first_port, 0) == 0);
    assert(rdp_connect(owner, &second, "127.0.0.1", second_port, 0) == 0);
    assert(connection_connected(first));
    assert(connection_connected(second));

    rdplib_rdp_handle_reported_icmp(owner, &first->tx_remote_addr, 3, 3, 0, 0, &unknown_source);
    rdplib_rdp_handle_reported_icmp(owner, &first->tx_remote_addr, 3, 3, 0, 0, &unknown_source);

    assert(!connection_connected(first));
    assert(first->tx_disconnect_reason == RDP_DISCONNECT_REASON_ICMP);
    assert(first->rx_icmp_received == 1);
    assert(test_statistics.connections_dropped_unreachable[3] == 1);
    assert(connection_connected(second));

    arrival = rdp_receive(owner, 1000);
    assert(arrival != NULL);
    assert(msg_arrival_get_sender(arrival) == first);
    assert(msg_arrival_get_size(arrival) == 0);
    fast_free(arrival);
    assert(rdp_receive(owner, 50) == NULL);

    rdplib_platform_socket_close(second_receiver);
    rdplib_platform_socket_close(first_receiver);
    rdp_destroy(owner, 1);
    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return 0;
}
