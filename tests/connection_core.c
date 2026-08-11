// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

#include "connection.h"
#include "container.h"
#include "eventq.h"
#include "rdp.h"
#include "stats.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

static rdp_global_statistics_t test_global_statistics;

static void initialize_connection(connection_t *connection)
{
    memset(connection, 0, sizeof(*connection));
    memset(&test_global_statistics, 0, sizeof(test_global_statistics));
    g_rdp_stat = &test_global_statistics;
    connection->transmit.bandwidth.bytes_per_second = 3000;
    connection->transmit.acknowledged_through_message_id = 0;
    connection->transmit.reliable_next_message_id = 1;
    connection->rdplib_keepalive_interval_ms = RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS;
}

static void test_event_selection(void)
{
    connection_t connection;
    rdp_timeout_data_t timeout;

    initialize_connection(&connection);
    connection.transmit.connected = 1;
    connection.options = 1;
    connection.receive.recording.last_reliable_receive_time_ms = 1;
    connection.transmit.last_reliable_enqueue_time_ms = 100;

    connection_recalc_event_timeout(&connection, &timeout);
    assert(!timeout.infinite);
    assert(timeout.deadline_ms == 10100);
    assert(connection.event_type == RDP_CONNECTION_EVENT_KEEPALIVE);

    connection.rdplib_keepalive_interval_ms = 400;
    connection_recalc_event_timeout(&connection, &timeout);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(timeout.deadline_ms == 10100); // The recovered scheduler retains its literal interval.
#else
    assert(timeout.deadline_ms == 500);
#endif
    connection.rdplib_keepalive_interval_ms = RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS;

    connection.options = 3;
    connection.receive.trace_last_send_time_ms = 10100;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.deadline_ms == 10100);
    assert(connection.event_type == RDP_CONNECTION_EVENT_KEEPALIVE); // Equal deadlines retain the earlier source.

    connection.receive.trace_last_send_time_ms = 10099;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.deadline_ms == 10099);
    assert(connection.event_type == RDP_CONNECTION_EVENT_TRACEROUTE);

    connection.linger_active = 1;
    connection.linger_deadline_ms = 10099;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.deadline_ms == 10099);
    assert(connection.event_type == RDP_CONNECTION_EVENT_TRACEROUTE);

    connection.linger_deadline_ms = 10098;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.deadline_ms == 10098);
    assert(connection.event_type == RDP_CONNECTION_EVENT_LINGER);

    initialize_connection(&connection);
    connection.transmit.connected = 1;
    connection.transmit.transmit_stopped = 1;
    connection.transmit.delayed_ack_pending = 1;
    connection.transmit.delayed_ack_deadline_ms = 500;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.deadline_ms == 500);
    assert(connection.event_type == RDP_CONNECTION_EVENT_TRANSMIT);
}

static void test_trace_retransmission_clamp(void)
{
    connection_t connection;
    rdp_timeout_data_t timeout;

    initialize_connection(&connection);
    connection.options = 2;
    connection.receive.trace_in_flight = 1;
    connection.receive.trace_last_send_time_ms = 100;

    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.deadline_ms == 150);
    assert(connection.event_type == RDP_CONNECTION_EVENT_TRACEROUTE);

    connection.transmit.rtt_estimator.mean_ms = UINT16_MAX;
    connection.transmit.rtt_estimator.deviation_ms = 1;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.deadline_ms == 100u + UINT16_MAX);
}

static void test_icmp_recording_and_abort(void)
{
    connection_t connection;
    uint8_t source_address[16] = {0};
    uint32_t expected_source = UINT32_C(0x44332211);

    memcpy(source_address + 4, &expected_source, sizeof(expected_source));
    initialize_connection(&connection);
    connection_handle_icmp(&connection, 4, 0, 0, 0, source_address);
    assert(connection.receive.icmp_count == 1);
    assert(connection.receive.last_icmp_type == 4);
    assert(connection.receive.last_icmp_code == 0);
    assert(connection.receive.last_icmp_source == expected_source);
    assert(test_global_statistics.icmp_source_quench == 1);
    assert(connection.receive.recording.statistics.icmp_source_quench_count == 1);

    connection_handle_icmp(&connection, 2, 0, 0, 0, source_address);
    assert(connection.receive.icmp_count == 1);

    initialize_connection(&connection);
    connection.transmit.connected = 1;
    connection_handle_icmp(&connection, 3, 3, 0, 0, source_address);
    assert(!connection.transmit.connected);
    assert(connection.transmit.transmit_stopped);
    assert(connection.transmit.disconnect_reason == RDP_DISCONNECT_REASON_ICMP);
    assert(test_global_statistics.icmp_destination_unreachable_by_code[3] == 1);
    assert(test_global_statistics.disconnect_icmp_destination_unreachable_by_code[3] == 1);
}

static void test_recent_icmp_diagnosis_override(void)
{
    connection_t connection;

    initialize_connection(&connection);
    connection.transmit.connected = 1;
    connection.receive.icmp_count = 1;
    connection.receive.last_icmp_type = 3;
    connection.receive.last_icmp_code = 3;
    connection.receive.last_icmp_time_ms = time_get_ms();

    tx_abort_connection(&connection, RDP_DISCONNECT_REASON_PEER_RESET);
    assert(connection.transmit.disconnect_reason == RDP_DISCONNECT_REASON_ICMP);
    assert(test_global_statistics.peer_reset_disconnects == 1);
    assert(test_global_statistics.disconnect_icmp_destination_unreachable_by_code[3] == 0);
}

