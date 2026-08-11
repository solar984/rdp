// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "stats.h"
#include "tx.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

static rdp_global_statistics_t test_global_statistics;

static void store_network_u16(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void initialize_connection(connection_t *connection, uint16_t acknowledged_through, uint16_t next_message_id)
{
    memset(connection, 0, sizeof(*connection));
    memset(&test_global_statistics, 0, sizeof(test_global_statistics));
    g_rdp_stat = &test_global_statistics;

    connection->transmit.acknowledged_through_message_id = acknowledged_through;
    connection->transmit.reliable_next_message_id = next_message_id;
    bitarray_clear(&connection->transmit.outstanding_message_ids);
    tx_create(connection);
}

static void queue_sent_message(connection_t *connection, uint16_t message_id)
{
    msg_outgoing_t *message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(*message) + 2u);

    assert(message != NULL);
    memset(message, 0, sizeof(*message));
    message->flags = RDP_FLAG_MSGID;
    message->message_id = message_id;
    msg_outgoing_init(message);
    message->first_sent_time_ms = 1;
    message->last_sent_time_ms = 1;
    message->transmission_count = 2;
    connection->transmit.sent_messages.queued_bytes += message->serialized_bytes;
    list_add_tail(&connection->transmit.sent_messages.messages, &message->link);
}

static _rdp_header_t make_ack_header(uint16_t flags, const uint8_t *ack_data)
{
    _rdp_header_t header;

    memset(&header, 0, sizeof(header));
    header.flags = flags;
    header.ack_data = ack_data;
    return header;
}

static void test_ack_validation(void)
{
    connection_t connection;
    _rdp_header_t header;
    uint8_t ack_data[3] = {0};
    uint32_t field_bytes;

    initialize_connection(&connection, 99, 1000);

    header = make_ack_header(0, ack_data);
    assert(tx_validate_ack_arrival(&connection, &header, &field_bytes) == RDP_RX_ACCEPT);
    assert(field_bytes == 0);

    header.flags = RDP_FLAG_ACKTHRU | RDP_FLAG_MASKOFFSET;
    assert(tx_validate_ack_arrival(&connection, &header, &field_bytes) == RDP_RX_ABORT);
    assert(test_global_statistics.conflicting_ack_base_flags == 1);
    assert(connection.receive.recording.statistics.conflicting_ack_flags_count == 1);

    header.flags = 1u << 4;
    assert(tx_validate_ack_arrival(&connection, &header, &field_bytes) == RDP_RX_ABORT);
    assert(test_global_statistics.ack_masks_without_base == 1);

    store_network_u16(ack_data, 500);
    ack_data[2] = 0;
    header.flags = RDP_FLAG_ACKTHRU | (1u << 4);
    assert(tx_validate_ack_arrival(&connection, &header, &field_bytes) == RDP_RX_ABORT);
    assert(field_bytes == 3);
    assert(test_global_statistics.empty_ack_mask_tails == 1);

    store_network_u16(ack_data, 1000);
    header.flags = RDP_FLAG_ACKTHRU;
    assert(tx_validate_ack_arrival(&connection, &header, &field_bytes) == RDP_RX_DISCARD);
    assert(field_bytes == 2);
    assert(test_global_statistics.future_or_unsent_acknowledgements == 1);

    connection.transmit.reliable_next_message_id = 5000;
    store_network_u16(ack_data, 904);
    assert(tx_validate_ack_arrival(&connection, &header, &field_bytes) == RDP_RX_ACCEPT);
    store_network_u16(ack_data, 903);
    assert(tx_validate_ack_arrival(&connection, &header, &field_bytes) == RDP_RX_DISCARD);
    assert(test_global_statistics.ancient_acknowledgements == 1);
    tx_destroy(&connection);
}

