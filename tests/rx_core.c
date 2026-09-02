// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fast.h"
#include "rdpstat.h"
#include "rx.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

_Static_assert(sizeof(connection_stat) == 0x10C, "connection_stat no longer matches the recovered counter layout");
_Static_assert(offsetof(connection_stat, tqd_last_interval) == 0x0F4, "connection_stat::tqd_last_interval moved");
_Static_assert(offsetof(connection_stat, tqd_bytes) == 0x108, "connection_stat::tqd_bytes moved");
_Static_assert(_Generic(&rx_init, void (*)(connection_t *): 1, default: 0), "rx_init signature");
_Static_assert(_Generic(&rx_create, uint32_t (*)(connection_t *): 1, default: 0), "rx_create signature");
_Static_assert(_Generic(&rx_destroy, void (*)(connection_t *): 1, default: 0), "rx_destroy signature");
_Static_assert(_Generic(&rx_flush_input_buffers, void (*)(connection_t *): 1, default: 0), "rx_flush_input_buffers signature");
_Static_assert(_Generic(&rx_record_packet_arrival, void (*)(connection_t *): 1, default: 0), "rx_record_packet_arrival signature");
_Static_assert(_Generic(&rx_record_seqnum_arrival, void (*)(connection_t *, rdp_header_t *): 1, default: 0), "rx_record_seqnum_arrival signature");
_Static_assert(_Generic(&rx_validate_seqnum_arrival, uint32_t (*)(connection_t *, uint16_t): 1, default: 0), "rx_validate_seqnum_arrival signature");
_Static_assert(_Generic(&rx_validate_msgid_arrival, uint32_t (*)(connection_t *, rdp_header_t *): 1, default: 0), "rx_validate_msgid_arrival signature");
_Static_assert(_Generic(&rx_validate_fragment_arrival, uint32_t (*)(connection_t *, rdp_header_t *): 1, default: 0), "rx_validate_fragment_arrival signature");
_Static_assert(_Generic(&rx_validate_stream_arrival, uint32_t (*)(connection_t *, rdp_header_t *): 1, default: 0), "rx_validate_stream_arrival signature");
_Static_assert(_Generic(&rx_record_msgid_arrival, uint32_t (*)(connection_t *, uint16_t): 1, default: 0), "rx_record_msgid_arrival signature");
_Static_assert(_Generic(&rx_append_ack, uint32_t (*)(connection_t *, uint16_t *, uint16_t *): 1, default: 0), "rx_append_ack signature");
_Static_assert(_Generic(&rx_sort_into_sequence, void (*)(connection_t *, msg_arrival_t *): 1, default: 0), "rx_sort_into_sequence signature");
_Static_assert(_Generic(&rx_get_next_in_sequence, msg_arrival_t *(*)(connection_t *, uint8_t): 1, default: 0), "rx_get_next_in_sequence signature");
_Static_assert(_Generic(&rx_in_sequence, uint32_t (*)(connection_t *, msg_arrival_t *): 1, default: 0), "rx_in_sequence signature");
_Static_assert(_Generic(&rx_assemble, msg_arrival_t *(*)(connection_t *, rdp_header_t *, char *): 1, default: 0), "rx_assemble signature");
_Static_assert(_Generic(&rx_load_fin_arrival, msg_arrival_t *(*)(connection_t *): 1, default: 0), "rx_load_fin_arrival signature");
_Static_assert(_Generic(&rx_save_fin_arrival, void (*)(connection_t *, msg_arrival_t *): 1, default: 0), "rx_save_fin_arrival signature");
_Static_assert(_Generic(&rx_rcvd_all_msgids, uint32_t (*)(connection_t *): 1, default: 0), "rx_rcvd_all_msgids signature");
_Static_assert(_Generic(&rx_fin_waiting, uint32_t (*)(connection_t *): 1, default: 0), "rx_fin_waiting signature");
#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&c_stat_format, uint32_t (*)(connection_stat *, char *, uint32_t): 1, default: 0), "c_stat_format signature");
#endif

static rdp_stat test_global_statistics;

static void initialize_receive(connection_t *c)
{
    memset(c, 0, sizeof(*c));
    memset(&test_global_statistics, 0, sizeof(test_global_statistics));
    g_rdp_stat = &test_global_statistics;
    rx_init(c);
}

static msg_arrival_t *allocate_arrival(uint16_t fragid)
{
    msg_arrival_t *arrival;

    arrival = (msg_arrival_t *)fast_malloc((uint32_t)sizeof(*arrival));
    assert(arrival != NULL);
    memset(arrival, 0, sizeof(*arrival));
    msg_arrival_init(arrival, fragid);
    return arrival;
}

