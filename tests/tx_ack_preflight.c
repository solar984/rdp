// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "stats.h"
#include "tx.h"

static rdp_global_statistics_t test_statistics;

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

static _rdp_header_t make_ack_header(uint16_t flags, const uint8_t *ack_data)
{
    _rdp_header_t header;

    memset(&header, 0, sizeof(header));
    header.flags = flags;
    header.ack_data = ack_data;
    return header;
}

static void initialize_connection(connection_t *connection, uint16_t acknowledged_through, uint16_t next_message_id)
{
    memset(connection, 0, sizeof(*connection));
    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    connection->transmit.connected = 1;
    connection->transmit.acknowledged_through_message_id = acknowledged_through;
    connection->transmit.reliable_next_message_id = next_message_id;
    bitarray_clear(&connection->transmit.outstanding_message_ids);
    tx_create(connection);
}

static msg_outgoing_t *queue_message(rdp_txq_t *queue, uint16_t message_id, int sent)
{
    msg_outgoing_t *message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(*message) + 2u);

    assert(message != NULL);
    memset(message, 0, sizeof(*message));
    message->flags = RDP_FLAG_MSGID;
    message->message_id = message_id;
    msg_outgoing_init(message);
    if (sent)
    {
        message->first_sent_time_ms = 1;
        message->last_sent_time_ms = 1;
        message->transmission_count = 2;
    }
    queue->queued_bytes += message->serialized_bytes;
    list_add_tail(&queue->messages, &message->link);
    return message;
}

static rdp_rx_arrival_disposition_t validate_and_record(connection_t *connection, const _rdp_header_t *header)
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
    _rdp_header_t header;
    uint8_t ack_data[2];
    rdp_rx_arrival_disposition_t disposition;

    initialize_connection(&connection, 100, 103);
    connection.transmit.syn_sent = 1;
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 1);
    queue_message(&connection.transmit.ready_messages, 102, 0);
    store_network_u16(ack_data, 102);
    header = make_ack_header(RDP_FLAG_MASKOFFSET, ack_data);

    disposition = validate_and_record(&connection, &header);
    assert(disposition == expected_unsent_disposition());
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, 1));
    assert(connection.transmit.syn_acknowledged == 1);
    assert(test_statistics.acknowledgement_message_ids_processed == 1);
#else
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 1));
    assert(connection.transmit.syn_acknowledged == 0);
    assert(test_statistics.future_or_unsent_acknowledgements == 1);
    assert(connection.receive.recording.statistics.future_or_unsent_ack_count == 1);
    assert(test_statistics.acknowledgement_message_ids_processed == 0);
#endif
    assert(connection.transmit.ready_messages.messages.count == 1);
    assert(connection.transmit.acknowledged_through_message_id == 100);
    tx_destroy(&connection);
    return disposition;
}

static rdp_rx_arrival_disposition_t test_window_blocked_selective_claim(void)
{
    connection_t connection;
    _rdp_header_t header;
    uint8_t ack_data[2];
    rdp_rx_arrival_disposition_t disposition;

    initialize_connection(&connection, 100, 221);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 119);
    queue_message(&connection.transmit.window_blocked_messages, 220, 0);
    store_network_u16(ack_data, 220);
    header = make_ack_header(RDP_FLAG_MASKOFFSET, ack_data);

    disposition = validate_and_record(&connection, &header);
    assert(disposition == expected_unsent_disposition());
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, 119));
    assert(test_statistics.acknowledgement_message_ids_processed == 1);
#else
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 119));
    assert(test_statistics.future_or_unsent_acknowledgements == 1);
    assert(connection.receive.recording.statistics.future_or_unsent_ack_count == 1);
    assert(test_statistics.acknowledgement_message_ids_processed == 0);
#endif
    assert(connection.transmit.window_blocked_messages.messages.count == 1);
    assert(connection.transmit.acknowledged_through_message_id == 100);
    tx_destroy(&connection);
    return disposition;
}

static rdp_rx_arrival_disposition_t test_mixed_cumulative_claim(void)
{
    connection_t connection;
    _rdp_header_t header;
    uint8_t ack_data[2];
    rdp_rx_arrival_disposition_t disposition;

    initialize_connection(&connection, 100, 104);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 0);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 1);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 2);
    queue_message(&connection.transmit.sent_messages, 101, 1);
    queue_message(&connection.transmit.sent_messages, 102, 1);
    queue_message(&connection.transmit.ready_messages, 103, 0);
    store_network_u16(ack_data, 103);
    header = make_ack_header(RDP_FLAG_ACKTHRU, ack_data);

    disposition = validate_and_record(&connection, &header);
    assert(disposition == expected_unsent_disposition());
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(connection.transmit.acknowledged_through_message_id == 103);
    assert(connection.transmit.sent_messages.messages.count == 0);
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, 0));
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, 1));
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, 2));
    assert(test_statistics.acknowledgement_message_ids_processed == 3);
#else
    assert(connection.transmit.acknowledged_through_message_id == 100);
    assert(connection.transmit.sent_messages.messages.count == 2);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 0));
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 1));
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 2));
    assert(test_statistics.future_or_unsent_acknowledgements == 1);
    assert(connection.receive.recording.statistics.future_or_unsent_ack_count == 1);
    assert(test_statistics.acknowledgement_message_ids_processed == 0);
#endif
    assert(connection.transmit.ready_messages.messages.count == 1);
    tx_destroy(&connection);
    return disposition;
}

static rdp_rx_arrival_disposition_t test_sent_and_redundant_claims(void)
{
    connection_t connection;
    _rdp_header_t header;
    msg_outgoing_t *message;
    uint8_t ack_data[2];
    rdp_rx_arrival_disposition_t disposition;

    initialize_connection(&connection, 100, 103);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 0);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 1);
    queue_message(&connection.transmit.sent_messages, 101, 1);
    queue_message(&connection.transmit.ready_messages, 102, 0);

    store_network_u16(ack_data, 101);
    header = make_ack_header(RDP_FLAG_MASKOFFSET, ack_data);
    disposition = validate_and_record(&connection, &header);
    assert(disposition == RDP_RX_ACCEPT);
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, 0));
    assert(connection.transmit.sent_messages.messages.count == 0);

    disposition = validate_and_record(&connection, &header);
    assert(disposition == RDP_RX_ACCEPT);
    assert(test_statistics.packets_without_new_acknowledgements == 1);

    message = (msg_outgoing_t *)list_remove_head(&connection.transmit.ready_messages.messages);
    assert(message != NULL);
    connection.transmit.ready_messages.queued_bytes -= message->serialized_bytes;
    message->first_sent_time_ms = 1;
    message->last_sent_time_ms = 1;
    message->transmission_count = 2;
    connection.transmit.sent_messages.queued_bytes += message->serialized_bytes;
    list_add_tail(&connection.transmit.sent_messages.messages, &message->link);

    store_network_u16(ack_data, 102);
    disposition = validate_and_record(&connection, &header);
    assert(disposition == RDP_RX_ACCEPT);
    assert(!getbit(connection.transmit.outstanding_message_ids.bytes, 1));
    assert(connection.transmit.sent_messages.messages.count == 0);
    assert(connection.transmit.ready_messages.messages.count == 0);
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
