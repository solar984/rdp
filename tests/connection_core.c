// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "connection.h"
#include "eventq.h"
#include "fast.h"
#include "list.h"
#include "rdp.h"
#include "stats.h"
#include "rdpstat.h"
#include "rxq.h"
#include "txq.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(sizeof(connection_stat) == 0x10C, "connection_stat recovered size");
_Static_assert(offsetof(connection_t, cn_app_ptr) == 0, "connection_t must begin with application pointers");
_Static_assert(offsetof(connection_t, cn_rdp) == 3u * sizeof(void *), "connection_t owner moved");
_Static_assert(offsetof(connection_t, stat) > offsetof(connection_t, cn_flags), "connection_t receive state order changed");
_Static_assert(offsetof(connection_t, tx_socket) > offsetof(connection_t, rx_recent_seqnum_history), "connection_t tx state order changed");
_Static_assert(offsetof(connection_t, trace_next_index) > offsetof(connection_t, trace_probes), "connection_t trace tail changed");

#if defined(_WIN32) && !defined(_WIN64)
_Static_assert(offsetof(connection_t, cn_rdp) == 0x000C, "connection_t::cn_rdp x86 offset");
_Static_assert(offsetof(connection_t, cn_ref_count) == 0x0020, "connection_t::cn_ref_count x86 offset");
_Static_assert(offsetof(connection_t, stat) == 0x0068 + RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::stat x86 offset");
_Static_assert(offsetof(connection_t, rx_reassembly_pool) == 0x039C + RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::rx_reassembly_pool x86 offset");
_Static_assert(offsetof(connection_t, rx_sequencer) == 0x03F0 + RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::rx_sequencer x86 offset");
_Static_assert(offsetof(connection_t, tx_socket) == 0x05A4 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_socket x86 offset");
_Static_assert(offsetof(connection_t, tx_outstanding_packets) == 0x07C8 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_outstanding_packets x86 offset");
_Static_assert(offsetof(connection_t, tx_bandwidth) == 0x0810 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_bandwidth x86 offset");
_Static_assert(offsetof(connection_t, trace_remote_addr) == 0x09A8 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::trace_remote_addr x86 offset");
_Static_assert(offsetof(connection_t, trace_next_index) == 0x09E0 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::trace_next_index x86 offset");
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
_Static_assert(sizeof(connection_t) == 0x09E8 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "faithful connection_t x86 size");
#else
_Static_assert(offsetof(connection_t, rdplib_keepalive_interval_ms) == 0x09E8 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "checked connection extension x86 offset");
#endif
#endif

_Static_assert(_Generic(&connection_init, void (*)(connection_t *, rdp_t *, struct sockaddr *, uint32_t): 1, default: 0), "connection_init signature");
_Static_assert(_Generic(&connection_create, uint32_t (*)(connection_t *): 1, default: 0), "connection_create signature");
_Static_assert(_Generic(&connection_destroy, void (*)(connection_t *): 1, default: 0), "connection_destroy signature");
_Static_assert(_Generic(&connection_recalc_event_timeout, void (*)(connection_t *, timeout_data *): 1, default: 0), "connection_recalc_event_timeout signature");
_Static_assert(_Generic(&connection_parse_and_validate_arrival, uint32_t (*)(connection_t *, uint16_t *, uint16_t, rdp_header_t *): 1, default: 0),
               "connection_parse_and_validate_arrival signature");
_Static_assert(_Generic(&connection_record_arrival, void (*)(connection_t *, rdp_header_t *, uint32_t *): 1, default: 0), "connection_record_arrival signature");
_Static_assert(_Generic(&connection_handle_icmp, void (*)(connection_t *, uint8_t, uint8_t, uint8_t, uint8_t, struct sockaddr_in *): 1, default: 0),
               "connection_handle_icmp signature");
_Static_assert(_Generic(&connection_event_process, void (*)(connection_t *, uint32_t, timeout_data *): 1, default: 0), "connection_event_process signature");
_Static_assert(_Generic(&connection_close, uint32_t (*)(connection_t *, uint32_t, uint32_t *, uevent_t *): 1, default: 0), "connection_close signature");
_Static_assert(_Generic(&connection_linger_expired, uint32_t (*)(connection_t *): 1, default: 0), "connection_linger_expired signature");
_Static_assert(_Generic(&connection_set_send_buffer_size, void (*)(connection_t *, uint32_t): 1, default: 0), "connection_set_send_buffer_size signature");
_Static_assert(_Generic(&connection_connected, uint32_t (*)(connection_t *): 1, default: 0), "connection_connected signature");
_Static_assert(_Generic(&connection_app_ptr, void **(*)(connection_t *): 1, default: 0), "connection_app_ptr signature");
_Static_assert(_Generic(&connection_get_remote_addr, struct sockaddr *(*)(connection_t *): 1, default: 0), "connection_get_remote_addr signature");
_Static_assert(_Generic(&connection_get_perf_stats, void (*)(connection_t *, perf_stats_t *): 1, default: 0), "connection_get_perf_stats signature");
_Static_assert(_Generic(&connection_get_disconnect_info, void (*)(connection_t *, disconnect_info_t *, uint32_t): 1, default: 0), "connection_get_disconnect_info signature");
#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&connection_keepalive, uint32_t (*)(connection_t *, uint32_t): 1, default: 0), "connection_keepalive signature");
_Static_assert(_Generic(&connection_set_timeouts, uint32_t (*)(connection_t *, uint32_t, uint32_t): 1, default: 0), "connection_set_timeouts signature");
_Static_assert(_Generic(&connection_get_last_rt_time, uint32_t (*)(connection_t *): 1, default: 0), "connection_get_last_rt_time signature");
#endif

static rdp_stat test_global_statistics;

static void initialize_connection(connection_t *connection)
{
    memset(connection, 0, sizeof(*connection));
    memset(&test_global_statistics, 0, sizeof(test_global_statistics));
    g_rdp_stat = &test_global_statistics;
    connection->tx_bandwidth.bandwidth = 3000;
    connection->tx_acked_thru = 0;
    connection->tx_next_msgid = 1;
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    connection->rdplib_keepalive_interval_ms = RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS;
#endif
}

static void store_network_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void make_ipv4_address(struct sockaddr_in *address, uint16_t port, uint32_t ipv4)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = RDP_TRANSMIT_ADDRESS_IPV4;
    address->sin_port = htons(port);
    address->sin_addr.s_addr = ipv4;
}

static void test_init_create_destroy_and_accessors(void)
{
    connection_t connection;
    rdp_t owner;
    struct sockaddr_in remote_address;
    uint32_t reference_count = UINT32_C(0x1234ABCD);

    memset(&connection, 0xA5, sizeof(connection));
    memset(&owner, 0, sizeof(owner));
    make_ipv4_address(&remote_address, 9000, UINT32_C(0x0100007F));
    owner.udp_socket = 17;
    owner.trace_socket = 19;
    owner.udp_socket_ttl = 61;
    connection.cn_ref_count = reference_count;

    connection_init(&connection, &owner, (struct sockaddr *)&remote_address, RDP_CONNECTION_FEATURE_KEEPALIVE | RDP_CONNECTION_FEATURE_TRACEROUTE);
    assert(connection.cn_ref_count == reference_count); // Hash insertion, not connection_init, owns the reference count.
    assert(connection.cn_rdp == &owner);
    assert(connection.cn_flags == (RDP_CONNECTION_FEATURE_KEEPALIVE | RDP_CONNECTION_FEATURE_TRACEROUTE));
    assert(connection.cn_addr_map_link.item == &connection && connection.cn_addr_map_link.key.p == &connection.tx_remote_addr);
    assert(connection.cn_event_queue_link.item == &connection && connection.cn_event_queue_link.key.p == &connection.cn_event_time);
    assert(connection.cn_event_time.infinite == 1 && connection.cn_event_time.time == 0 && connection.cn_event_type == CONNECTION_EVENT_NONE);
    assert(connection.cn_closed == 0 && connection.cn_delete_time == 0 && connection.cn_accepted == 0 && connection.cn_abort == 0);
    assert(connection_app_ptr(&connection) == connection.cn_app_ptr);
    assert(connection.cn_app_ptr[0] == NULL && connection.cn_app_ptr[1] == NULL && connection.cn_app_ptr[2] == NULL);
    assert(connection_get_remote_addr(&connection) == &connection.tx_remote_addr);
    assert(memcmp(&connection.tx_remote_addr, &remote_address, sizeof(connection.tx_remote_addr)) == 0);
    assert(connection.tx_socket == owner.udp_socket && connection.trace_socket == owner.trace_socket && connection.trace_udp_ttl == owner.udp_socket_ttl);
    assert(connection.tx_acked_thru == (uint16_t)(connection.tx_next_msgid - 1u) && connection.tx_syn_msgid == connection.tx_next_msgid);
    assert(connection.tx_connected == 1 && connection_connected(&connection) == 1);
    assert(connection.rx_highest_seqnum_received == UINT16_MAX && connection.rx_recent_seqnum_history == UINT64_MAX);
    assert(connection.rx_reassembly_pool.list.head == NULL && connection.rx_reassembly_pool.list.size == 0);
    assert(memcmp(&connection.stat, &(connection_stat){0}, sizeof(connection.stat)) == 0);
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(connection.rdplib_keepalive_interval_ms == RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS);
    assert(connection.rdplib_packet_drop_callback == NULL && connection.rdplib_packet_drop_context == NULL);
#endif

    connection.cn_app_ptr[1] = &owner;
    assert(connection_app_ptr(&connection)[1] == &owner);
    connection_set_send_buffer_size(&connection, 12345);
    assert(connection.tx_send_buffer_size == 12345);
    assert(connection_create(&connection) == 0);
    assert(connection.tx_outstanding_packets.queue_size == 0 && connection.tx_virgin_packets.queue_size == 0 && connection.tx_delayed_packets.queue_size == 0);
    connection_destroy(&connection);
}

static void test_event_selection(void)
{
    connection_t connection;
    timeout_data timeout;

    initialize_connection(&connection);
    connection.tx_connected = 1;
    connection.cn_flags = 1;
    connection.rx_time_last_msgid_arrival = 1;
    connection.tx_time_last_guaranteed_send = 100;

    // Receiving reliable traffic does not establish the opposite transmit
    // direction. Keepalive starts only after this direction has sent SYN on
    // its first reliable message.
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.infinite);
    assert(connection.cn_event_type == CONNECTION_EVENT_NONE);

    connection.rx_time_last_msgid_arrival = 0;
    connection.tx_syn_sent = 1;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(!timeout.infinite);
    assert(timeout.time == 10100);
    assert(connection.cn_event_type == CONNECTION_EVENT_ALIVE);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    connection.rdplib_keepalive_interval_ms = 400;
#endif
    connection_recalc_event_timeout(&connection, &timeout);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(timeout.time == 10100); // The recovered scheduler retains its literal interval.
#else
    assert(timeout.time == 500);
#endif
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    connection.rdplib_keepalive_interval_ms = RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS;
#endif

    connection.cn_flags = 3;
    connection.trace_time = 10100;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.time == 10100);
    assert(connection.cn_event_type == CONNECTION_EVENT_ALIVE); // Equal deadlines retain the earlier source.

    connection.trace_time = 10099;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.time == 10099);
    assert(connection.cn_event_type == CONNECTION_EVENT_TRACE);

    connection.cn_closed = 1;
    connection.cn_delete_time = 10099;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.time == 10099);
    assert(connection.cn_event_type == CONNECTION_EVENT_TRACE);

    connection.cn_delete_time = 10098;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.time == 10098);
    assert(connection.cn_event_type == CONNECTION_EVENT_DELETE);
    timeout.infinite = 0;
    timeout.time = UINT32_C(0xA5A5A5A5);
    connection_event_process(&connection, 10098, &timeout);
    assert(timeout.infinite == 0 && timeout.time == UINT32_C(0xA5A5A5A5)); // A delete event returns without rescheduling.

    initialize_connection(&connection);
    connection.tx_connected = 1;
    connection.tx_stopped = 1;
    connection.tx_delayed_ack = 1;
    connection.tx_ack_time = 500;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.time == 500);
    assert(connection.cn_event_type == CONNECTION_EVENT_TX);
}

static void test_trace_retransmission_clamp(void)
{
    connection_t connection;
    timeout_data timeout;

    initialize_connection(&connection);
    connection.cn_flags = 2;
    connection.trace_en_route = 1;
    connection.trace_time = 100;

    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.time == 150);
    assert(connection.cn_event_type == CONNECTION_EVENT_TRACE);

    connection.tx_rt_tracker.weighted_avg = UINT16_MAX;
    connection.tx_rt_tracker.std_deviation = 1;
    connection_recalc_event_timeout(&connection, &timeout);
    assert(timeout.time == 100u + UINT16_MAX);
}