static rdp_header_t make_header(uint16_t flags, uint16_t sequence, uint16_t message_id, uint16_t fragment_id, uint16_t fragment_index, uint16_t fragment_count,
                                 uint8_t stream_id, uint8_t stream_sequence, uint16_t payload_bytes)
{
    rdp_header_t header;

    memset(&header, 0, sizeof(header));
    header.options = flags;
    header.seqnum = sequence;
    header.msgid = message_id;
    header.fragid = fragment_id;
    header.frag_number = fragment_index;
    header.frag_total = fragment_count;
    header.stream = stream_id;
    header.stream_seqnum = stream_sequence;
    header.header_size = 12;
    header.data_size = payload_bytes;
    return header;
}

static void fill_pattern(uint8_t *bytes, size_t size, uint8_t seed)
{
    size_t index;

    for (index = 0; index < size; ++index)
    {
        bytes[index] = (uint8_t)(seed + (uint8_t)(index * 11u));
    }
}

static void test_selective_init_create_and_destroy(void)
{
    connection_t c;
    connection_stat cleared_statistics;
    uint8_t cleared_history[sizeof(c.rx_others_received)];
    uint32_t stream;

    memset(&c, 0, sizeof(c));
    memset(&test_global_statistics, 0, sizeof(test_global_statistics));
    memset(&cleared_statistics, 0, sizeof(cleared_statistics));
    memset(cleared_history, 0, sizeof(cleared_history));
    g_rdp_stat = &test_global_statistics;

    c.rx_syn_msgid = UINT16_C(0x1234);
    c.rx_time_last_arrival = UINT32_MAX;
    c.rx_time_last_msgid_arrival = UINT32_MAX;
    c.rx_msgid_lo = UINT16_C(0x2345);
    c.rx_msgid_hi = UINT16_C(0x3456);
    c.rx_fin_msgid = UINT16_C(0x4567);
    c.rx_icmp_type = UINT8_C(0x51);
    c.rx_icmp_code = UINT8_C(0x52);
    c.rx_icmp_time = UINT32_C(0x53545556);
    c.rx_icmp_from.s_addr = UINT32_C(0x5758595A);
    c.rx_connect_clock = UINT32_C(0x61626364);
    c.trace_start = UINT32_C(0x65666768);
    c.trace_clock = UINT32_C(0x71727374);

    rx_init(&c);

    assert(memcmp(&c.stat, &cleared_statistics, sizeof(cleared_statistics)) == 0);
    assert(c.rx_received_all_thru == 0);
    assert(c.rx_highest_received == 0);
    assert(memcmp(&c.rx_others_received, cleared_history, sizeof(cleared_history)) == 0);
    assert(c.rx_msgid_count == 0);
    assert(c.rx_best_effort_stream_seqnum_reset == 0);
    assert(c.rx_syn_recvd == 0);
    assert(c.rx_highest_seqnum_received == UINT16_MAX);
    assert(c.rx_recent_seqnum_history == UINT64_MAX);
    assert(c.rx_fin_recvd == 0);
    assert(c.rx_fin_storage == NULL);
    assert(c.trace_probes == NULL);
    assert(c.rx_connect_trace == NULL);
    assert(c.rx_connect_count == 0);
    assert(c.trace_en_route == 0);
    assert(c.trace_next_ttl == 1);
    assert(c.trace_max_ttl == 30);
    assert(c.trace_pass == 0);
    assert(c.trace_next_index == 0);
    assert(c.rx_icmp_received == 0);
    assert(c.rx_time_last_arrival != UINT32_MAX);
    assert(c.rx_time_last_msgid_arrival != UINT32_MAX);

    assert(c.rx_syn_msgid == UINT16_C(0x1234));
    assert(c.rx_msgid_lo == UINT16_C(0x2345));
    assert(c.rx_msgid_hi == UINT16_C(0x3456));
    assert(c.rx_fin_msgid == UINT16_C(0x4567));
    assert(c.rx_icmp_type == UINT8_C(0x51));
    assert(c.rx_icmp_code == UINT8_C(0x52));
    assert(c.rx_icmp_time == UINT32_C(0x53545556));
    assert(c.rx_icmp_from.s_addr == UINT32_C(0x5758595A));
    assert(c.rx_connect_clock == UINT32_C(0x61626364));
    assert(c.trace_start == UINT32_C(0x65666768));
    assert(c.trace_clock == UINT32_C(0x71727374));

    assert(c.rx_reassembly_pool.list.head == NULL);
    assert(c.rx_reassembly_pool.list.tail == NULL);
    assert(c.rx_reassembly_pool.list.size == 0);
    assert(c.rx_reassembly_pool.list.keycmp == NULL);
    assert(c.rx_reassembly_pool.list.sorted == 0);
    for (stream = 0; stream < RDP_STREAM_COUNT; ++stream)
    {
        assert(c.rx_best_effort_stream_seqnum[stream] == 0);
        assert(c.rx_guaranteed_stream_seqnum[stream] == 0);
        assert(c.rx_sequencer[stream].list.head == NULL);
        assert(c.rx_sequencer[stream].list.tail == NULL);
        assert(c.rx_sequencer[stream].list.size == 0);
        assert(c.rx_sequencer[stream].list.keycmp == NULL);
        assert(c.rx_sequencer[stream].list.sorted == 0);
    }

    assert(rx_create(&c) == 0);
    assert(c.rx_reassembly_pool.list.keycmp == uint16_cmp);
    assert(c.rx_reassembly_pool.list.sorted == 1);
    for (stream = 0; stream < RDP_STREAM_COUNT; ++stream)
    {
        assert(c.rx_sequencer[stream].list.keycmp == uint8_cmp);
        assert(c.rx_sequencer[stream].list.sorted == 1);
    }

    c.trace_probes = malloc(32);
    assert(c.trace_probes != NULL);
    rx_destroy(&c);
    assert(c.trace_probes == NULL);
}