static void test_ack_recording(void)
{
    connection_t connection;
    _rdp_header_t header;
    uint8_t ack_data[3];

    initialize_connection(&connection, 100, 110);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 0);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 1);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 4);
    queue_sent_message(&connection, 101);
    queue_sent_message(&connection, 102);
    queue_sent_message(&connection, 105);

    store_network_u16(ack_data, 102);
    ack_data[2] = 0x20; // Mask bit 2 acknowledges message ID 105.
    header = make_ack_header(RDP_FLAG_ACKTHRU | (1u << 4), ack_data);

    assert(tx_validate_ack_arrival(&connection, &header, &(uint32_t){0}) == RDP_RX_ACCEPT);
    tx_record_ack_arrival(&connection, &header);

    assert(connection.transmit.acknowledged_through_message_id == 102);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 0) == 0);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 1) == 0);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 2) == 0);
    assert(test_global_statistics.acknowledgement_message_ids_processed == 3);
    assert(connection.receive.recording.statistics.ack_message_ids_processed == 3);
    assert(test_global_statistics.ack_only_packets_received == 1);
    assert(test_global_statistics.packets_without_new_acknowledgements == 0);

    tx_record_ack_arrival(&connection, &header);
    assert(test_global_statistics.acknowledgement_message_ids_processed == 3);
    assert(test_global_statistics.ack_only_packets_received == 2);
    assert(test_global_statistics.packets_without_new_acknowledgements == 1);
    assert(test_global_statistics.nominal_ack_header_bytes_without_new_acknowledgements == 3);
    tx_destroy(&connection);
}

static void test_ack_rollover(void)
{
    connection_t connection;
    _rdp_header_t header;
    uint8_t ack_data[2];
    uint32_t field_bytes;

    initialize_connection(&connection, UINT16_C(0xFFFE), 5);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 0);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 1);
    queue_sent_message(&connection, UINT16_C(0xFFFF));
    queue_sent_message(&connection, 0);
    store_network_u16(ack_data, 0);
    header = make_ack_header(RDP_FLAG_ACKTHRU, ack_data);

    assert(tx_validate_ack_arrival(&connection, &header, &field_bytes) == RDP_RX_ACCEPT);
    tx_record_ack_arrival(&connection, &header);

    assert(connection.transmit.acknowledged_through_message_id == 0);
    assert(test_global_statistics.acknowledgement_message_ids_processed == 2);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 0) == 0);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 1) == 0);
    tx_destroy(&connection);
}

static void test_maskoffset_and_no_ack_accounting(void)
{
    connection_t connection;
    _rdp_header_t header;
    uint8_t ack_data[2];

    initialize_connection(&connection, 100, 110);
    bitarray_setbit(&connection.transmit.outstanding_message_ids, 4);
    queue_sent_message(&connection, 105);
    store_network_u16(ack_data, 105);
    header = make_ack_header(RDP_FLAG_MASKOFFSET, ack_data);

    tx_record_ack_arrival(&connection, &header);
    assert(connection.transmit.acknowledged_through_message_id == 100);
    assert(getbit(connection.transmit.outstanding_message_ids.bytes, 4) == 0);
    assert(test_global_statistics.acknowledgement_message_ids_processed == 1);
    assert(test_global_statistics.packets_without_new_acknowledgements == 0);

    // The clients count even a data only packet as a redundant 2 byte ACK
    // header when it retires no reliable IDs.
    header = make_ack_header(0, NULL);
    header.payload_bytes = 10;
    tx_record_ack_arrival(&connection, &header);
    assert(test_global_statistics.packets_without_new_acknowledgements == 1);
    assert(test_global_statistics.nominal_ack_header_bytes_without_new_acknowledgements == 2);
    assert(connection.receive.recording.statistics.redundant_ack_packets_received == 1);
    assert(connection.receive.recording.statistics.redundant_ack_header_bytes_received == 2);
    tx_destroy(&connection);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    fast_malloc_init(1024u * 1024u);
    test_ack_validation();
    test_ack_recording();
    test_ack_rollover();
    test_maskoffset_and_no_ack_accounting();
    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return 0;
}