static void test_parser_preflight_and_unaligned_selected_fields(void)
{
    connection_t connection;
    rdp_header_t header;
    uint16_t packet_words[32];
    uint8_t *packet = (uint8_t *)packet_words;
    uint16_t options = RDP_FLAG_SYN | RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT | RDP_FLAG_SEQUENCED | RDP_FLAG_ACKTHRU | (UINT16_C(1) << 4);

    initialize_connection(&connection);
    connection.tx_connected = 0;
    connection.tx_acked_thru = 100;
    connection.tx_next_msgid = 102;
    connection.rx_highest_seqnum_received = UINT16_MAX;
    connection.rx_recent_seqnum_history = UINT64_MAX;
    bitarray_clear(&connection.tx_outstanding_packet_mask);
    memset(packet_words, 0, sizeof(packet_words));
    store_network_u16(packet, options);
    store_network_u16(packet + 2, 1);
    store_network_u16(packet + 4, 100);
    packet[6] = UINT8_C(0x80); // One byte ACK mask puts every selected 16 bit field that follows at an odd address.
    store_network_u16(packet + 7, 500);
    store_network_u16(packet + 9, 7);
    store_network_u16(packet + 11, 1);
    store_network_u16(packet + 13, 2);
    packet[15] = 3;
    packet[16] = 4;
    packet[17] = UINT8_C(0xDD);

    memset(&header, 0, sizeof(header));
    assert(connection_parse_and_validate_arrival(&connection, packet_words, 18, &header) == RDP_RX_ACCEPT);
    assert(header.options == options && header.seqnum == 1 && header.ack == (uint16_t *)(packet + 4));
    assert(header.msgid == 500 && header.fragid == 7 && header.frag_number == 1 && header.frag_total == 2);
    assert(header.stream == 3 && header.stream_seqnum == 4 && header.header_size == 17 && header.data_size == 1);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    {
        uint8_t odd_storage[8] = {0};
        uint8_t *odd_packet = odd_storage + 1;

        connection.rx_highest_seqnum_received = 3;
        connection.rx_recent_seqnum_history = 0;
        store_network_u16(odd_packet, 0);
        store_network_u16(odd_packet + 2, 4);
        memset(&header, 0, sizeof(header));
        assert(connection_parse_and_validate_arrival(&connection, (uint16_t *)odd_packet, 4, &header) == RDP_RX_ACCEPT);
        assert(header.options == 0 && header.seqnum == 4 && header.ack == (uint16_t *)(odd_packet + 4) && header.header_size == 4 && header.data_size == 0);
    }

    memset(&header, 0xA5, sizeof(header));
    memset(packet_words, 0, sizeof(packet_words));
    assert(connection_parse_and_validate_arrival(&connection, packet_words, 2, &header) == RDP_RX_ABORT);
    assert(header.options == UINT16_C(0xA5A5));
    assert(connection.stat.discarded_too_short == 1 && test_global_statistics.discarded_too_short == 1);

    memset(&header, 0xA5, sizeof(header));
    store_network_u16(packet, RDP_FLAG_SYN | RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT);
    store_network_u16(packet + 2, 2);
    store_network_u16(packet + 4, 501);
    assert(connection_parse_and_validate_arrival(&connection, packet_words, 10, &header) == RDP_RX_ABORT);
    assert(header.options == UINT16_C(0xA5A5));
    assert(connection.stat.discarded_too_short == 2 && test_global_statistics.discarded_too_short == 2);
#endif

    memset(packet_words, 0, sizeof(packet_words));
    store_network_u16(packet, RDP_FLAG_SYN | RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT);
    store_network_u16(packet + 2, 3);
    store_network_u16(packet + 4, 502);
    store_network_u16(packet + 6, 8);
    store_network_u16(packet + 8, 100);
    store_network_u16(packet + 10, 101);
    packet[12] = UINT8_C(0xEE);
    memset(&header, 0, sizeof(header));
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(connection_parse_and_validate_arrival(&connection, packet_words, 13, &header) == RDP_RX_ACCEPT);
    assert(header.frag_number == 100 && header.frag_total == 101 && header.data_size == 1);
#else
    assert(connection_parse_and_validate_arrival(&connection, packet_words, 13, &header) == RDP_RX_ABORT);
    assert(connection.stat.discarded_bad_fragment == 1 && test_global_statistics.discarded_bad_fragment == 1);
#endif
}