static uint64_t expected_reordered_mask(uint32_t distance)
{
    uint32_t history_bit = distance - 1u;

#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    uint32_t source_bit = UINT32_C(1) << (history_bit & 31u);

    return (uint64_t)(int64_t)(int32_t)source_bit;
#else
    return UINT64_C(1) << history_bit;
#endif
}

typedef struct reliable_receive_result_t
{
    uint32_t packet_disposition;
    uint32_t duplicate_reliable;
    uint32_t application_deliveries;
    uint16_t delivered_message_ids[2];
    uint8_t delivered_stream_sequences[2];
    uint8_t delivered_payloads[2];
} reliable_receive_result_t;

static void record_reordered_packet_sequence(connection_t *c, uint16_t sequence)
{
    rdp_header_t header;

    header = make_header(0, sequence, 0, 0, 0, 1, 0, 0, 0);
    assert(rx_validate_seqnum_arrival(c, sequence) == RDP_RX_ACCEPT);
    rx_record_packet_arrival(c);
    rx_record_seqnum_arrival(c, &header);
}

static reliable_receive_result_t receive_reliable_stream_packet(connection_t *c, rdp_header_t *header, uint8_t payload)
{
    reliable_receive_result_t result;
    msg_arrival_t *arrival;
    msg_arrival_t *delivered;

    memset(&result, 0, sizeof(result));
    result.packet_disposition = rx_validate_seqnum_arrival(c, header->seqnum);
    result.duplicate_reliable = UINT32_MAX;
    if (result.packet_disposition != RDP_RX_ACCEPT)
    {
        return result;
    }

    // Mirror the receive relevant pipeline order after packet parsing: validate the reliable and stream fields, record the accepted packet and message ID, suppress a duplicate message ID, then assemble and release the ordered stream.
    assert(rx_validate_msgid_arrival(c, header) == RDP_RX_ACCEPT);
    assert(rx_validate_stream_arrival(c, header) == RDP_RX_ACCEPT);
    rx_record_packet_arrival(c);
    rx_record_seqnum_arrival(c, header);
    result.duplicate_reliable = rx_record_msgid_arrival(c, header->msgid);
    if (result.duplicate_reliable)
    {
        return result;
    }

    arrival = rx_assemble(c, header, (char *)&payload);
    assert(arrival != NULL);
    rx_sort_into_sequence(c, arrival);
    while ((delivered = rx_get_next_in_sequence(c, header->stream)) != NULL)
    {
        assert(delivered->stream == header->stream);
        assert(delivered->size == 1);
        assert(result.application_deliveries < 2);
        result.delivered_message_ids[result.application_deliveries] = delivered->msgid;
        result.delivered_stream_sequences[result.application_deliveries] = delivered->stream_seqnum;
        result.delivered_payloads[result.application_deliveries] = (uint8_t)msg_arrival_get_data(delivered)[0];
        ++result.application_deliveries;
        fast_free(delivered);
    }
    return result;
}

