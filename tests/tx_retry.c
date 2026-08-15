// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "connection.h"
#include "rdp.h"
#include "rdplib_platform.h"
#include "stats.h"
#include "rdpstat.h"
#include "tx.h"
#include "utime.h"
#include "usend.h"

static rdp_stat test_statistics;
static int test_backend_result;
static uint8_t test_header[RDP_WIRE_HEADER_MAX_BYTES];
static uint32_t test_header_bytes;
static uint32_t test_send_count;
static uint32_t test_iov_lengths[2];

static uint32_t test_usend(intptr_t socket, iov_t *iov, uint32_t iov_len, struct sockaddr *remote_addr, uint32_t encrypt, uint32_t crc)
{
    (void)socket;
    (void)remote_addr;
    (void)encrypt;
    (void)crc;

    assert(iov != NULL);
    assert(iov_len != 0);
    assert(iov[0].size <= sizeof(test_header));
    assert(test_send_count < sizeof(test_iov_lengths) / sizeof(test_iov_lengths[0]));
    test_iov_lengths[test_send_count] = iov_len;
    memcpy(test_header, iov[0].data, iov[0].size);
    test_header_bytes = iov[0].size;
    ++test_send_count;
    return (uint32_t)test_backend_result;
}

#ifdef RDPLIB_SOURCE_FAITHFUL
uint32_t usend(intptr_t socket, iov_t *iov, uint32_t iov_len, struct sockaddr *remote_addr, uint32_t encrypt, uint32_t crc)
{
    return test_usend(socket, iov, iov_len, remote_addr, encrypt, crc);
}
#else
uint32_t rdplib_usend(connection_t *connection, intptr_t socket, iov_t *iov, uint32_t iov_len, struct sockaddr *remote_addr, uint32_t encrypt, uint32_t crc)
{
    (void)connection;
    return test_usend(socket, iov, iov_len, remote_addr, encrypt, crc);
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
    uint32_t now_ms = time_get_ms();
    uint16_t family = RDP_TRANSMIT_ADDRESS_IPV4;

    memset(connection, 0, sizeof(*connection));
    memset(owner, 0, sizeof(*owner));
    memset(&test_statistics, 0, sizeof(test_statistics));
    memset(test_header, 0, sizeof(test_header));
    memset(test_iov_lengths, 0, sizeof(test_iov_lengths));
    test_header_bytes = 0;
    test_send_count = 0;

    connection->cn_rdp = owner;
    memcpy(&connection->tx_remote_addr, &family, sizeof(family));
    connection->tx_connected = 1;
    connection->tx_delayed_ack = 1;
    connection->tx_ack_time = now_ms;
    bandwidth_init(&connection->tx_bandwidth);

    connection->rx_received_all_thru = UINT16_C(0x1234);
    connection->rx_msgid_count = 1;
    connection->rx_msgid_lo = UINT16_C(0x1230);
    connection->rx_msgid_hi = UINT16_C(0x1232);
    umutex_create(&connection->cn_lock);
    umutex_lock(&connection->cn_lock);
    umutex_create(&owner->serial.lock);
    g_rdp_stat = &test_statistics;
}

int main(void)
{
    connection_t connection;
    rdp_t owner;
    timeout_data timeout;
    uint32_t saved_deadline_ms;
    uint16_t first_flags;
    uint16_t second_flags;

#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    initialize_connection(&connection, &owner);
    saved_deadline_ms = connection.tx_ack_time;

    test_backend_result = 5;
    assert(tx_send_packet(&connection, NULL, 0, 0) == 5);
    assert(test_send_count == 1);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(test_iov_lengths[0] == 2);
#else
    assert(test_iov_lengths[0] == 1);
#endif
    assert(test_header_bytes == RDP_WIRE_HEADER_BASE_BYTES + 2u);
    first_flags = read_network_u16(test_header);
    assert((first_flags & RDP_FLAG_ACKTHRU) != 0);
    assert(connection.tx_next_seqnum == 0);

    connection_recalc_event_timeout(&connection, &timeout);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(connection.rx_msgid_count == 0);
    assert(connection.rx_msgid_lo == UINT16_C(0x1234));
    assert(connection.rx_msgid_hi == UINT16_C(0x1234));
    assert(connection.tx_delayed_ack == 0);
    assert(timeout.infinite);
    assert(connection.cn_event_type == CONNECTION_EVENT_NONE);
#else
    assert(connection.rx_msgid_count == 1);
    assert(connection.rx_msgid_lo == UINT16_C(0x1230));
    assert(connection.rx_msgid_hi == UINT16_C(0x1232));
    assert(connection.tx_delayed_ack == 1);
    assert(connection.tx_ack_time == saved_deadline_ms);
    assert(!timeout.infinite);
    assert(connection.cn_event_type == CONNECTION_EVENT_TX);
    assert((int32_t)(timeout.time - time_get_ms()) > 0);
#endif

    test_backend_result = 0;
    assert(tx_send_packet(&connection, NULL, 0, 0) == 0);
    assert(test_send_count == 2);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(test_iov_lengths[1] == 2);
#else
    assert(test_iov_lengths[1] == 1);
#endif
    second_flags = read_network_u16(test_header);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(test_header_bytes == RDP_WIRE_HEADER_BASE_BYTES);
    assert((second_flags & (RDP_FLAG_ACKTHRU | RDP_FLAG_MASKOFFSET)) == 0);
#else
    assert(test_header_bytes == RDP_WIRE_HEADER_BASE_BYTES + 2u);
    assert((second_flags & RDP_FLAG_ACKTHRU) != 0);
    assert(connection.rx_msgid_count == 0);
    assert(connection.tx_delayed_ack == 0);
#endif
    assert(connection.tx_next_seqnum == 1);

    printf("build=%s first_flags=0x%04X second_flags=0x%04X pending_after_retry=%u report_after_retry=%u\n", selected_mode(), first_flags, second_flags,
           connection.tx_delayed_ack, connection.rx_msgid_count);
    umutex_unlock(&connection.cn_lock);
    umutex_destroy(&connection.cn_lock);
    umutex_destroy(&owner.serial.lock);
    g_rdp_stat = NULL;
    return 0;
}