static void test_fragment_payload_exact_fit(void)
{
    enum
    {
        FRAGMENT_HEADER_BYTES = 12,
        EXACT_PACKET_BYTES = FRAGMENT_HEADER_BYTES + RDP_FRAGMENT_PAYLOAD_BYTES
    };
    connection_t connection;
    rdp_header_t header;
    uint16_t packet_words[(EXACT_PACKET_BYTES + 2u) / 2u];
    uint8_t *packet = (uint8_t *)packet_words;

    initialize_connection(&connection);
    connection.tx_connected = 0;
    connection.rx_highest_seqnum_received = UINT16_MAX;
    connection.rx_recent_seqnum_history = UINT64_MAX;
    memset(packet_words, 0, sizeof(packet_words));
    store_network_u16(packet, RDP_FLAG_SYN | RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT);
    store_network_u16(packet + 2, 1);
    store_network_u16(packet + 4, 1);
    store_network_u16(packet + 6, 7);
    store_network_u16(packet + 8, 0);
    store_network_u16(packet + 10, 2);

    memset(&header, 0, sizeof(header));
    assert(connection_parse_and_validate_arrival(&connection, packet_words, EXACT_PACKET_BYTES, &header) == RDP_RX_ACCEPT);
    assert(header.header_size == FRAGMENT_HEADER_BYTES && header.data_size == RDP_FRAGMENT_PAYLOAD_BYTES);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    memset(&header, 0xA5, sizeof(header));
    assert(connection_parse_and_validate_arrival(&connection, packet_words, EXACT_PACKET_BYTES + 1u, &header) == RDP_RX_ABORT);
    assert(header.options == UINT16_C(0xA5A5));
    assert(connection.stat.discarded_bad_fragment == 1 && test_global_statistics.discarded_bad_fragment == 1);
#endif
}

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
static void test_one_byte_preflight_aborts_connected_peer(void)
{
    connection_t connection;
    rdp_header_t header;
    uint16_t packet_word = 0;

    initialize_connection(&connection);
    connection.tx_connected = 1;
    memset(&header, 0xA5, sizeof(header));

    assert(connection_parse_and_validate_arrival(&connection, &packet_word, 1, &header) == RDP_RX_ABORT);
    assert(connection.stat.discarded_too_short == 1);
    assert(test_global_statistics.discarded_too_short == 1);
    assert(!connection.tx_connected && connection.tx_stopped);
    assert(connection.tx_disconnect_reason == RDP_DISCONNECT_REASON_PROTOCOL_ERROR);
}
#endif