static void test_distance_32_sign_extension_false_duplicate_recovery(void)
{
    connection_t c;
    rdp_header_t original;
    rdp_header_t retransmission;
    rdp_header_t following;
    reliable_receive_result_t result;

    initialize_receive(&c);
    assert(rx_create(&c) == 0);
    c.rx_highest_seqnum_received = 1000;
    c.rx_recent_seqnum_history = 0;
    c.rx_syn_recvd = 1;
    c.rx_syn_msgid = 99;
    c.rx_highest_received = 99;
    c.rx_received_all_thru = 99;

    record_reordered_packet_sequence(&c, 968);
    assert(c.rx_recent_seqnum_history == expected_reordered_mask(32));

    original = make_header(RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, 967, 100, 0, 0, 1, 6, 0, 1);
    result = receive_reliable_stream_packet(&c, &original, UINT8_C(0xA1));
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(result.packet_disposition == RDP_RX_DISCARD);
    assert(result.duplicate_reliable == UINT32_MAX);
    assert(result.application_deliveries == 0);
    assert(c.rx_guaranteed_stream_seqnum[6] == 0);
#else
    assert(result.packet_disposition == RDP_RX_ACCEPT);
    assert(result.duplicate_reliable == 0);
    assert(result.application_deliveries == 1);
    assert(result.delivered_message_ids[0] == 100);
    assert(result.delivered_stream_sequences[0] == 0);
    assert(result.delivered_payloads[0] == UINT8_C(0xA1));
    assert(c.rx_guaranteed_stream_seqnum[6] == 1);
#endif

    following = make_header(RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, 1001, 101, 0, 0, 1, 6, 1, 1);
    result = receive_reliable_stream_packet(&c, &following, UINT8_C(0xA2));
    assert(result.packet_disposition == RDP_RX_ACCEPT);
    assert(result.duplicate_reliable == 0);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(result.application_deliveries == 0);
    assert(c.rx_guaranteed_stream_seqnum[6] == 0);
    assert(c.rx_sequencer[6].list.size == 1);
#else
    assert(result.application_deliveries == 1);
    assert(result.delivered_message_ids[0] == 101);
    assert(result.delivered_stream_sequences[0] == 1);
    assert(result.delivered_payloads[0] == UINT8_C(0xA2));
    assert(c.rx_guaranteed_stream_seqnum[6] == 2);
    assert(c.rx_sequencer[6].list.size == 0);
#endif

    retransmission = original;
    retransmission.seqnum = 1002;
    result = receive_reliable_stream_packet(&c, &retransmission, UINT8_C(0xA1));
    assert(result.packet_disposition == RDP_RX_ACCEPT);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(result.duplicate_reliable == 0);
    assert(result.application_deliveries == 2);
    assert(result.delivered_message_ids[0] == 100);
    assert(result.delivered_stream_sequences[0] == 0);
    assert(result.delivered_payloads[0] == UINT8_C(0xA1));
    assert(result.delivered_message_ids[1] == 101);
    assert(result.delivered_stream_sequences[1] == 1);
    assert(result.delivered_payloads[1] == UINT8_C(0xA2));
#else
    assert(result.duplicate_reliable != 0);
    assert(result.application_deliveries == 0);
#endif
    assert(c.rx_guaranteed_stream_seqnum[6] == 2);
    assert(c.rx_sequencer[6].list.size == 0);
    rx_destroy(&c);
}

static void test_distance_33_masked_shift_relies_on_msgid_dedup(void)
{
    connection_t c;
    rdp_header_t original;
    rdp_header_t following;
    reliable_receive_result_t result;

    initialize_receive(&c);
    assert(rx_create(&c) == 0);
    c.rx_highest_seqnum_received = 1000;
    c.rx_recent_seqnum_history = 0;

    original = make_header(RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, 967, 200, 0, 0, 1, 7, 0, 1);
    result = receive_reliable_stream_packet(&c, &original, UINT8_C(0xB1));
    assert(result.packet_disposition == RDP_RX_ACCEPT);
    assert(result.duplicate_reliable == 0);
    assert(result.application_deliveries == 1);
    assert(result.delivered_message_ids[0] == 200);
    assert(result.delivered_stream_sequences[0] == 0);
    assert(result.delivered_payloads[0] == UINT8_C(0xB1));
    assert(c.rx_recent_seqnum_history == expected_reordered_mask(33));
    assert(c.rx_guaranteed_stream_seqnum[7] == 1);

    result = receive_reliable_stream_packet(&c, &original, UINT8_C(0xB1));
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(result.packet_disposition == RDP_RX_ACCEPT);
    assert(result.duplicate_reliable != 0);
#else
    assert(result.packet_disposition == RDP_RX_DISCARD);
    assert(result.duplicate_reliable == UINT32_MAX);
#endif
    assert(result.application_deliveries == 0);
    assert(c.rx_guaranteed_stream_seqnum[7] == 1);
    assert(c.rx_sequencer[7].list.size == 0);

    following = make_header(RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, 1001, 201, 0, 0, 1, 7, 1, 1);
    result = receive_reliable_stream_packet(&c, &following, UINT8_C(0xB2));
    assert(result.packet_disposition == RDP_RX_ACCEPT);
    assert(result.duplicate_reliable == 0);
    assert(result.application_deliveries == 1);
    assert(result.delivered_message_ids[0] == 201);
    assert(result.delivered_stream_sequences[0] == 1);
    assert(result.delivered_payloads[0] == UINT8_C(0xB2));
    assert(c.rx_guaranteed_stream_seqnum[7] == 2);
    assert(c.rx_sequencer[7].list.size == 0);
    rx_destroy(&c);
}

