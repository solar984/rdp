// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "rdplib_platform.h"
#include "tx.h"
#include "txq.h"
#include "utime.h"

enum
{
    TEST_HISTORY_BASE = 1000
};

static const char *selected_mode(void)
{
#ifdef RDPLIB_SOURCE_FAITHFUL
    return "source-faithful";
#else
    return "default";
#endif
}

static uint32_t history_span(const connection_t *connection)
{
    return (uint16_t)(connection->tx_next_msgid - connection->tx_acked_thru - 1u);
}

static void initialize_connection(connection_t *connection, uint32_t span)
{
    uint32_t bit;
    uint32_t now_ms = time_get_ms();

    assert(span <= RDP_BITARRAY_BITS);
    memset(connection, 0, sizeof(*connection));
    connection->cn_flags = RDP_CONNECTION_FEATURE_KEEPALIVE;
#ifndef RDPLIB_SOURCE_FAITHFUL
    connection->rdplib_keepalive_interval_ms = RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS;
#endif
    connection->tx_acked_thru = TEST_HISTORY_BASE;
    connection->tx_next_msgid = (uint16_t)(TEST_HISTORY_BASE + span + 1u);
    connection->tx_time_last_guaranteed_send = now_ms - 10000u;
    connection->tx_syn_sent = 1;
    connection->tx_connected = 1;
    connection->rx_time_last_msgid_arrival = now_ms ? now_ms : 1u;
    bitarray_clear(&connection->tx_outstanding_packet_mask);
    bandwidth_init(&connection->tx_bandwidth);
    tx_create(connection);

    for (bit = 0; bit < span; ++bit)
    {
        assert(bitarray_setbit(&connection->tx_outstanding_packet_mask, bit) == 0);
    }
}

static void destroy_connection(connection_t *connection)
{
    tx_destroy(connection);
    list_destroy(&connection->tx_outstanding_packets.list);
    list_destroy(&connection->tx_virgin_packets.list);
    list_destroy(&connection->tx_delayed_packets.list);
}

static void test_keepalive_boundary(void)
{
    connection_t connection;
    timeout_data timeout;
    uint16_t next_message_id;
    uint32_t last_enqueue_time_ms;
    int result;

    initialize_connection(&connection, RDP_BITARRAY_BITS - 1u);
    next_message_id = connection.tx_next_msgid;
    last_enqueue_time_ms = connection.tx_time_last_guaranteed_send;

    connection_recalc_event_timeout(&connection, &timeout);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(!timeout.infinite);
    assert(connection.cn_event_type == CONNECTION_EVENT_ALIVE);
#else
    assert(timeout.infinite);
    assert(connection.cn_event_type == CONNECTION_EVENT_NONE);
#endif

    result = tx_send_alive(&connection);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(result == RDP_CONNECTION_SEND_OK);
    assert(connection.tx_next_msgid == (uint16_t)(next_message_id + 1u));
    assert(history_span(&connection) == RDP_BITARRAY_BITS);
    assert(getbit(connection.tx_outstanding_packet_mask.bits, RDP_BITARRAY_BITS - 1u));
    assert(connection.tx_delayed_packets.list.size == 1);
#else
    assert(result == RDP_CONNECTION_SEND_HISTORY_FULL);
    assert(connection.tx_next_msgid == next_message_id);
    assert(connection.tx_time_last_guaranteed_send == last_enqueue_time_ms);
    assert(history_span(&connection) == RDP_BITARRAY_BITS - 1u);
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, RDP_BITARRAY_BITS - 1u));
    assert(connection.tx_delayed_packets.list.size == 0);
#endif

    printf("build=%s keepalive_result=%d history_span=%u next_index=%u\n", selected_mode(), result, history_span(&connection), history_span(&connection));
    destroy_connection(&connection);
}

static void test_fin_reservation(void)
{
    connection_t connection;
    uint16_t next_message_id;
    int result;

    initialize_connection(&connection, RDP_BITARRAY_BITS - 1u);
    next_message_id = connection.tx_next_msgid;
    result = tx_send_fin(&connection);
    assert(result == RDP_CONNECTION_SEND_OK);
    assert(connection.tx_fin_sent == 1);
    assert(connection.tx_next_msgid == (uint16_t)(next_message_id + 1u));
    assert(history_span(&connection) == RDP_BITARRAY_BITS);
    assert(getbit(connection.tx_outstanding_packet_mask.bits, RDP_BITARRAY_BITS - 1u));
    assert(connection.tx_delayed_packets.list.size == 1);
    destroy_connection(&connection);

    initialize_connection(&connection, RDP_BITARRAY_BITS);
    next_message_id = connection.tx_next_msgid;
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(history_span(&connection) == RDP_BITARRAY_BITS);
    assert((uint16_t)(connection.tx_next_msgid - connection.tx_acked_thru - 1u) == RDP_BITARRAY_BITS);
#else
    result = tx_send_fin(&connection);
    assert(result == RDP_CONNECTION_SEND_HISTORY_FULL);
    assert(connection.tx_fin_sent == 0);
    assert(connection.tx_next_msgid == next_message_id);
    assert(connection.tx_delayed_packets.list.size == 0);
#endif
    destroy_connection(&connection);
}

static void test_keepalive_below_boundary(void)
{
    connection_t connection;
    timeout_data timeout;
    int result;

    initialize_connection(&connection, RDP_BITARRAY_BITS - 2u);
    connection_recalc_event_timeout(&connection, &timeout);
    assert(!timeout.infinite);
    assert(connection.cn_event_type == CONNECTION_EVENT_ALIVE);

    result = tx_send_alive(&connection);
    assert(result == RDP_CONNECTION_SEND_OK);
    assert(history_span(&connection) == RDP_BITARRAY_BITS - 1u);
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, RDP_BITARRAY_BITS - 1u));
    destroy_connection(&connection);
}

int main(void)
{
    fast_malloc_init(1024u * 1024u);
    test_keepalive_boundary();
    test_fin_reservation();
    test_keepalive_below_boundary();
    fast_malloc_destroy();
    return 0;
}