static void test_icmp_recording_and_abort(void)
{
    connection_t connection;
    struct sockaddr_in source_address;
    uint32_t expected_source = UINT32_C(0x44332211);

    memset(&source_address, 0, sizeof(source_address));
    source_address.sin_addr.s_addr = expected_source;
    initialize_connection(&connection);
    connection_handle_icmp(&connection, 4, 0, 0, 0, &source_address);
    assert(connection.rx_icmp_received == 1);
    assert(connection.rx_icmp_type == 4);
    assert(connection.rx_icmp_code == 0);
    assert(connection.rx_icmp_from.s_addr == expected_source);
    assert(test_global_statistics.icmp_source_quench == 1);
    assert(connection.stat.icmp_source_quench == 1);

    connection_handle_icmp(&connection, 2, 0, 0, 0, &source_address);
    assert(connection.rx_icmp_received == 1);

    initialize_connection(&connection);
    connection.tx_connected = 1;
    connection_handle_icmp(&connection, 3, 3, 0, 0, &source_address);
    assert(!connection.tx_connected);
    assert(connection.tx_stopped);
    assert(connection.tx_disconnect_reason == RDP_DISCONNECT_REASON_ICMP);
    assert(test_global_statistics.icmp_unreachable[3] == 1);
    assert(test_global_statistics.connections_dropped_unreachable[3] == 1);
}