static void test_packet_sequence_validation_and_recording(void)
{
    static const uint32_t reordered_distances[] = {31, 32, 33, 64};
    connection_t c;
    rdp_header_t header;
    uint32_t index;
    uint32_t stream;

    initialize_receive(&c);
    c.rx_highest_seqnum_received = 1000;
    c.rx_recent_seqnum_history = UINT64_C(1);
    assert(rx_validate_seqnum_arrival(&c, 1000) == RDP_RX_DISCARD);
    assert(rx_validate_seqnum_arrival(&c, 999) == RDP_RX_DISCARD);
    c.rx_recent_seqnum_history = 0;
    assert(rx_validate_seqnum_arrival(&c, 999) == RDP_RX_ACCEPT);
    assert(rx_validate_seqnum_arrival(&c, 935) == RDP_RX_DISCARD);
    assert(rx_validate_seqnum_arrival(&c, 5096) == RDP_RX_ACCEPT);
    assert(rx_validate_seqnum_arrival(&c, 5097) == RDP_RX_DISCARD);
    assert(c.stat.discarded_dup_seqnum == 2);
    assert(c.stat.discarded_old_seqnum == 2);

    c.rx_highest_seqnum_received = UINT16_C(0xFFFE);
    c.rx_recent_seqnum_history = 0;
    assert(rx_validate_seqnum_arrival(&c, 1) == RDP_RX_ACCEPT);

    memset(&header, 0, sizeof(header));
    header.header_size = 12;
    header.data_size = 7;
    for (index = 0; index < sizeof(reordered_distances) / sizeof(reordered_distances[0]); ++index)
    {
        uint32_t distance = reordered_distances[index];

        c.rx_highest_seqnum_received = 1000;
        c.rx_recent_seqnum_history = 0;
        header.seqnum = (uint16_t)(1000u - distance);
        rx_record_seqnum_arrival(&c, &header);
        assert(c.rx_highest_seqnum_received == 1000);
        assert(c.rx_recent_seqnum_history == expected_reordered_mask(distance));
    }
    assert(c.stat.packets_rx_out_of_sequence == 4);
    assert(c.stat.bytes_rx_out_of_sequence == 4u * 19u);

    c.rx_highest_seqnum_received = 100;
    c.rx_recent_seqnum_history = UINT64_C(0x0123456789ABCDEF);
    header.seqnum = 101;
    rx_record_seqnum_arrival(&c, &header);
    assert(c.rx_recent_seqnum_history == UINT64_C(0x02468ACF13579BDF));

    c.rx_highest_seqnum_received = 100;
    c.rx_recent_seqnum_history = UINT64_C(0x0123456789ABCDEF);
    header.seqnum = 164;
    rx_record_seqnum_arrival(&c, &header);
    assert(c.rx_recent_seqnum_history == UINT64_C(0x8000000000000000));

    c.rx_highest_seqnum_received = 100;
    c.rx_recent_seqnum_history = UINT64_MAX;
    header.seqnum = 165;
    rx_record_seqnum_arrival(&c, &header);
    assert(c.rx_recent_seqnum_history == 0);

    c.rx_highest_seqnum_received = 20000;
    c.rx_best_effort_stream_seqnum_reset = 4000;
    header.seqnum = 20001;
    rx_record_seqnum_arrival(&c, &header);
    assert(c.rx_best_effort_stream_seqnum_reset == 20000);
    for (stream = 0; stream < RDP_STREAM_COUNT; ++stream)
    {
        assert(c.rx_best_effort_stream_seqnum[stream] == 20000);
    }
}

static void test_message_ids_and_rejected_syn_flag(void)
{
    connection_t c;
    rdp_header_t header;

    initialize_receive(&c);
    assert(rx_record_msgid_arrival(&c, UINT16_C(0xFFFE)) == 0);
    assert(c.rx_syn_recvd != 0);
    assert(c.rx_syn_msgid == UINT16_C(0xFFFE));
    assert(c.rx_received_all_thru == UINT16_C(0xFFFE));
    assert(c.rx_highest_received == UINT16_C(0xFFFE));

    assert(rx_record_msgid_arrival(&c, 0) == 0);
    assert(c.rx_received_all_thru == UINT16_C(0xFFFE));
    assert(rx_record_msgid_arrival(&c, UINT16_MAX) == 0);
    assert(c.rx_received_all_thru == 0);
    assert(rx_record_msgid_arrival(&c, UINT16_MAX) != 0);
    assert(c.rx_msgid_count == 4);
    assert(c.rx_msgid_lo == 0);
    assert(c.rx_msgid_hi == UINT16_MAX);

    initialize_receive(&c);
    c.rx_syn_recvd = 1;
    c.rx_syn_msgid = 100;
    c.rx_received_all_thru = 100;
    header = make_header(RDP_FLAG_SYN | RDP_FLAG_MSGID, 0, 101, 0, 0, 1, 0, 0, 0);
    assert(rx_validate_msgid_arrival(&c, &header) == RDP_RX_DISCARD);
    assert(test_global_statistics.discarded_old_msgid == 1);
    assert(c.stat.discarded_old_msgid == 1);
    header.options = RDP_FLAG_MSGID;
    assert(rx_validate_msgid_arrival(&c, &header) == RDP_RX_ACCEPT);
    header.options = RDP_FLAG_SYN | RDP_FLAG_MSGID;
    header.msgid = 100;
    assert(rx_validate_msgid_arrival(&c, &header) == RDP_RX_ACCEPT);
}

