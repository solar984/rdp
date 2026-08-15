// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "stats.h"
#include "rdpstat.h"
#include "tx.h"
#include "txq.h"

static rdp_stat test_statistics;

static const char *selected_mode(void)
{
#ifdef RDPLIB_SOURCE_FAITHFUL
    return "source-faithful";
#else
    return "default";
#endif
}

static rdp_rx_arrival_disposition_t expected_unsent_disposition(void)
{
#ifdef RDPLIB_SOURCE_FAITHFUL
    return RDP_RX_ACCEPT;
#else
    return RDP_RX_DISCARD;
#endif
}

static void store_network_u16(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static rdp_header_t make_ack_header(uint16_t flags, const uint8_t *ack_data)
{
    rdp_header_t header;

    memset(&header, 0, sizeof(header));
    header.options = flags;
    header.ack = (uint16_t *)(uintptr_t)ack_data;
    return header;
}

static void initialize_connection(connection_t *connection, uint16_t acknowledged_through, uint16_t next_message_id)
{
    memset(connection, 0, sizeof(*connection));
    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    connection->tx_connected = 1;
    connection->tx_acked_thru = acknowledged_through;
    connection->tx_next_msgid = next_message_id;
    bitarray_clear(&connection->tx_outstanding_packet_mask);
    tx_create(connection);
}

static msg_outgoing_t *queue_message(txq_t *queue, uint16_t message_id, int sent)
{
    msg_outgoing_t *message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(*message) + 2u);

    assert(message != NULL);
    memset(message, 0, sizeof(*message));
    message->options = RDP_FLAG_MSGID;
    message->msgid = message_id;
    msg_outgoing_init(message);
    if (sent)
    {
        message->time_first_sent = 1;
        message->time_last_sent = 1;
        message->attempts = 2;
    }
    txq_add_tail(queue, message);
    return message;
}

static rdp_rx_arrival_disposition_t validate_and_record(connection_t *connection, rdp_header_t *header)
{
    uint32_t field_bytes;
    rdp_rx_arrival_disposition_t disposition = tx_validate_ack_arrival(connection, header, &field_bytes);

    if (disposition == RDP_RX_ACCEPT)
    {
        tx_record_ack_arrival(connection, header);
    }
    return disposition;
}

static rdp_rx_arrival_disposition_t test_ready_selective_claim(void)
{
    connection_t connection;
    rdp_header_t header;
    uint8_t ack_data[2];
    rdp_rx_arrival_disposition_t disposition;

    initialize_connection(&connection, 100, 103);
    connection.tx_syn_sent = 1;
    bitarray_setbit(&connection.tx_outstanding_packet_mask, 1);
    queue_message(&connection.tx_virgin_packets, 102, 0);
    store_network_u16(ack_data, 102);
    header = make_ack_header(RDP_FLAG_MASKOFFSET, ack_data);

    disposition = validate_and_record(&connection, &header);
    assert(disposition == expected_unsent_disposition());
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, 1));
    assert(connection.tx_syn_acked == 1);
    assert(test_statistics.messages_acked == 1);
#else
    assert(getbit(connection.tx_outstanding_packet_mask.bits, 1));
    assert(connection.tx_syn_acked == 0);
    assert(test_statistics.acks_for_unsent_messages == 1);
    assert(connection.stat.acks_for_unsent_messages == 1);
    assert(test_statistics.messages_acked == 0);
#endif
    assert(connection.tx_virgin_packets.list.size == 1);
    assert(connection.tx_acked_thru == 100);
    tx_destroy(&connection);
    return disposition;
}

static rdp_rx_arrival_disposition_t test_window_blocked_selective_claim(void)
{
    connection_t connection;
    rdp_header_t header;
    uint8_t ack_data[2];
    rdp_rx_arrival_disposition_t disposition;

    initialize_connection(&connection, 100, 221);
    bitarray_setbit(&connection.tx_outstanding_packet_mask, 119);
    queue_message(&connection.tx_delayed_packets, 220, 0);
    store_network_u16(ack_data, 220);
    header = make_ack_header(RDP_FLAG_MASKOFFSET, ack_data);

    disposition = validate_and_record(&connection, &header);
    assert(disposition == expected_unsent_disposition());
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, 119));
    assert(test_statistics.messages_acked == 1);
#else
    assert(getbit(connection.tx_outstanding_packet_mask.bits, 119));
    assert(test_statistics.acks_for_unsent_messages == 1);
    assert(connection.stat.acks_for_unsent_messages == 1);
    assert(test_statistics.messages_acked == 0);
