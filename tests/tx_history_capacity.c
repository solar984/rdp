// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "rdplib_platform.h"
#include "tx.h"

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
    return (uint16_t)(connection->transmit.reliable_next_message_id - connection->transmit.acknowledged_through_message_id - 1u);
}

static void initialize_connection(connection_t *connection, uint32_t span)
{
    uint32_t bit;
    uint32_t now_ms = rdplib_platform_current_time_ms();

    assert(span <= RDP_BITARRAY_BITS);
    memset(connection, 0, sizeof(*connection));
    connection->options = RDP_CONNECTION_FEATURE_KEEPALIVE;
    connection->rdplib_keepalive_interval_ms = RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS;
    connection->transmit.acknowledged_through_message_id = TEST_HISTORY_BASE;
    connection->transmit.reliable_next_message_id = (uint16_t)(TEST_HISTORY_BASE + span + 1u);
    connection->transmit.last_reliable_enqueue_time_ms = now_ms - 10000u;
    connection->transmit.syn_sent = 1;
    connection->transmit.connected = 1;
    connection->receive.recording.last_reliable_receive_time_ms = now_ms ? now_ms : 1u;
    bitarray_clear(&connection->transmit.outstanding_message_ids);
    bandwidth_init(&connection->transmit.bandwidth);
    tx_create(connection);

    for (bit = 0; bit < span; ++bit)
    {
        assert(bitarray_setbit(&connection->transmit.outstanding_message_ids, bit) == 0);
    }
}

static void destroy_connection(connection_t *connection)
{
    tx_destroy(connection);
    list_destroy(&connection->transmit.sent_messages.messages);
    list_destroy(&connection->transmit.ready_messages.messages);
    list_destroy(&connection->transmit.window_blocked_messages.messages);
}

static void test_keepalive_boundary(void)
{
    connection_t connection;
    rdp_timeout_data_t timeout;
    uint16_t next_message_id;
    uint32_t last_enqueue_time_ms;
    int result;

    initialize_connection(&connection, RDP_BITARRAY_BITS - 1u);
    next_message_id = connection.transmit.reliable_next_message_id;
    last_enqueue_time_ms = connection.transmit.last_reliable_enqueue_time_ms;

    connection_recalc_event_timeout(&connection, &timeout);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(!timeout.infinite);
    assert(connection.event_type == RDP_CONNECTION_EVENT_KEEPALIVE);
#else
    assert(timeout.infinite);
    assert(connection.event_type == RDP_CONNECTION_EVENT_NONE);
#endif

    result = tx_send_alive(&connection);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(result == RDP_CONNECTION_SEND_OK);
    assert(connection.transmit.reliable_next_message_id == (uint16_t)(next_message_id + 1u));
    assert(history_span(&connection) == RDP_BITARRAY_BITS);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, RDP_BITARRAY_BITS - 1u));
    assert(connection.transmit.window_blocked_messages.messages.count == 1);
#else
    assert(result == RDP_CONNECTION_SEND_HISTORY_FULL);
    assert(connection.transmit.reliable_next_message_id == next_message_id);
    assert(connection.transmit.last_reliable_enqueue_time_ms == last_enqueue_time_ms);
    assert(history_span(&connection) == RDP_BITARRAY_BITS - 1u);
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, RDP_BITARRAY_BITS - 1u));
    assert(connection.transmit.window_blocked_messages.messages.count == 0);
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
    next_message_id = connection.transmit.reliable_next_message_id;
    result = tx_send_fin(&connection);
    assert(result == RDP_CONNECTION_SEND_OK);
    assert(connection.transmit.fin_sent == 1);
    assert(connection.transmit.reliable_next_message_id == (uint16_t)(next_message_id + 1u));
    assert(history_span(&connection) == RDP_BITARRAY_BITS);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, RDP_BITARRAY_BITS - 1u));
    assert(connection.transmit.window_blocked_messages.messages.count == 1);
    destroy_connection(&connection);

    initialize_connection(&connection, RDP_BITARRAY_BITS);
    next_message_id = connection.transmit.reliable_next_message_id;
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(history_span(&connection) == RDP_BITARRAY_BITS);
    assert((uint16_t)(connection.transmit.reliable_next_message_id - connection.transmit.acknowledged_through_message_id - 1u) == RDP_BITARRAY_BITS);
#else
    result = tx_send_fin(&connection);
    assert(result == RDP_CONNECTION_SEND_HISTORY_FULL);
    assert(connection.transmit.fin_sent == 0);
    assert(connection.transmit.reliable_next_message_id == next_message_id);
    assert(connection.transmit.window_blocked_messages.messages.count == 0);
#endif
    destroy_connection(&connection);
}

static void test_keepalive_below_boundary(void)
{
    connection_t connection;
    rdp_timeout_data_t timeout;
    int result;

    initialize_connection(&connection, RDP_BITARRAY_BITS - 2u);
    connection_recalc_event_timeout(&connection, &timeout);
    assert(!timeout.infinite);
    assert(connection.event_type == RDP_CONNECTION_EVENT_KEEPALIVE);

    result = tx_send_alive(&connection);
    assert(result == RDP_CONNECTION_SEND_OK);
    assert(history_span(&connection) == RDP_BITARRAY_BITS - 1u);
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, RDP_BITARRAY_BITS - 1u));
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