static void test_trace_icmp_is_separate_from_ordinary_icmp(void)
{
    connection_t connection;
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    struct sockaddr_in aligned_source_address;
#else
    uint8_t source_storage[sizeof(struct sockaddr_in) + 1u];
#endif
    struct sockaddr_in *source_address;
    trace_probe_t probe;
    uint32_t before;
    uint32_t expected_source = UINT32_C(0x04030201);

#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    make_ipv4_address(&aligned_source_address, 0, expected_source);
    source_address = &aligned_source_address;
#else
    memset(source_storage, 0, sizeof(source_storage));
    source_address = (struct sockaddr_in *)(source_storage + 1);
    memcpy((uint8_t *)source_address + offsetof(struct sockaddr_in, sin_addr), &expected_source, sizeof(expected_source));
#endif
    initialize_connection(&connection);
    memset(&probe, 0, sizeof(probe));
    before = time_get_ms();
    probe.time_sent = before - 25u;
    probe.ttl = 5;
    connection.trace_probes = &probe;
    connection.trace_next_index = 1;
    connection.trace_max_ttl = 30;
    connection.trace_next_ttl = 9;
    connection.trace_en_route = 1;

    connection_handle_icmp(&connection, 3, 3, 1, 0, source_address);
    assert(probe.reply_time >= 25u && probe.reply_time < 5000u);
    assert(probe.icmp_from.s_addr == expected_source && probe.icmp_type == 3 && probe.icmp_code == 3);
    assert(connection.trace_max_ttl == 5 && connection.trace_next_ttl == 1 && connection.trace_pass == 1);
    assert(connection.trace_en_route == 0 && connection.rx_icmp_received == 0);
    assert(test_global_statistics.icmp_unreachable[3] == 0);

    // The May path does not defer an ordinary ICMP for five seconds after data arrival.
    connection.tx_connected = 1;
    connection.rx_time_last_arrival = time_get_ms();
    connection_handle_icmp(&connection, 3, 3, 0, 0, source_address);
    assert(connection.rx_icmp_received == 1 && !connection.tx_connected);
    assert(connection.tx_disconnect_reason == RDP_DISCONNECT_REASON_ICMP);
    assert(test_global_statistics.icmp_unreachable[3] == 1);
}

static void test_record_reset_stop_duplicate_and_delayed_ack(void)
{
    connection_t connection;
    rdp_header_t header;
    uint32_t duplicate;
    uint32_t first_ack_time;

    initialize_connection(&connection);
    connection.tx_connected = 1;
    memset(&header, 0, sizeof(header));
    header.options = RDP_FLAG_RESET;
    header.seqnum = 1;
    header.header_size = 4;
    connection_record_arrival(&connection, &header, &duplicate);
    assert(duplicate == 0 && !connection.tx_connected && connection.tx_stopped);
    assert(connection.tx_disconnect_reason == RDP_DISCONNECT_REASON_PEER_RESET);
    assert(connection.stat.header_bytes_rx == 0 && connection.tx_delayed_ack == 0); // RESET returns before the normal record path.

    initialize_connection(&connection);
    connection.tx_connected = 1;
    connection.rx_highest_seqnum_received = 0;
    connection.rx_recent_seqnum_history = 0;
    memset(&header, 0, sizeof(header));
    header.options = RDP_FLAG_STOP;
    header.seqnum = 1;
    header.header_size = 4;
    connection_record_arrival(&connection, &header, &duplicate);
    assert(duplicate == 0 && connection.tx_stopped);
    assert(connection.stat.packets_rx_in_sequence == 1 && connection.stat.header_bytes_rx == 4);

    initialize_connection(&connection);
    connection.tx_connected = 1;
    connection.rx_highest_seqnum_received = UINT16_MAX;
    connection.rx_recent_seqnum_history = UINT64_MAX;
    memset(&header, 0, sizeof(header));
    header.options = RDP_FLAG_MSGID;
    header.seqnum = 1;
    header.msgid = 400;
    header.header_size = 6;
    header.data_size = 5;
    connection_record_arrival(&connection, &header, &duplicate);
    assert(duplicate == 0);
    assert(connection.stat.guaranteed_packets_rx == 1 && connection.stat.guaranteed_bytes_rx == 5);
    assert(connection.tx_delayed_ack == 1);
    first_ack_time = connection.tx_ack_time;

    header.seqnum = 2;
    connection_record_arrival(&connection, &header, &duplicate);
    assert(duplicate != 0);
    assert(connection.stat.guaranteed_packets_rx == 1 && connection.stat.guaranteed_bytes_rx == 5);
    assert(connection.stat.duplicate_packets_rx == 1 && connection.stat.duplicate_bytes_rx == 5);
    assert(connection.stat.header_bytes_rx == 12 && test_global_statistics.header_bytes_rx == 12);
    assert(connection.tx_delayed_ack == 1 && connection.tx_ack_time == first_ack_time); // A second reliable packet does not extend the existing delayed ACK.
}