static void test_ack_serialization(void)
{
    connection_t c;
    uint16_t output_words[(RDP_ACK_MAX_BYTES + 1u) / 2u];
    uint8_t *output = (uint8_t *)output_words;
    uint16_t options;

    initialize_receive(&c);
    assert(rx_record_msgid_arrival(&c, 100) == 0);
    assert(rx_record_msgid_arrival(&c, 102) == 0);
    assert(rx_record_msgid_arrival(&c, 101) == 0);
    memset(output_words, 0xA5, sizeof(output_words));
    options = RDP_FLAG_MSGID;
    assert(rx_append_ack(&c, output_words, &options) == 2);
    assert(options == (RDP_FLAG_MSGID | RDP_FLAG_ACKTHRU));
    assert(output[0] == 0 && output[1] == 102);
    assert(c.rx_msgid_count == 0);
    assert(c.rx_msgid_lo == 102);
    assert(c.rx_msgid_hi == 102);

    bitarray_clear(&c.rx_others_received);
    bitarray_setbit(&c.rx_others_received, 10);
    bitarray_setbit(&c.rx_others_received, 17);
    c.rx_received_all_thru = 100;
    c.rx_msgid_count = 2;
    c.rx_msgid_lo = 110;
    c.rx_msgid_hi = 119;
    memset(output_words, 0xA5, sizeof(output_words));
    options = 0;
    assert(rx_append_ack(&c, output_words, &options) == 4);
    assert(options == (RDP_FLAG_MASKOFFSET | (UINT16_C(2) << 4)));
    assert(output[0] == 0 && output[1] == 110);
    assert(output[2] == UINT8_C(0x81) && output[3] == 0);

    bitarray_clear(&c.rx_others_received);
    c.rx_msgid_count = 1;
    c.rx_msgid_lo = 200;
    c.rx_msgid_hi = 400;
    memset(output_words, 0, sizeof(output_words));
    options = 0;
    assert(rx_append_ack(&c, output_words, &options) == RDP_ACK_MAX_BYTES);
    assert(options == (RDP_FLAG_MASKOFFSET | (UINT16_C(15) << 4)));

    c.rx_msgid_count = 0;
    assert(rx_append_ack(&c, NULL, NULL) == 0);
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    c.rx_msgid_count = 1;
    assert(rx_append_ack(&c, NULL, &options) == 0);
    assert(c.rx_msgid_count == 1);
#endif
}

static void initialize_sequence_arrival(msg_arrival_t *arrival, uint8_t stream, uint8_t stream_sequence)
{
    memset(arrival, 0, sizeof(*arrival));
    msg_arrival_init(arrival, 0);
    arrival->options = RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED;
    arrival->stream = stream;
    arrival->stream_seqnum = stream_sequence;
}

static void test_streams_and_sequencing(void)
{
    connection_t c;
    rdp_header_t header;
    msg_arrival_t arrivals[4];
    msg_arrival_t unreliable;

    initialize_receive(&c);
    assert(rx_create(&c) == 0);

    memset(&header, 0, sizeof(header));
    header.stream = RDP_STREAM_COUNT - 1u;
    assert(rx_validate_stream_arrival(&c, &header) == RDP_RX_ACCEPT);
    header.stream = RDP_STREAM_COUNT;
    assert(rx_validate_stream_arrival(&c, &header) == RDP_RX_ABORT);
    assert(test_global_statistics.discarded_bad_stream == 1);
    assert(c.stat.discarded_bad_stream == 1);

    initialize_sequence_arrival(&arrivals[0], 3, 1);
    initialize_sequence_arrival(&arrivals[1], 3, 0);
    rx_sort_into_sequence(&c, &arrivals[0]);
    rx_sort_into_sequence(&c, &arrivals[1]);
    assert(rx_get_next_in_sequence(&c, 3) == &arrivals[1]);
    assert(rx_get_next_in_sequence(&c, 3) == &arrivals[0]);
    assert(rx_get_next_in_sequence(&c, 3) == NULL);

    c.rx_guaranteed_stream_seqnum[4] = UINT8_MAX;
    initialize_sequence_arrival(&arrivals[2], 4, 0);
    initialize_sequence_arrival(&arrivals[3], 4, UINT8_MAX);
    rx_sort_into_sequence(&c, &arrivals[2]);
    rx_sort_into_sequence(&c, &arrivals[3]);
    assert(rx_get_next_in_sequence(&c, 4) == &arrivals[3]);
    assert(rx_get_next_in_sequence(&c, 4) == &arrivals[2]);
    assert(c.rx_guaranteed_stream_seqnum[4] == 1);

    memset(&unreliable, 0, sizeof(unreliable));
    unreliable.stream = 5;
    c.rx_best_effort_stream_seqnum[5] = 1000;
    unreliable.seqnum = 1002;
    assert(rx_in_sequence(&c, &unreliable) != 0);
    assert(c.rx_best_effort_stream_seqnum[5] == 1003);
    unreliable.seqnum = 1001;
    assert(rx_in_sequence(&c, &unreliable) == 0);
    assert(c.rx_best_effort_stream_seqnum[5] == 1003);
    c.rx_best_effort_stream_seqnum[5] = UINT16_MAX;
    unreliable.seqnum = 0;
    assert(rx_in_sequence(&c, &unreliable) != 0);
    assert(c.rx_best_effort_stream_seqnum[5] == 1);

    rx_destroy(&c);
}

