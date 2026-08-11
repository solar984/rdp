// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "connection.h"
#include "rdp.h"
#include "rdplib_platform.h"
#include "stats.h"
#include "tx.h"
#include "usend.h"

static rdp_global_statistics_t test_statistics;
static int test_backend_result;
static uint8_t test_header[RDP_WIRE_HEADER_MAX_BYTES];
static uint32_t test_header_bytes;
static uint32_t test_send_count;

static int test_usend(intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc)
{
    (void)endpoint;
    (void)destination;
    (void)use_encryption;
    (void)use_crc;

    assert(buffers != NULL);
    assert(buffer_count != 0);
    assert(buffers[0].bytes <= sizeof(test_header));
    memcpy(test_header, buffers[0].data, buffers[0].bytes);
    test_header_bytes = buffers[0].bytes;
    ++test_send_count;
    return test_backend_result;
}

#ifdef RDPLIB_SOURCE_FAITHFUL
int usend(intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc)
{
    return test_usend(endpoint, buffers, buffer_count, destination, use_encryption, use_crc);
}
#else
int rdplib_usend(connection_t *connection, intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc)
{
    (void)connection;
    return test_usend(endpoint, buffers, buffer_count, destination, use_encryption, use_crc);
}
#endif

static const char *selected_mode(void)
{
#ifdef RDPLIB_SOURCE_FAITHFUL
    return "source-faithful";
#else
    return "default";
#endif
}

static uint16_t read_network_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void initialize_connection(connection_t *connection, rdp_t *owner)
{
    uint32_t now_ms = rdplib_platform_current_time_ms();

    memset(connection, 0, sizeof(*connection));
    memset(owner, 0, sizeof(*owner));
    memset(&test_statistics, 0, sizeof(test_statistics));
    memset(test_header, 0, sizeof(test_header));
    test_header_bytes = 0;
    test_send_count = 0;

    connection->owner = owner;
    connection->transmit.address_family = RDP_TRANSMIT_ADDRESS_IPV4;
    connection->transmit.connected = 1;
    connection->transmit.delayed_ack_pending = 1;
    connection->transmit.delayed_ack_deadline_ms = now_ms;
    bandwidth_init(&connection->transmit.bandwidth);

    connection->receive.ack.received_through_message_id = UINT16_C(0x1234);
    connection->receive.ack.unreported_message_count = 1;
    connection->receive.ack.unreported_min_message_id = UINT16_C(0x1230);
    connection->receive.ack.unreported_max_message_id = UINT16_C(0x1232);
    g_rdp_stat = &test_statistics;
}

int main(void)
{
    connection_t connection;
    rdp_t owner;
    rdp_timeout_data_t timeout;
    uint32_t saved_deadline_ms;
    uint16_t first_flags;
    uint16_t second_flags;

    initialize_connection(&connection, &owner);
    saved_deadline_ms = connection.transmit.delayed_ack_deadline_ms;

    test_backend_result = 5;
    assert(tx_send_packet(&connection, NULL, 0, 0) == 5);
    assert(test_send_count == 1);
    assert(test_header_bytes == RDP_WIRE_HEADER_BASE_BYTES + 2u);
    first_flags = read_network_u16(test_header);
    assert((first_flags & RDP_FLAG_ACKTHRU) != 0);
    assert(connection.transmit.next_packet_sequence == 0);

    connection_recalc_event_timeout(&connection, &timeout);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(connection.receive.ack.unreported_message_count == 0);
    assert(connection.receive.ack.unreported_min_message_id == UINT16_C(0x1234));
    assert(connection.receive.ack.unreported_max_message_id == UINT16_C(0x1234));
    assert(connection.transmit.delayed_ack_pending == 0);
    assert(timeout.infinite);
    assert(connection.event_type == RDP_CONNECTION_EVENT_NONE);
#else
    assert(connection.receive.ack.unreported_message_count == 1);
    assert(connection.receive.ack.unreported_min_message_id == UINT16_C(0x1230));
    assert(connection.receive.ack.unreported_max_message_id == UINT16_C(0x1232));
    assert(connection.transmit.delayed_ack_pending == 1);
    assert(connection.transmit.delayed_ack_deadline_ms == saved_deadline_ms);
    assert(!timeout.infinite);
    assert(connection.event_type == RDP_CONNECTION_EVENT_TRANSMIT);
    assert((int32_t)(timeout.deadline_ms - rdplib_platform_current_time_ms()) > 0);
#endif

    test_backend_result = 0;
    assert(tx_send_packet(&connection, NULL, 0, 0) == 0);
    assert(test_send_count == 2);
    second_flags = read_network_u16(test_header);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(test_header_bytes == RDP_WIRE_HEADER_BASE_BYTES);
    assert((second_flags & (RDP_FLAG_ACKTHRU | RDP_FLAG_MASKOFFSET)) == 0);
#else
    assert(test_header_bytes == RDP_WIRE_HEADER_BASE_BYTES + 2u);
    assert((second_flags & RDP_FLAG_ACKTHRU) != 0);
    assert(connection.receive.ack.unreported_message_count == 0);
    assert(connection.transmit.delayed_ack_pending == 0);
#endif
    assert(connection.transmit.next_packet_sequence == 1);

    printf("build=%s first_flags=0x%04X second_flags=0x%04X pending_after_retry=%u report_after_retry=%u\n", selected_mode(), first_flags, second_flags,
           connection.transmit.delayed_ack_pending, connection.receive.ack.unreported_message_count);
    g_rdp_stat = NULL;
    return 0;
}