#endif
    assert(connection.tx_delayed_packets.list.size == 1);
    assert(connection.tx_acked_thru == 100);
    tx_destroy(&connection);
    return disposition;
}

static rdp_rx_arrival_disposition_t test_mixed_cumulative_claim(void)
{
    connection_t connection;
    rdp_header_t header;
    uint8_t ack_data[2];
    rdp_rx_arrival_disposition_t disposition;

    initialize_connection(&connection, 100, 104);
    bitarray_setbit(&connection.tx_outstanding_packet_mask, 0);
    bitarray_setbit(&connection.tx_outstanding_packet_mask, 1);
    bitarray_setbit(&connection.tx_outstanding_packet_mask, 2);
    queue_message(&connection.tx_outstanding_packets, 101, 1);
    queue_message(&connection.tx_outstanding_packets, 102, 1);
    queue_message(&connection.tx_virgin_packets, 103, 0);
    store_network_u16(ack_data, 103);
    header = make_ack_header(RDP_FLAG_ACKTHRU, ack_data);

    disposition = validate_and_record(&connection, &header);
    assert(disposition == expected_unsent_disposition());
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(connection.tx_acked_thru == 103);
    assert(connection.tx_outstanding_packets.list.size == 0);
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, 0));
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, 1));
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, 2));
    assert(test_statistics.messages_acked == 3);
#else
    assert(connection.tx_acked_thru == 100);
    assert(connection.tx_outstanding_packets.list.size == 2);
    assert(getbit(connection.tx_outstanding_packet_mask.bits, 0));
    assert(getbit(connection.tx_outstanding_packet_mask.bits, 1));
    assert(getbit(connection.tx_outstanding_packet_mask.bits, 2));
    assert(test_statistics.acks_for_unsent_messages == 1);
    assert(connection.stat.acks_for_unsent_messages == 1);
    assert(test_statistics.messages_acked == 0);
#endif
    assert(connection.tx_virgin_packets.list.size == 1);
    tx_destroy(&connection);
    return disposition;
}

static rdp_rx_arrival_disposition_t test_sent_and_redundant_claims(void)
{
    connection_t connection;
    rdp_header_t header;
    msg_outgoing_t *message;
    uint8_t ack_data[2];
    rdp_rx_arrival_disposition_t disposition;

    initialize_connection(&connection, 100, 103);
    bitarray_setbit(&connection.tx_outstanding_packet_mask, 0);
    bitarray_setbit(&connection.tx_outstanding_packet_mask, 1);
    queue_message(&connection.tx_outstanding_packets, 101, 1);
    queue_message(&connection.tx_virgin_packets, 102, 0);

    store_network_u16(ack_data, 101);
    header = make_ack_header(RDP_FLAG_MASKOFFSET, ack_data);
    disposition = validate_and_record(&connection, &header);
    assert(disposition == RDP_RX_ACCEPT);
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, 0));
    assert(connection.tx_outstanding_packets.list.size == 0);

    disposition = validate_and_record(&connection, &header);
    assert(disposition == RDP_RX_ACCEPT);
    assert(test_statistics.duplicate_acks == 1);

    message = txq_remove_head(&connection.tx_virgin_packets);
    assert(message != NULL);
    message->time_first_sent = 1;
    message->time_last_sent = 1;
    message->attempts = 2;
    txq_add_tail(&connection.tx_outstanding_packets, message);

    store_network_u16(ack_data, 102);
    disposition = validate_and_record(&connection, &header);
    assert(disposition == RDP_RX_ACCEPT);
    assert(!getbit(connection.tx_outstanding_packet_mask.bits, 1));
    assert(connection.tx_outstanding_packets.list.size == 0);
    assert(connection.tx_virgin_packets.list.size == 0);
    tx_destroy(&connection);
    return disposition;
}

int main(void)
{
    rdp_rx_arrival_disposition_t ready_disposition;
    rdp_rx_arrival_disposition_t blocked_disposition;
    rdp_rx_arrival_disposition_t mixed_disposition;
    rdp_rx_arrival_disposition_t sent_disposition;

    fast_malloc_init(1024u * 1024u);
    ready_disposition = test_ready_selective_claim();
    blocked_disposition = test_window_blocked_selective_claim();
    mixed_disposition = test_mixed_cumulative_claim();
    sent_disposition = test_sent_and_redundant_claims();
    fast_malloc_destroy();
    g_rdp_stat = NULL;

    printf("build=%s ready=%d blocked=%d mixed=%d sent=%d\n", selected_mode(), ready_disposition, blocked_disposition, mixed_disposition, sent_disposition);
    return 0;
}