static void test_recent_icmp_diagnosis_override(void)
{
    connection_t connection;

    initialize_connection(&connection);
    connection.tx_connected = 1;
    connection.rx_icmp_received = 1;
    connection.rx_icmp_type = 3;
    connection.rx_icmp_code = 3;
    connection.rx_icmp_time = time_get_ms();

    tx_abort_connection(&connection, RDP_DISCONNECT_REASON_PEER_RESET);
    assert(connection.tx_disconnect_reason == RDP_DISCONNECT_REASON_ICMP);
    assert(test_global_statistics.connections_dropped_rst == 1);
    assert(test_global_statistics.connections_dropped_unreachable[3] == 0);
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
    txq_init(&connection.tx_outstanding_packets);
    connection.tx_rt_tracker.weighted_avg = 50;
    connection.tx_rt_tracker.std_deviation = 0;

    head_message.txq_link.item = &head_message;
    head_message.time_first_sent = now_ms - 100u;
    head_message.time_last_sent = now_ms - 10u;
    txq_add_tail(&connection.tx_outstanding_packets, &head_message);

    older_message.txq_link.item = &older_message;
    older_message.time_first_sent = now_ms - 200u;
    older_message.time_last_sent = now_ms - 20u;
    txq_add_tail(&connection.tx_outstanding_packets, &older_message);

    assert(tx_get_stall_time(&connection) >= 150u);
    assert(connection.tx_outstanding_packets.list.head == &head_message.txq_link);
    assert(connection.tx_outstanding_packets.list.tail == &older_message.txq_link);
    assert(connection.tx_outstanding_packets.list.size == 2);
}

static void test_perf_disconnect_and_dead_accessors(void)
{
    connection_t connection;
    rdp_t owner;
    struct sockaddr_in remote_address;
    perf_stats_t performance;
    disconnect_info_t disconnect;
    disconnect_info_t untouched;

    memset(&owner, 0, sizeof(owner));
    owner.udp_socket = 21;
    owner.trace_socket = 22;
    make_ipv4_address(&remote_address, 9001, UINT32_C(0x0200007F));
    connhash_init(&owner.addr_map);
    assert(connhash_create(&owner.addr_map, 1) == 0);

    memset(&connection, 0, sizeof(connection));
    connection_init(&connection, &owner, (struct sockaddr *)&remote_address, 0);
    assert(connection_create(&connection) == 0);
    connhash_insert(&owner.addr_map, &connection);
    connection.rx_time_last_arrival = 1234;
    connection.rx_recent_seqnum_history = UINT64_C(0x0123456789ABCDEF);
    connection.rx_highest_seqnum_received = 4321;
    connection.tx_rt_tracker.weighted_avg = 57;
    connection.tx_rt_tracker.std_deviation = 9;
    connection.tx_last_rt_time = 63;
    connection.tx_outstanding_packets.queue_size = 3;
    connection.tx_virgin_packets.queue_size = 5;
    connection.tx_delayed_packets.queue_size = 7;

    memset(&performance, 0xA5, sizeof(performance));
    connection_get_perf_stats(&connection, &performance);
    assert(performance.time_last_arrival == 1234);
    assert(performance.recent_seqnum_history == UINT64_C(0x0123456789ABCDEF));
    assert(performance.highest_seqnum_received == 4321);
    assert(performance.average_rt_time == 57 && performance.std_deviation == 9 && performance.last_rt_time == 63);
    assert(performance.queue_size == 15 && performance.stall_time == 0);
    assert(connection.cn_ref_count == 1);

    connection.tx_disconnect_reason = RDP_DISCONNECT_REASON_PROTOCOL_ERROR;
    connection.rx_icmp_type = 11;
    connection.rx_icmp_code = 1;
    connection.rx_icmp_from.s_addr = UINT32_C(0xAABBCCDD);
    memset(&disconnect, 0xA5, sizeof(disconnect));
    connection_get_disconnect_info(&connection, &disconnect, sizeof(disconnect));
    assert(disconnect.disconnect_reason == RDP_DISCONNECT_REASON_PROTOCOL_ERROR);
    assert(disconnect.icmp_type == 11 && disconnect.icmp_code == 1 && disconnect.icmp_from == UINT32_C(0xAABBCCDD));

    memset(&untouched, 0x5A, sizeof(untouched));
    disconnect = untouched;
    connection_get_disconnect_info(&connection, &disconnect, sizeof(disconnect) - 1u);
    assert(memcmp(&disconnect, &untouched, sizeof(disconnect)) == 0);

#ifdef RDP_DEAD_CODE
    assert(connection_set_timeouts(&connection, 4567, 8910) == RDP_CONNECTION_SEND_OK);
    assert(connection.tx_max_message_age == 4567 && connection.tx_max_service_outage == 8910);
    connection.tx_last_rt_time = 72;
    assert(connection_get_last_rt_time(&connection) == 72);
    (void)&connection_keepalive; // The mutator also resorts the owner event queue; signature coverage is sufficient here.
#endif

    connection.tx_outstanding_packets.queue_size = 0;
    connection.tx_virgin_packets.queue_size = 0;
    connection.tx_delayed_packets.queue_size = 0;
    assert(connhash_subref(&owner.addr_map, &connection) == &connection);
    connection_destroy(&connection);
    connhash_destroy(&owner.addr_map);
}