static void test_stall_uses_oldest_first_send(void)
{
    connection_t connection;
    msg_outgoing_t head_message;
    msg_outgoing_t older_message;
    uint32_t now_ms = time_get_ms();

    initialize_connection(&connection);
    memset(&head_message, 0, sizeof(head_message));
    memset(&older_message, 0, sizeof(older_message));
    list_init(&connection.transmit.sent_messages.messages);
    connection.transmit.rtt_estimator.mean_ms = 50;
    connection.transmit.rtt_estimator.deviation_ms = 0;

    head_message.link.value = &head_message;
    head_message.first_sent_time_ms = now_ms - 100u;
    head_message.last_sent_time_ms = now_ms - 10u;
    list_add_tail(&connection.transmit.sent_messages.messages, &head_message.link);

    older_message.link.value = &older_message;
    older_message.first_sent_time_ms = now_ms - 200u;
    older_message.last_sent_time_ms = now_ms - 20u;
    list_add_tail(&connection.transmit.sent_messages.messages, &older_message.link);

    assert(tx_get_stall_time(&connection) >= 150u);
    assert(connection.transmit.sent_messages.messages.head == &head_message.link);
    assert(connection.transmit.sent_messages.messages.tail == &older_message.link);
    assert(connection.transmit.sent_messages.messages.count == 2);
}

static void test_ack_base_sign_extension(void)
{
    connection_t connection;
    uint16_t output_words[RDP_ACK_MAX_BYTES / sizeof(uint16_t) + 1];
    uint8_t *output = (uint8_t *)output_words;
    uint16_t flags = 0;

    initialize_connection(&connection);
    connection.receive.ack.received_through_message_id = UINT16_C(0x7FF0);
    connection.receive.ack.unreported_message_count = 1;
    connection.receive.ack.unreported_min_message_id = UINT16_C(0x8001);
    connection.receive.ack.unreported_max_message_id = UINT16_C(0x8001);
    bitarray_clear(&connection.receive.ack.received_message_ids);

    // The reference code sign extends the pending minimum but zero extends
    // the cumulative base. This is not a modular distance: the
    // wrapped distance is +17, yet every client selects ACKTHRU here.
    assert(rx_append_ack(&connection, output_words, &flags) == 5);
    assert(flags == (RDP_FLAG_ACKTHRU | (UINT16_C(3) << 4)));
    assert(output[0] == 0x7F && output[1] == 0xF0);
    assert(connection.receive.ack.unreported_message_count == 0);
    assert(connection.receive.ack.unreported_min_message_id == UINT16_C(0x7FF0));
}

static void test_fixed_width_wrap_interpretation(void)
{
    const uint8_t byte_zero = 0;
    const uint8_t byte_half = UINT8_C(0x80);
    const uint16_t word_zero = 0;
    const uint16_t word_half = UINT16_C(0x8000);
    const rdp_timeout_data_t time_zero = {0, 0};
    const rdp_timeout_data_t time_half = {0, UINT32_C(0x80000000)};

    // Signed fixed width subtraction makes the exact half range ambiguous in
    // both directions. Preserve it instead of imposing total ordering there.
    assert(uint8_cmp(&byte_half, &byte_zero) < 0);
    assert(uint8_cmp(&byte_zero, &byte_half) < 0);
    assert(uint16_cmp(&word_half, &word_zero) < 0);
    assert(uint16_cmp(&word_zero, &word_half) < 0);
    assert(ascending_timeout_data_cmp(&time_half, &time_zero) < 0);
    assert(ascending_timeout_data_cmp(&time_zero, &time_half) < 0);
}

static void test_packet_history_distance_64(void)
{
    connection_t connection;
    _rdp_header_t header;

    initialize_connection(&connection);
    memset(&header, 0, sizeof(header));
    connection.receive.recording.packet_sequence.last_received_packet_sequence = 100;
    connection.receive.recording.packet_sequence.received_packet_sequence_history = UINT64_C(0x0123456789ABCDEF);
    header.sequence = 164;

    rx_record_seqnum_arrival(&connection, &header);
    assert(connection.receive.recording.packet_sequence.received_packet_sequence_history == UINT64_C(0x8000000000000000));
}

static void test_unreliable_stream_uses_packet_sequence(void)
{
    connection_t connection;
    msg_arrival_t message;

    initialize_connection(&connection);
    memset(&message, 0, sizeof(message));
    message.stream_id = 3;
    message.stream_sequence = UINT8_MAX;
    message.sequence = 1002;
    connection.receive.recording.next_unreliable_sequence[3] = 1000;

    assert(rx_in_sequence(&connection, &message));
    assert(connection.receive.recording.next_unreliable_sequence[3] == 1003);

    message.stream_sequence = 0;
    message.sequence = 1001;
    assert(!rx_in_sequence(&connection, &message));
    assert(connection.receive.recording.next_unreliable_sequence[3] == 1003);

    connection.receive.recording.next_unreliable_sequence[3] = UINT16_MAX;
    message.sequence = 0;
    assert(rx_in_sequence(&connection, &message));
    assert(connection.receive.recording.next_unreliable_sequence[3] == 1);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_event_selection();
    test_trace_retransmission_clamp();
    test_icmp_recording_and_abort();
    test_recent_icmp_diagnosis_override();
    test_stall_uses_oldest_first_send();
    test_ack_base_sign_extension();
    test_fixed_width_wrap_interpretation();
    test_packet_history_distance_64();
    test_unreliable_stream_uses_packet_sequence();
    return 0;
}