static void test_fragment_validation_and_assembly(void)
{
    connection_t c;
    rdp_header_t header;
    msg_arrival_t *arrival;
    uint8_t first[RDP_FRAGMENT_PAYLOAD_BYTES];
    uint8_t last[7];
    uint8_t whole[13];

    initialize_receive(&c);
    assert(rx_create(&c) == 0);
    fill_pattern(first, sizeof(first), UINT8_C(0x31));
    fill_pattern(last, sizeof(last), UINT8_C(0xA7));
    fill_pattern(whole, sizeof(whole), UINT8_C(0x5D));

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    header = make_header(RDP_FLAG_FRAGMENT | RDP_FLAG_MSGID, 1, 1, 1, 0, 1, 0, 0, 1);
    assert(rx_validate_fragment_arrival(&c, &header) == RDP_RX_ABORT);
    header.frag_total = 2;
    header.data_size = RDP_FRAGMENT_PAYLOAD_BYTES - 1u;
    assert(rx_validate_fragment_arrival(&c, &header) == RDP_RX_ABORT);
    header.frag_number = 1;
    header.data_size = 0;
    assert(rx_validate_fragment_arrival(&c, &header) == RDP_RX_ACCEPT);
    assert(c.stat.discarded_bad_fragment == 2);
#endif

    header = make_header(RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, 77, 300, 0, 0, 1, 6, 9, sizeof(whole));
    arrival = rx_assemble(&c, &header, (char *)whole);
    assert(arrival != NULL);
    assert(arrival->sender == &c);
    assert(arrival->size == sizeof(whole));
    assert(arrival->options == header.options);
    assert(arrival->seqnum == 77);
    assert(arrival->msgid == 300);
    assert(arrival->stream == 6);
    assert(arrival->stream_seqnum == 9);
    assert(memcmp(msg_arrival_get_data(arrival), whole, sizeof(whole)) == 0);
    fast_free(arrival);

    header = make_header(RDP_FLAG_FRAGMENT | RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, 88, 401, 41, 1, 2, 7, 10, sizeof(last));
    assert(rx_validate_fragment_arrival(&c, &header) == RDP_RX_ACCEPT);
    assert(rx_assemble(&c, &header, (char *)last) == NULL);
    assert(c.rx_reassembly_pool.list.size == 1);
    assert(arrival_fragid_lookup(&c.rx_reassembly_pool, &header.fragid) != NULL);
    header = make_header(RDP_FLAG_FRAGMENT | RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, 87, 400, 41, 0, 2, 7, 9, sizeof(first));
    assert(rx_validate_fragment_arrival(&c, &header) == RDP_RX_ACCEPT);
    arrival = rx_assemble(&c, &header, (char *)first);
    assert(arrival != NULL);
    assert(c.rx_reassembly_pool.list.size == 0);
    assert(arrival->sender == &c);
    assert(arrival->size == sizeof(first) + sizeof(last));
    assert(arrival->seqnum == 87);
    assert(arrival->msgid == 400);
    assert(arrival->stream == 7);
    assert(arrival->stream_seqnum == 9);
    assert(memcmp(msg_arrival_get_data(arrival), first, sizeof(first)) == 0);
    assert(memcmp(msg_arrival_get_data(arrival) + sizeof(first), last, sizeof(last)) == 0);
    fast_free(arrival);

    header = make_header(RDP_FLAG_FRAGMENT | RDP_FLAG_MSGID, 90, 500, 42, 0, 2, 0, 0, sizeof(first));
    assert(rx_assemble(&c, &header, (char *)first) == NULL);
    assert(c.rx_reassembly_pool.list.size == 1);
    header = make_header(RDP_FLAG_FRAGMENT | RDP_FLAG_MSGID, 91, 501, 42, 1, 2, 0, 0, sizeof(last));
    assert(rx_validate_fragment_arrival(&c, &header) == RDP_RX_ACCEPT);
    arrival = rx_assemble(&c, &header, (char *)last);
    assert(arrival != NULL);
    assert(c.rx_reassembly_pool.list.size == 0);
    assert(arrival->size == sizeof(first) + sizeof(last));
    assert(memcmp(msg_arrival_get_data(arrival), first, sizeof(first)) == 0);
    assert(memcmp(msg_arrival_get_data(arrival) + sizeof(first), last, sizeof(last)) == 0);
    fast_free(arrival);

    rx_destroy(&c);
}