static void create_close_owner(rdp_t *owner)
{
    memset(owner, 0, sizeof(*owner));
    rdp_init(owner);
    assert(connhash_create(&owner->addr_map, 1) == 0);
    assert(eventq_create(&owner->conn_eventq, 2) == 0);
    assert(usemaphore_create(&owner->receive_semaphore) == 0);
    owner->wake_sent = 1; // Keep rdp_resort from needing a real wakeup socket.
}

static void destroy_close_owner(rdp_t *owner)
{
    assert(rxq_peek_head(&owner->message_rxq) == NULL);
    assert(rxq_peek_head(&owner->external_rxq) == NULL);
    connhash_destroy(&owner->addr_map);
    eventq_destroy(&owner->conn_eventq);
    usemaphore_destroy(&owner->receive_semaphore);
    rxq_destroy(&owner->message_rxq);
    umutex_destroy(&owner->message_rxq_mutex);
    rxq_destroy(&owner->external_rxq);
    umutex_destroy(&owner->external_rxq_mutex);
}

static connection_t *create_close_connection(rdp_t *owner, uint16_t port)
{
    connection_t *connection;
    struct sockaddr_in remote_address;

    connection = (connection_t *)rdplib_platform_malloc(sizeof(*connection));
    assert(connection != NULL);
    memset(connection, 0, sizeof(*connection));
    make_ipv4_address(&remote_address, port, UINT32_C(0x0300007F));
    connection_init(connection, owner, (struct sockaddr *)&remote_address, 0);
    assert(connection_create(connection) == 0);
    connhash_insert(&owner->addr_map, connection);
    assert(eventq_insert(&owner->conn_eventq, connection) == 0);
    return connection;
}

static msg_arrival_t *create_queued_arrival(connection_t *sender)
{
    msg_arrival_t *arrival = (msg_arrival_t *)fast_malloc(sizeof(*arrival));

    assert(arrival != NULL);
    memset(arrival, 0, sizeof(*arrival));
    arrival->rxq_link.item = arrival;
    arrival->sender = sender;
    return arrival;
}