static void test_fin_helpers_and_flush_ownership(void)
{
    connection_t c;
    msg_arrival_t *fragment;
    msg_arrival_t *fin;
    uint32_t stream;

    initialize_receive(&c);
    assert(rx_create(&c) == 0);
    assert(rx_rcvd_all_msgids(&c) == 0);
    assert(rx_fin_waiting(&c) == 0);

    fin = allocate_arrival(0);
    fin->msgid = UINT16_C(0x3132);
    fin->options = RDP_FLAG_FIN | RDP_FLAG_MSGID;
    rx_save_fin_arrival(&c, fin);
    assert(c.rx_fin_recvd != 0);
    assert(c.rx_fin_msgid == UINT16_C(0x3132));
    assert(c.rx_fin_storage == fin);
    assert(rx_fin_waiting(&c) != 0);
    assert(rx_rcvd_all_msgids(&c) == 0);
    c.rx_received_all_thru = UINT16_C(0x3132);
    assert(rx_rcvd_all_msgids(&c) != 0);
    assert(rx_load_fin_arrival(&c) == fin);
    assert(c.rx_fin_storage == NULL);
    assert(c.rx_fin_recvd != 0);
    assert(c.rx_fin_msgid == UINT16_C(0x3132));
    assert(rx_fin_waiting(&c) == 0);
    assert(rx_rcvd_all_msgids(&c) != 0);
    fast_free(fin);

    fragment = allocate_arrival(UINT16_C(0x5152));
    arrival_fragid_insert(&c.rx_reassembly_pool, fragment);
    for (stream = 0; stream < RDP_STREAM_COUNT; ++stream)
    {
        msg_arrival_t *sequenced = allocate_arrival(0);

        sequenced->options = RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED;
        sequenced->stream = (uint8_t)stream;
        sequenced->stream_seqnum = (uint8_t)(stream + 1u);
        rx_sort_into_sequence(&c, sequenced);
    }
    fin = allocate_arrival(0);
    fin->msgid = UINT16_C(0x6162);
    c.rx_fin_recvd = 0;
    c.rx_fin_storage = NULL;
    rx_save_fin_arrival(&c, fin);
    c.rx_guaranteed_stream_seqnum[8] = 6;
    c.rx_best_effort_stream_seqnum[8] = 700;

    rx_flush_input_buffers(&c);
    assert(c.rx_reassembly_pool.list.head == NULL);
    assert(c.rx_reassembly_pool.list.tail == NULL);
    assert(c.rx_reassembly_pool.list.size == 0);
    for (stream = 0; stream < RDP_STREAM_COUNT; ++stream)
    {
        assert(c.rx_sequencer[stream].list.head == NULL);
        assert(c.rx_sequencer[stream].list.tail == NULL);
        assert(c.rx_sequencer[stream].list.size == 0);
    }
    assert(c.rx_fin_storage == NULL);
    assert(c.rx_fin_recvd != 0);
    assert(c.rx_fin_msgid == UINT16_C(0x6162));
    assert(c.rx_guaranteed_stream_seqnum[8] == 6);
    assert(c.rx_best_effort_stream_seqnum[8] == 700);
    rx_destroy(&c);
}

#ifdef RDP_DEAD_CODE
static void test_dead_formatter_is_referenced(void)
{
    connection_stat statistics;
    char buffer[8192];
    uint32_t length;

    memset(&statistics, 0, sizeof(statistics));
    statistics.ack_only_packets_tx = 3;
    statistics.ack_and_data_packets_tx = 4;
    statistics.icmp_unreachable[7] = 5;
    statistics.icmp_unreachable[8] = 9;
    length = c_stat_format(&statistics, buffer, 0);

    assert(length == strlen(buffer));
    assert(strstr(buffer, "  3 ack-only packets (3 delayed)\n") != NULL);
    assert(strstr(buffer, " 4 packets without ack\n") != NULL);
    assert(strstr(buffer, " 5 icmp destination host unknown\n") != NULL);
    assert(strstr(buffer, " 5 icmp source host isolated (obsolete)\n") != NULL);
}
#endif

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    fast_malloc_init(1024u * 1024u);
    test_selective_init_create_and_destroy();
    test_packet_sequence_validation_and_recording();
    test_distance_32_sign_extension_false_duplicate_recovery();
    test_distance_33_masked_shift_relies_on_msgid_dedup();
    test_message_ids_and_rejected_syn_flag();
    test_ack_serialization();
    test_streams_and_sequencing();
    test_fragment_validation_and_assembly();
    test_fin_helpers_and_flush_ownership();
#ifdef RDP_DEAD_CODE
    test_dead_formatter_is_referenced();
#endif
    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return 0;
}