static void test_close_queue_semaphore_result_and_linger(void)
{
    rdp_t owner;
    connection_t *connection;
    uevent_t completion_event;
    uint32_t all_acked;
    uint32_t before;

    fast_malloc_init(4u * 584u);
    assert(connection_close(NULL, 0, NULL, NULL) == RDP_CONNECTION_SEND_INVALID_ARGUMENT);
    create_close_owner(&owner);
    connection = create_close_connection(&owner, 9002);
    connection->tx_connected = 0;
    rxq_add_tail(&owner.message_rxq, create_queued_arrival(connection));
    rxq_add_tail(&owner.external_rxq, create_queued_arrival(connection));
    usemaphore_increment(&owner.receive_semaphore);
    uevent_init(&completion_event);
    assert(uevent_create(&completion_event) == 0);
    all_acked = UINT32_C(0xA5A5A5A5);

    assert(connection_close(connection, 0, &all_acked, &completion_event) == RDP_CONNECTION_SEND_OK);
    assert(all_acked == 0);
    uevent_wait(&completion_event);
    assert(!usemaphore_decrement(&owner.receive_semaphore, 0));
    assert(rxq_peek_head(&owner.message_rxq) == NULL && rxq_peek_head(&owner.external_rxq) == NULL);
    assert(eventq_peek_head(&owner.conn_eventq) == NULL);
    uevent_destroy(&completion_event);
    destroy_close_owner(&owner);

    create_close_owner(&owner);
    connection = create_close_connection(&owner, 9003);
    connection->tx_syn_sent = 1;
    connection->tx_syn_acked = 0; // Keep FIN queued; this test does not provide a real transport backend.
    uevent_init(&completion_event);
    assert(uevent_create(&completion_event) == 0);
    all_acked = UINT32_C(0x5A5A5A5A);
    before = time_get_ms();
    assert(connection_close(connection, 200, &all_acked, &completion_event) == RDP_CONNECTION_SEND_OK);
    assert(all_acked == UINT32_C(0x5A5A5A5A));
    assert(connection->cn_closed == 1 && connection->cn_delete_time - before >= 200u && connection->cn_delete_time - before < 5000u);
    assert(connection->tx_fin_sent == 1 && connection->tx_all_acked == &all_acked && connection->tx_all_acked_event == &completion_event);
    assert(!connection_linger_expired(connection));
    connection->cn_delete_time = time_get_ms() + 5u;
    assert(connection_linger_expired(connection));
    connection->cn_closed = 0;
    assert(!connection_linger_expired(connection)); // Source review also verifies that this false disposition still evaluates the clock.

    connection->tx_all_acked = NULL;
    connection->tx_all_acked_event = NULL;
    eventq_lock(&owner.conn_eventq);
    assert(eventq_remove_by_ptr(&owner.conn_eventq, connection) == connection);
    eventq_unlock(&owner.conn_eventq);
    assert(connhash_subref(&owner.addr_map, connection) == connection);
    connection_destroy(connection);
    rdplib_platform_free(connection);
    uevent_destroy(&completion_event);
    destroy_close_owner(&owner);
    fast_malloc_destroy();
}

static void test_ack_base_sign_extension(void)
{
    connection_t connection;
    uint16_t output_words[RDP_ACK_MAX_BYTES / sizeof(uint16_t) + 1];
    uint8_t *output = (uint8_t *)output_words;
    uint16_t flags = 0;

    initialize_connection(&connection);
    connection.rx_received_all_thru = UINT16_C(0x7FF0);
    connection.rx_msgid_count = 1;
    connection.rx_msgid_lo = UINT16_C(0x8001);
    connection.rx_msgid_hi = UINT16_C(0x8001);
    bitarray_clear(&connection.rx_others_received);

    // The reference code sign extends the pending minimum but zero extends
    // the cumulative base. This is not a modular distance: the
    // wrapped distance is +17, yet every client selects ACKTHRU here.
    assert(rx_append_ack(&connection, output_words, &flags) == 5);
    assert(flags == (RDP_FLAG_ACKTHRU | (UINT16_C(3) << 4)));
    assert(output[0] == 0x7F && output[1] == 0xF0);
    assert(connection.rx_msgid_count == 0);
    assert(connection.rx_msgid_lo == UINT16_C(0x7FF0));
}

static void test_packet_history_distance_64(void)
{
    connection_t connection;
    rdp_header_t header;

    initialize_connection(&connection);
    memset(&header, 0, sizeof(header));
    connection.rx_highest_seqnum_received = 100;
    connection.rx_recent_seqnum_history = UINT64_C(0x0123456789ABCDEF);
    header.seqnum = 164;

    rx_record_seqnum_arrival(&connection, &header);
    assert(connection.rx_recent_seqnum_history == UINT64_C(0x8000000000000000));
}

static void test_unreliable_stream_uses_packet_sequence(void)
{
    connection_t connection;
    msg_arrival_t message;

    initialize_connection(&connection);
    memset(&message, 0, sizeof(message));
    message.stream = 3;
    message.stream_seqnum = UINT8_MAX;
    message.seqnum = 1002;
    connection.rx_best_effort_stream_seqnum[3] = 1000;

    assert(rx_in_sequence(&connection, &message));
    assert(connection.rx_best_effort_stream_seqnum[3] == 1003);

    message.stream_seqnum = 0;
    message.seqnum = 1001;
    assert(!rx_in_sequence(&connection, &message));
    assert(connection.rx_best_effort_stream_seqnum[3] == 1003);

    connection.rx_best_effort_stream_seqnum[3] = UINT16_MAX;
    message.seqnum = 0;
    assert(rx_in_sequence(&connection, &message));
    assert(connection.rx_best_effort_stream_seqnum[3] == 1);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_init_create_destroy_and_accessors();
    test_event_selection();
    test_trace_retransmission_clamp();
    test_parser_preflight_and_unaligned_selected_fields();
    test_fragment_payload_exact_fit();
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    test_one_byte_preflight_aborts_connected_peer();
#endif
    test_icmp_recording_and_abort();
    test_trace_icmp_is_separate_from_ordinary_icmp();
    test_record_reset_stop_duplicate_and_delayed_ack();
    test_recent_icmp_diagnosis_override();
    test_stall_uses_oldest_first_send();
    test_perf_disconnect_and_dead_accessors();
    test_close_queue_semaphore_result_and_linger();
    test_ack_base_sign_extension();
    test_packet_history_distance_64();
    test_unreliable_stream_uses_packet_sequence();
    return 0;
}
