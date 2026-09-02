// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "connhash.h"
#include "fast.h"
#include "protocol_limits.h"
#include "rdp.h"
#include "rdplib_platform.h"
#include "rdpstat.h"
#include "rx.h"
#include "serial.h"
#include "trace.h"
#include "tx.h"
#include "usend.h"

_Static_assert(sizeof(bandwidth_t) == 0x10, "bandwidth_t must retain its recovered fourth slot");
_Static_assert(offsetof(connection_t, tx_remote_addr) == offsetof(connection_t, tx_socket) + sizeof(intptr_t), "connection_t transmit address moved");
_Static_assert(offsetof(connection_t, tx_next_seqnum) == offsetof(connection_t, tx_acked_thru) + sizeof(uint16_t), "connection_t sequence fields split");
_Static_assert(offsetof(connection_t, tx_rt_tracker) >= offsetof(connection_t, tx_modem) + sizeof(uint32_t), "connection_t RTT estimator overlaps tx_modem");
_Static_assert(offsetof(connection_t, tx_rt_tracker) - offsetof(connection_t, tx_modem) - sizeof(uint32_t) <= 4u, "connection_t RTT alignment gap grew");
_Static_assert(offsetof(connection_t, trace_udp_ttl) == offsetof(connection_t, trace_socket) + sizeof(intptr_t), "connection_t trace tail moved");

#if defined(_WIN32) && !defined(_WIN64)
_Static_assert(offsetof(connection_t, tx_acked_thru) == 0x05B8 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_acked_thru x86 offset");
_Static_assert(offsetof(connection_t, tx_outstanding_packet_mask) == 0x05C4 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_outstanding_packet_mask x86 offset");
_Static_assert(offsetof(connection_t, tx_outstanding_packets) == 0x07C8 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_outstanding_packets x86 offset");
_Static_assert(offsetof(connection_t, tx_bandwidth) == 0x0810 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_bandwidth x86 offset");
_Static_assert(offsetof(connection_t, tx_rt_tracker) == 0x0830 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_rt_tracker x86 offset");
_Static_assert(offsetof(connection_t, tx_syn_sent) == 0x0964 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_syn_sent x86 offset");
_Static_assert(offsetof(connection_t, tx_all_acked_event) == 0x09A0 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::tx_all_acked_event x86 offset");
_Static_assert(offsetof(connection_t, trace_remote_addr) == 0x09A8 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::trace_remote_addr x86 offset");
_Static_assert(offsetof(connection_t, trace_udp_ttl) == 0x09BC + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "connection_t::trace_udp_ttl x86 offset");
#endif

_Static_assert(_Generic(&tx_init, void (*)(connection_t *, rdp_t *, struct sockaddr *): 1, default: 0), "tx_init signature");
_Static_assert(_Generic(&tx_create, uint32_t (*)(connection_t *): 1, default: 0), "tx_create signature");
_Static_assert(_Generic(&tx_destroy, void (*)(connection_t *): 1, default: 0), "tx_destroy signature");
_Static_assert(_Generic(&tx_flush_output_buffers, void (*)(connection_t *): 1, default: 0), "tx_flush_output_buffers signature");
_Static_assert(_Generic(&tx_handle_ack, void (*)(connection_t *, uint16_t): 1, default: 0), "tx_handle_ack signature");
_Static_assert(_Generic(&tx_validate_ack_arrival, uint32_t (*)(connection_t *, rdp_header_t *, uint32_t *): 1, default: 0), "tx_validate_ack_arrival signature");
_Static_assert(_Generic(&tx_record_ack_arrival, void (*)(connection_t *, rdp_header_t *): 1, default: 0), "tx_record_ack_arrival signature");
_Static_assert(_Generic(&tx_send_ready, uint32_t (*)(connection_t *): 1, default: 0), "tx_send_ready signature");
_Static_assert(_Generic(&tx_send_virgin, void (*)(connection_t *, msg_outgoing_t *): 1, default: 0), "tx_send_virgin signature");
_Static_assert(_Generic(&tx_reserve_msgid, uint16_t (*)(connection_t *): 1, default: 0), "tx_reserve_msgid signature");
_Static_assert(_Generic(&tx_outgoing_msg_in_outstanding_range, uint32_t (*)(connection_t *, msg_outgoing_t *): 1, default: 0), "tx_outgoing range signature");
_Static_assert(_Generic(&tx_enqueue_outgoing, void (*)(connection_t *, msg_outgoing_t *): 1, default: 0), "tx_enqueue_outgoing signature");
_Static_assert(_Generic(&tx_send_fin, uint32_t (*)(connection_t *): 1, default: 0), "tx_send_fin signature");
_Static_assert(_Generic(&tx_send_alive, uint32_t (*)(connection_t *): 1, default: 0), "tx_send_alive signature");
_Static_assert(_Generic(&tx_get_event_time, void (*)(connection_t *, timeout_data *): 1, default: 0), "tx_get_event_time signature");
_Static_assert(_Generic(&tx_send_ready_virgins, uint32_t (*)(connection_t *): 1, default: 0), "tx_send_ready_virgins signature");
_Static_assert(_Generic(&tx_abort_connection, void (*)(connection_t *, uint32_t): 1, default: 0), "tx_abort_connection signature");
_Static_assert(_Generic(&tx_received_stopped, void (*)(connection_t *): 1, default: 0), "tx_received_stopped signature");
_Static_assert(_Generic(&tx_set_delayed_ack, void (*)(connection_t *): 1, default: 0), "tx_set_delayed_ack signature");
_Static_assert(_Generic(&tx_tx, void (*)(connection_t *): 1, default: 0), "tx_tx signature");
_Static_assert(_Generic(&tx_send_packet, uint32_t (*)(connection_t *, char *, uint32_t, uint16_t): 1, default: 0), "tx_send_packet signature");
_Static_assert(_Generic(&connection_send, uint32_t (*)(connection_t *, const char *, uint32_t, uint32_t, uint32_t): 1, default: 0), "connection_send signature");
_Static_assert(_Generic(&connection_sendv, uint32_t (*)(connection_t *, iov_t *, uint32_t, uint32_t, uint32_t): 1, default: 0), "connection_sendv signature");
_Static_assert(_Generic(&trace_start, uint32_t (*)(connection_t *): 1, default: 0), "trace_start signature");
_Static_assert(_Generic(&trace_send, uint32_t (*)(connection_t *): 1, default: 0), "trace_send signature");
_Static_assert(_Generic(&connection_set_max_data_rate, uint32_t (*)(connection_t *, uint32_t): 1, default: 0), "connection_set_max_data_rate signature");
_Static_assert(_Generic(&tx_get_stall_time, uint32_t (*)(connection_t *): 1, default: 0), "tx_get_stall_time signature");
_Static_assert(_Generic(&tx_needs_disconnect_msg, uint32_t (*)(connection_t *): 1, default: 0), "tx_needs_disconnect_msg signature");
_Static_assert(_Generic(&tx_get_queue_size, uint32_t (*)(connection_t *): 1, default: 0), "tx_get_queue_size signature");

enum
{
    TEST_BACKEND_NONE,
    TEST_BACKEND_USEND,
    TEST_BACKEND_RDPLIB_USEND,
    TEST_BACKEND_SERIAL,
    TEST_BACKEND_CALL_CAPACITY = 4,
    TEST_PACKET_CAPACITY = 2048
};

typedef struct test_allocation_header_t
{
    uint32_t marker;
    uint32_t size;
} test_allocation_header_t;

static const uint32_t TEST_ALLOCATION_MARKER = UINT32_C(0x51a110c5);
static rdp_stat test_statistics;
rdp_stat *g_rdp_stat;

static uint32_t test_now;
static uint32_t test_time_calls;
static uint32_t test_fast_calls;
static uint32_t test_fast_fail_at;
static uint32_t test_fast_live;
static uint32_t test_platform_live;
static connection_t *test_locked_connection;
static uint32_t test_lock_calls;
static uint32_t test_unlock_calls;
static uint32_t test_resort_calls;
static int test_resort_wake;
static uint32_t test_rx_flush_calls;
static uint32_t test_event_signal_calls;
static uevent_t *test_last_event;
static uint32_t test_serial_ready;
static uint32_t test_serial_empty_time;
static uint32_t test_backend_result;
static uint32_t test_backend_kind;
static uint32_t test_backend_calls;
static uint32_t test_backend_iov_len;
static uint8_t test_backend_packet[TEST_PACKET_CAPACITY];
static uint32_t test_backend_packet_size;
static uint32_t test_backend_result_count;
static uint32_t test_backend_results[TEST_BACKEND_CALL_CAPACITY];
static uint8_t test_backend_packets[TEST_BACKEND_CALL_CAPACITY][TEST_PACKET_CAPACITY];
static uint32_t test_backend_packet_sizes[TEST_BACKEND_CALL_CAPACITY];
static uint32_t test_ack_enabled;
static uint16_t test_ack_msgid;
static uint32_t test_ack_mask_bytes;
static int test_socket_results[4];
static uint32_t test_socket_result_count;
static uint32_t test_socket_calls;
static uint32_t test_socket_values[4];
static uint32_t test_socket_value_sizes[4];
static uint32_t test_last_socket_error_calls;
static int32_t test_datagram_result;
static uint32_t test_datagram_calls;
static uint8_t test_datagram_byte;

static void store_family(uint8_t address[16], uint16_t family)
{
    memcpy(address, &family, sizeof(family));
}

static uint16_t load_network_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void reset_harness(void)
{
    assert(test_fast_live == 0);
    assert(test_platform_live == 0);
    memset(&test_statistics, 0, sizeof(test_statistics));
    test_now = UINT32_C(100000);
    test_time_calls = 0;
    test_fast_calls = 0;
    test_fast_fail_at = 0;
    test_locked_connection = NULL;
    test_lock_calls = 0;
    test_unlock_calls = 0;
    test_resort_calls = 0;
    test_resort_wake = 0;
    test_rx_flush_calls = 0;
    test_event_signal_calls = 0;
    test_last_event = NULL;
    test_serial_ready = 1;
    test_serial_empty_time = test_now;
    test_backend_result = 0;
    test_backend_kind = TEST_BACKEND_NONE;
    test_backend_calls = 0;
    test_backend_iov_len = 0;
    test_backend_packet_size = 0;
    memset(test_backend_packet, 0, sizeof(test_backend_packet));
    test_backend_result_count = 0;
    memset(test_backend_results, 0, sizeof(test_backend_results));
    memset(test_backend_packets, 0, sizeof(test_backend_packets));
    memset(test_backend_packet_sizes, 0, sizeof(test_backend_packet_sizes));
    test_ack_enabled = 0;
    test_ack_msgid = 0;
    test_ack_mask_bytes = 0;
    memset(test_socket_results, 0, sizeof(test_socket_results));
    test_socket_result_count = 0;
    test_socket_calls = 0;
    memset(test_socket_values, 0, sizeof(test_socket_values));
    memset(test_socket_value_sizes, 0, sizeof(test_socket_value_sizes));
    test_last_socket_error_calls = 0;
    test_datagram_result = 1;
    test_datagram_calls = 0;
    test_datagram_byte = 0;
    g_rdp_stat = &test_statistics;
}

void *fast_malloc(uint32_t size)
{
    test_allocation_header_t *header;

    ++test_fast_calls;
    if (test_fast_fail_at && test_fast_calls == test_fast_fail_at)
    {
        return NULL;
    }
    header = (test_allocation_header_t *)malloc(sizeof(*header) + size);
    if (!header)
    {
        return NULL;
    }
    header->marker = TEST_ALLOCATION_MARKER;
    header->size = size;
    ++test_fast_live;
    return header + 1;
}

void fast_free(void *allocation)
{
    test_allocation_header_t *header;

    assert(allocation != NULL);
    header = (test_allocation_header_t *)allocation - 1;
    assert(header->marker == TEST_ALLOCATION_MARKER);
    header->marker = 0;
    assert(test_fast_live != 0);
    --test_fast_live;
    free(header);
}

uint32_t time_get_ms(void)
{
    ++test_time_calls;
    return test_now;
}

uint32_t rdplib_platform_wall_time_seconds(void)
{
    return UINT32_C(0x12345678);
}

int rdplib_random_next(void)
{
    return 0x1357;
}

void *rdplib_platform_malloc(size_t size)
{
    void *allocation = malloc(size ? size : 1u);

    if (allocation)
    {
        ++test_platform_live;
    }
    return allocation;
}

void rdplib_platform_free(void *allocation)
{
    if (allocation)
    {
        assert(test_platform_live != 0);
        --test_platform_live;
        free(allocation);
    }
}

void uevent_signal(uevent_t *event)
{
    ++test_event_signal_calls;
    test_last_event = event;
}

int rdplib_platform_socket_set_option(intptr_t endpoint, int level, int option, const void *value, uint32_t value_bytes)
{
    int result = 0;

    (void)endpoint;
    (void)level;
    (void)option;
    assert(test_socket_calls < 4);
    test_socket_value_sizes[test_socket_calls] = value_bytes;
    if (value && value_bytes >= sizeof(uint32_t))
    {
        memcpy(&test_socket_values[test_socket_calls], value, sizeof(uint32_t));
    }
    if (test_socket_calls < test_socket_result_count)
    {
        result = test_socket_results[test_socket_calls];
    }
    ++test_socket_calls;
    return result;
}

uint32_t rdplib_platform_last_socket_error(void)
{
    ++test_last_socket_error_calls;
    return 0;
}

int32_t rdplib_platform_send_datagram(intptr_t endpoint, const uint8_t *packet, uint32_t packet_bytes, const uint8_t destination[16])
{
    (void)endpoint;
    (void)destination;
    ++test_datagram_calls;
    assert(packet_bytes == 1);
    test_datagram_byte = packet[0];
    return test_datagram_result;
}

connection_t *connhash_lock(connhash_t *connhash, struct sockaddr *address)
{
    (void)connhash;
    assert(test_locked_connection != NULL);
    assert(address == &test_locked_connection->tx_remote_addr);
    ++test_lock_calls;
    return test_locked_connection;
}

void rdp_unlock(connection_t *c)
{
    assert(c == test_locked_connection);
    ++test_unlock_calls;
}

void rdp_resort(connection_t *c, uint32_t wake)
{
    assert(c == test_locked_connection);
    ++test_resort_calls;
    test_resort_wake = wake;
}

#if defined(RDPLIB_DEBUG) || defined(RDPLIB_SOURCE_FAITHFUL)
void format_sockaddr(char *buf, struct sockaddr *sa)
{
    (void)sa;
    strcpy(buf, "test");
}
#endif

void rx_flush_input_buffers(connection_t *c)
{
    (void)c;
    ++test_rx_flush_calls;
}

uint32_t rx_append_ack(connection_t *c, uint16_t *dst, uint16_t *options)
{
    uint8_t *mask;

    if (!test_ack_enabled)
    {
        return 0;
    }
    assert(test_ack_mask_bytes <= 15u);
    *options |= (uint16_t)(RDP_FLAG_ACKTHRU | (test_ack_mask_bytes << 4));
    *dst = htons(test_ack_msgid);
    mask = (uint8_t *)(dst + 1);
    memset(mask, 0x80, test_ack_mask_bytes);
    c->rx_msgid_count = 0;
    c->rx_msgid_lo = test_ack_msgid;
    c->rx_msgid_hi = test_ack_msgid;
    return 2u + test_ack_mask_bytes;
}

uint32_t rdp_serial_tx_ready(rdp_t *rdp)
{
    (void)rdp;
    return test_serial_ready;
}

uint32_t rdp_serial_get_time_empty(rdp_t *rdp)
{
    (void)rdp;
    return test_serial_empty_time;
}

static uint32_t capture_backend(uint32_t kind, iov_t *iov, uint32_t iov_len)
{
    uint32_t call_index;
    uint32_t index;
    uint32_t result;

    call_index = test_backend_calls;
    assert(call_index < TEST_BACKEND_CALL_CAPACITY);
    test_backend_kind = kind;
    test_backend_iov_len = iov_len;
    test_backend_packet_size = 0;
    for (index = 0; index < iov_len; ++index)
    {
        assert(iov[index].size <= sizeof(test_backend_packet) - test_backend_packet_size);
        if (iov[index].size)
        {
            assert(iov[index].data != NULL);
            memcpy(test_backend_packet + test_backend_packet_size, iov[index].data, iov[index].size);
        }
        test_backend_packet_size += iov[index].size;
    }
    memcpy(test_backend_packets[call_index], test_backend_packet, test_backend_packet_size);
    test_backend_packet_sizes[call_index] = test_backend_packet_size;
    result = call_index < test_backend_result_count ? test_backend_results[call_index] : test_backend_result;
    ++test_backend_calls;
    return result;
}

uint32_t usend(intptr_t socket, iov_t *iov, uint32_t iov_len, struct sockaddr *remote_addr, uint32_t encrypt, uint32_t crc)
{
    (void)socket;
    (void)remote_addr;
    (void)encrypt;
    (void)crc;
    return capture_backend(TEST_BACKEND_USEND, iov, iov_len);
}

uint32_t rdplib_usend_framed_size(uint32_t plaintext_bytes, uint32_t encrypt, uint32_t crc)
{
    if (crc || encrypt)
    {
        plaintext_bytes += sizeof(uint32_t);
    }
    if (encrypt)
    {
        plaintext_bytes = (plaintext_bytes + 8u) & ~UINT32_C(7);
    }
    return plaintext_bytes;
}

uint32_t rdplib_usend(connection_t *c, intptr_t socket, iov_t *iov, uint32_t iov_len, struct sockaddr *remote_addr, uint32_t encrypt, uint32_t crc)
{
    (void)c;
    (void)socket;
    (void)remote_addr;
    (void)encrypt;
    (void)crc;
    return capture_backend(TEST_BACKEND_RDPLIB_USEND, iov, iov_len);
}

uint32_t serial_send(serial_t *serial, iov_t *iov, uint32_t iov_len, sockaddr_com *scom)
{
    (void)serial;
    (void)scom;
    return capture_backend(TEST_BACKEND_SERIAL, iov, iov_len);
}

static void initialize_connection(connection_t *c, rdp_t *rdp, uint16_t family)
{
    memset(c, 0, sizeof(*c));
    memset(rdp, 0, sizeof(*rdp));
    c->cn_rdp = rdp;
    store_family((uint8_t *)&c->tx_remote_addr, family);
    c->tx_socket = 19;
    c->trace_socket = 29;
    c->trace_udp_ttl = 64;
    c->tx_acked_thru = 100;
    c->tx_next_msgid = 101;
    c->tx_syn_msgid = 101;
    c->tx_connected = 1;
    c->tx_send_buffer_size = UINT32_MAX;
    c->tx_max_message_age = 10000;
    c->tx_max_service_outage = 10000;
    bitarray_clear(&c->tx_outstanding_packet_mask);
    txq_init(&c->tx_outstanding_packets);
    txq_init(&c->tx_virgin_packets);
    txq_init(&c->tx_delayed_packets);
    assert(tx_create(c) == 0);
    bandwidth_init(&c->tx_bandwidth);
    c->tx_bandwidth.bandwidth = 8000;
    timeout_init(&c->tx_rt_tracker, 100, 1);
    test_locked_connection = c;
}

static msg_outgoing_t *make_message(uint16_t options, uint16_t msgid, const void *data, uint32_t size)
{
    msg_outgoing_t *message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(*message) + size + 10u);

    assert(message != NULL);
    memset(message, 0, sizeof(*message));
    message->options = options;
    message->msgid = msgid;
    msg_outgoing_init(message);
    if (size)
    {
        msg_outgoing_append(message, data, size);
    }
    return message;
}

static void destroy_connection(connection_t *c)
{
    tx_destroy(c);
    assert(c->tx_outstanding_packets.list.size == 0);
    assert(c->tx_virgin_packets.list.size == 0);
    assert(c->tx_delayed_packets.list.size == 0);
    assert(test_fast_live == 0);
    test_locked_connection = NULL;
}

static void test_init_and_create(void)
{
    connection_t c;
    rdp_t rdp;
    uint8_t remote_addr[16] = {0};
    uint16_t family;

    reset_harness();
    memset(&c, 0, sizeof(c));
    memset(&rdp, 0, sizeof(rdp));
    memset((uint8_t *)&c + offsetof(connection_t, tx_socket), 0xA5, offsetof(connection_t, trace_probes) - offsetof(connection_t, tx_socket));
    family = RDP_TRANSMIT_ADDRESS_IPV4;
    memcpy(remote_addr, &family, sizeof(family));
    remote_addr[2] = 0x12;
    remote_addr[3] = 0x34;
    rdp.udp_socket = 11;
    rdp.ipx_socket = 12;
    rdp.trace_socket = 13;
    rdp.udp_socket_ttl = 77;
    tx_init(&c, &rdp, (struct sockaddr *)(void *)remote_addr);

    assert(c.tx_socket == 11);
    assert(memcmp(&c.tx_remote_addr, remote_addr, sizeof(remote_addr)) == 0);
    assert(((uint8_t *)&c.trace_remote_addr)[2] == 0x92 && ((uint8_t *)&c.trace_remote_addr)[3] == 0x34);
    assert(c.trace_socket == 13 && c.trace_udp_ttl == 77);
    assert(c.tx_acked_thru == (uint16_t)(c.tx_next_msgid - 1u));
    assert(c.tx_syn_msgid == c.tx_next_msgid);
    assert(c.tx_next_seqnum == 0 && c.tx_next_fragid == 0);
    assert(c.tx_send_buffer_size == 8000);
    assert(c.tx_connected == 1 && !c.tx_stopped);
    assert(c.tx_max_message_age == 10000 && c.tx_max_service_outage == 10000);
    assert(c.tx_outstanding_packets.queue_size == UINT32_C(0xa5a5a5a5));
    assert(c.tx_virgin_packets.queue_size == UINT32_C(0xa5a5a5a5));
    assert(c.tx_delayed_packets.queue_size == UINT32_C(0xa5a5a5a5));
    assert(memcmp((uint8_t *)&c + offsetof(connection_t, tx_modem) + sizeof(c.tx_modem), (uint8_t[4]){0xA5, 0xA5, 0xA5, 0xA5},
                  offsetof(connection_t, tx_rt_tracker) - offsetof(connection_t, tx_modem) - sizeof(c.tx_modem)) == 0);
    assert(((uint32_t *)&c.tx_bandwidth)[3] == UINT32_C(0xa5a5a5a5));
    assert(tx_create(&c) == 0);
    assert(tx_get_queue_size(&c) == 0);

    memset(&c, 0, sizeof(c));
    family = RDP_TRANSMIT_ADDRESS_IPX;
    memcpy(remote_addr, &family, sizeof(family));
    tx_init(&c, &rdp, (struct sockaddr *)(void *)remote_addr);
    assert(c.tx_socket == 12);
    assert(memcmp(&c.trace_remote_addr, (uint8_t[16]){0}, 16) == 0);

    memset(&c, 0, sizeof(c));
    family = RDP_TRANSMIT_ADDRESS_SERIAL;
    memcpy(remote_addr, &family, sizeof(family));
    tx_init(&c, &rdp, (struct sockaddr *)(void *)remote_addr);
    assert(c.tx_socket == -1);
}

static void test_helpers(void)
{
    connection_t c;
    rdp_t rdp;
    msg_outgoing_t message;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_bandwidth.bandwidth = 800;
    c.tx_bandwidth.queue_time = test_now;
    c.tx_bandwidth.queue_size = 99;
    assert(tx_send_ready(&c) == 1);
    c.tx_bandwidth.queue_size = 100;
    assert(tx_send_ready(&c) == 0);

    store_family((uint8_t *)&c.tx_remote_addr, RDP_TRANSMIT_ADDRESS_SERIAL);
    c.tx_bandwidth.queue_size = 0;
    test_serial_ready = 0;
    assert(tx_send_ready(&c) == 0);
    test_serial_ready = 1;
    assert(tx_send_ready(&c) == 1);

    bitarray_clear(&c.tx_outstanding_packet_mask);
    c.tx_acked_thru = UINT16_C(0xfffe);
    c.tx_next_msgid = UINT16_C(0xffff);
    assert(tx_reserve_msgid(&c) == UINT16_C(0xffff));
    assert(c.tx_next_msgid == 0);
    assert(getbit(c.tx_outstanding_packet_mask.bits, 0));
    assert(c.tx_time_last_guaranteed_send == test_now);
    assert(tx_reserve_msgid(&c) == 0);
    assert(c.tx_next_msgid == 1);
    assert(getbit(c.tx_outstanding_packet_mask.bits, 1));

    memset(&message, 0, sizeof(message));
    message.options = RDP_FLAG_MSGID;
    c.tx_acked_thru = 100;
    message.msgid = 219;
    assert(tx_outgoing_msg_in_outstanding_range(&c, &message) == 1);
    message.msgid = 220;
    assert(tx_outgoing_msg_in_outstanding_range(&c, &message) == 0);
    message.msgid = 100;
    assert(tx_outgoing_msg_in_outstanding_range(&c, &message) == 1);
    c.tx_acked_thru = UINT16_C(0xfff0);
    message.msgid = (uint16_t)(c.tx_acked_thru + 119u);
    assert(tx_outgoing_msg_in_outstanding_range(&c, &message) == 1);
    message.msgid = (uint16_t)(c.tx_acked_thru + 120u);
    assert(tx_outgoing_msg_in_outstanding_range(&c, &message) == 0);

    c.tx_outstanding_packets.queue_size = 3;
    c.tx_virgin_packets.queue_size = 5;
    c.tx_delayed_packets.queue_size = 7;
    assert(tx_get_queue_size(&c) == 15);
    c.tx_connected = 0;
    c.tx_enqueued_disconnect_msg = 0;
    assert(tx_needs_disconnect_msg(&c) == 1);
    c.tx_enqueued_disconnect_msg = 1;
    assert(tx_needs_disconnect_msg(&c) == 0);
    c.tx_connected = 1;
    assert(tx_needs_disconnect_msg(&c) == 0);

    c.tx_outstanding_packets.queue_size = 0;
    c.tx_virgin_packets.queue_size = 0;
    c.tx_delayed_packets.queue_size = 0;
    destroy_connection(&c);
}

static void test_send_ready_virgin_clock_sampling(void)
{
    static const uint8_t data = 1;
    connection_t c;
    rdp_t rdp;
    msg_outgoing_t *msg_virgin;
    msg_outgoing_t *msg_outgoing;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);

    test_time_calls = 0;
    assert(tx_send_ready_virgins(&c) == 0);
    assert(test_time_calls == 0);

    msg_virgin = make_message(0, 0, &data, sizeof(data));
    txq_add_tail(&c.tx_virgin_packets, msg_virgin);
    test_time_calls = 0;
    assert(tx_send_ready_virgins(&c) == 1);
    assert(test_time_calls == 0);

    msg_outgoing = make_message(RDP_FLAG_MSGID, 101, &data, sizeof(data));
    msg_outgoing->time_last_sent = test_now - 50u;
    txq_add_tail(&c.tx_outstanding_packets, msg_outgoing);
    test_time_calls = 0;
    assert(tx_send_ready_virgins(&c) == 1);
    assert(test_time_calls == 1);

    destroy_connection(&c);
}

static void test_queue_admission(void)
{
    static const uint8_t first_data[] = {1, 2, 3};
    static const uint8_t second_data[] = {4, 5};
    connection_t c;
    rdp_t rdp;
    msg_outgoing_t *first;
    msg_outgoing_t *second;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    first = make_message(RDP_FLAG_MSGID, tx_reserve_msgid(&c), first_data, sizeof(first_data));
    tx_enqueue_outgoing(&c, first);
    assert(c.tx_syn_sent == 1);
    assert((first->options & RDP_FLAG_SYN) != 0);
    assert(c.tx_outstanding_packets.list.size == 1);
    assert(test_backend_calls == 1);
    assert((load_network_u16(test_backend_packet) & (RDP_FLAG_SYN | RDP_FLAG_MSGID)) == (RDP_FLAG_SYN | RDP_FLAG_MSGID));
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    first = make_message(0, 0, first_data, sizeof(first_data));
    tx_enqueue_outgoing(&c, first);
    assert(test_backend_calls == 0);
    assert(c.tx_virgin_packets.list.size == 1);
    second = make_message(RDP_FLAG_MSGID, 220, second_data, sizeof(second_data));
    c.tx_syn_sent = 1;
    tx_enqueue_outgoing(&c, second);
    assert(c.tx_delayed_packets.list.size == 1);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    c.tx_syn_acked = 1;
    first = make_message(RDP_FLAG_MSGID, 101, first_data, sizeof(first_data));
    second = make_message(RDP_FLAG_MSGID, 102, second_data, sizeof(second_data));
    txq_add_tail(&c.tx_virgin_packets, first);
    tx_enqueue_outgoing(&c, second);
    assert(test_backend_calls == 1);
    assert(c.tx_outstanding_packets.list.head->item == first);
    assert(c.tx_virgin_packets.list.head->item == second);
    assert(load_network_u16(test_backend_packet + RDP_WIRE_HEADER_BASE_BYTES) == 101);
    assert(memcmp(test_backend_packet + RDP_WIRE_HEADER_BASE_BYTES + 2u, first_data, sizeof(first_data)) == 0);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_SERIAL);
    c.tx_syn_sent = 1;
    c.tx_syn_acked = 1;
    test_serial_ready = 0;
    first = make_message(RDP_FLAG_MSGID, 101, first_data, sizeof(first_data));
    tx_enqueue_outgoing(&c, first);
    assert(c.tx_virgin_packets.list.size == 1 && test_backend_calls == 0);
    test_serial_ready = 1;
    tx_tx(&c);
    assert(test_backend_kind == TEST_BACKEND_SERIAL);
    assert(c.tx_virgin_packets.list.size == 0);
    assert(c.tx_outstanding_packets.list.size == 1);
    destroy_connection(&c);
}

static void test_vector_fragmentation(void)
{
    connection_t c;
    rdp_t rdp;
    uint8_t payload[700];
    iov_t iov[5];
    msg_outgoing_t *first;
    msg_outgoing_t *second;
    uint32_t index;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    c.tx_next_fragid = 7;
    c.tx_guaranteed_stream_seqnum[3] = 9;
    c.tx_delayed_ack = 1;
    c.tx_ack_time = UINT32_C(0x12345678);
    for (index = 0; index < sizeof(payload); ++index)
    {
        payload[index] = (uint8_t)(index * 13u + 7u);
    }
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    // The recovered loop calls memcpy for zero length elements, so keep its test pointers valid.
    iov[0].data = payload;
    iov[2].data = payload + 512;
    iov[4].data = payload;
#else
    iov[0].data = NULL;
    iov[2].data = NULL;
    iov[4].data = NULL;
#endif
    iov[0].size = 0;
    iov[1].data = payload;
    iov[1].size = 512;
    iov[2].size = 0;
    iov[3].data = payload + 512;
    iov[3].size = 188;
    iov[4].size = 0;

    assert(connection_sendv(&c, iov, 5, 3, RDP_SEND_RELIABLE) == RDP_CONNECTION_SEND_OK);
    assert(test_lock_calls == 1 && test_unlock_calls == 1);
    assert(test_resort_calls == 1 && test_resort_wake == 1);
    assert(c.tx_next_msgid == 103);
    assert(c.tx_next_fragid == 8);
    assert(c.tx_guaranteed_stream_seqnum[3] == 10);
    assert(c.tx_delayed_ack == 1 && c.tx_ack_time == UINT32_C(0x12345678));
    assert(c.tx_virgin_packets.list.size == 2);
    first = txq_remove_head(&c.tx_virgin_packets);
    second = txq_remove_head(&c.tx_virgin_packets);
    assert(first->msgid == 101 && second->msgid == 102);
    assert(first->fragid == 7 && second->fragid == 7);
    assert(first->frag_number == 0 && second->frag_number == 1);
    assert(first->frag_total == 2 && second->frag_total == 2);
    assert(first->stream == 3 && second->stream == 3);
    assert(first->stream_seqnum == 9 && second->stream_seqnum == 10);
    assert(first->txq_link.item == first && second->txq_link.item == second);
    assert((first->options & (RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT | RDP_FLAG_SEQUENCED)) == (RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT | RDP_FLAG_SEQUENCED));
    assert((second->options & RDP_FLAG_SEQUENCED) == 0);
    assert(first->size == 522 && second->size == 196);
    assert(memcmp(msg_outgoing_get_data(first) + 10, payload, 512) == 0);
    assert(memcmp(msg_outgoing_get_data(second) + 8, payload + 512, 188) == 0);
    fast_free(first);
    fast_free(second);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    c.tx_delayed_ack = 1;
    c.tx_ack_time = UINT32_C(0x87654321);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    iov[0].data = payload;
    iov[2].data = payload + 3;
#else
    iov[0].data = NULL;
    iov[2].data = NULL;
#endif
    iov[0].size = 0;
    iov[1].data = payload;
    iov[1].size = 3;
    iov[2].size = 0;
    assert(connection_sendv(&c, iov, 3, 0, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_OK);
    assert(c.tx_next_msgid == 101 && c.tx_next_fragid == 0);
    assert(c.tx_delayed_ack == 1 && c.tx_ack_time == UINT32_C(0x87654321));
    assert(c.tx_virgin_packets.list.size == 1);
    first = txq_remove_head(&c.tx_virgin_packets);
    assert(first->txq_link.item == first);
    assert(first->options == 0 && first->size == 3);
    assert(memcmp(msg_outgoing_get_data(first), payload, 3) == 0);
    fast_free(first);
    destroy_connection(&c);
}

static void test_checked_and_faithful_edges(void)
{
    connection_t c;
    rdp_t rdp;
    uint8_t payload[1024] = {0};
    iov_t iov = {payload, sizeof(payload)};
    uint16_t next_msgid;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    test_fast_fail_at = 2;
    next_msgid = c.tx_next_msgid;
    assert(connection_sendv(&c, &iov, 1, 1, RDP_SEND_RELIABLE) == RDP_CONNECTION_SEND_ALLOCATION_FAILED);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(c.tx_next_msgid == (uint16_t)(next_msgid + 1u));
    assert(c.tx_virgin_packets.list.size == 1);
    assert(getbit(c.tx_outstanding_packet_mask.bits, 0));
#else
    assert(c.tx_next_msgid == next_msgid);
    assert(c.tx_virgin_packets.list.size == 0);
    assert(!getbit(c.tx_outstanding_packet_mask.bits, 0));
    assert(test_fast_live == 0);
#endif
    test_fast_fail_at = 0;
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    iov.size = 1;
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(connection_sendv(&c, &iov, 1, 255, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_OK);
    assert(((msg_outgoing_t *)c.tx_virgin_packets.list.head->item)->stream == 255);
#else
    assert(connection_sendv(&c, &iov, 1, 20, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_INVALID_ARGUMENT);
    test_fast_fail_at = 1;
    assert(connection_sendv(&c, &iov, 1, 0, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_ALLOCATION_FAILED);
    assert(test_fast_live == 0);
#endif
    test_fast_fail_at = 0;
    destroy_connection(&c);
}

static void test_send_boundaries(void)
{
    connection_t c;
    rdp_t rdp;
    static uint8_t payload[51201];
    iov_t iov;
    msg_outgoing_t *message;

    iov.data = payload;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    iov.size = 512;
    assert(connection_sendv(&c, &iov, 1, 0, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_OK);
    assert(c.tx_virgin_packets.list.size == 1);
    message = txq_remove_head(&c.tx_virgin_packets);
    assert(message->size == 512);
    fast_free(message);
    iov.size = 513;
    assert(connection_sendv(&c, &iov, 1, 0, RDP_SEND_UNRELIABLE) == RDP_SEND_ERROR_TOO_BIG);
    assert(c.tx_virgin_packets.list.size == 0);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    iov.size = 51200;
    assert(connection_sendv(&c, &iov, 1, 0, RDP_SEND_RELIABLE) == RDP_CONNECTION_SEND_OK);
    assert(c.tx_virgin_packets.list.size == 100);
    assert(c.tx_next_msgid == 201);
    iov.size = 51201;
    assert(connection_sendv(&c, &iov, 1, 0, RDP_SEND_RELIABLE) == RDP_SEND_ERROR_TOO_BIG);
    assert(c.tx_virgin_packets.list.size == 100);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    iov.size = 1;
    assert(connection_sendv(&c, &iov, 1, 19, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_OK);
    message = txq_remove_head(&c.tx_virgin_packets);
    assert(message->stream == 19);
    fast_free(message);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(connection_sendv(&c, &iov, 1, 20, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_OK);
    message = txq_remove_head(&c.tx_virgin_packets);
    assert(message->stream == 20);
    fast_free(message);
#else
    assert(connection_sendv(&c, &iov, 1, 20, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_INVALID_ARGUMENT);
#endif
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    iov.size = 1;
    c.tx_send_buffer_size = 1;
    assert(connection_sendv(&c, &iov, 1, 0, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_OK);
    assert(tx_get_queue_size(&c) == 1);
    assert(connection_sendv(&c, &iov, 1, 0, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_OK);
    assert(tx_get_queue_size(&c) == 2);
    assert(connection_sendv(&c, &iov, 1, 0, RDP_SEND_UNRELIABLE) == RDP_CONNECTION_SEND_BUFFER_FULL);
    assert(tx_get_queue_size(&c) == 2);
    destroy_connection(&c);
}

static void test_send_packet_results(void)
{
    connection_t c;
    rdp_t rdp;
    msg_outgoing_t *first;
    static const uint8_t payload[] = {0xaa, 0xbb, 0xcc};
    uint16_t flags;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_next_seqnum = 77;
    c.cn_closed = 1;
    test_ack_enabled = 1;
    test_ack_msgid = UINT16_C(0x2345);
    c.rx_msgid_count = 1;
    assert(tx_send_packet(&c, (char *)(void *)payload, sizeof(payload), RDP_FLAG_MSGID) == 0);
    assert(c.tx_next_seqnum == 78);
    assert(test_backend_packet_size == RDP_WIRE_HEADER_BASE_BYTES + 2u + sizeof(payload));
    flags = load_network_u16(test_backend_packet);
    assert((flags & (RDP_FLAG_STOP | RDP_FLAG_ACKTHRU | RDP_FLAG_MSGID)) == (RDP_FLAG_STOP | RDP_FLAG_ACKTHRU | RDP_FLAG_MSGID));
    assert(test_statistics.ack_and_data_packets_tx == 1);
    assert(c.stat.ack_and_data_packets_tx == 1);
    assert(memcmp(test_backend_packet + RDP_WIRE_HEADER_BASE_BYTES + 2u, payload, sizeof(payload)) == 0);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_connected = 0;
    c.cn_closed = 1;
    assert(tx_send_packet(&c, NULL, 0, 0) == 0);
    flags = load_network_u16(test_backend_packet);
    assert((flags & RDP_FLAG_RESET) != 0 && (flags & RDP_FLAG_STOP) == 0);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_acked = 1;
    c.tx_bandwidth.bandwidth = 1000;
    c.tx_bandwidth.queue_size = 0;
    c.tx_bandwidth.queue_time = test_now;
    test_backend_result = 5;
    assert(tx_send_packet(&c, NULL, 0, 0) == 5);
    assert(c.tx_next_seqnum == 0);
    assert(c.tx_bandwidth.queue_size == 100);
    destroy_connection(&c);

#ifndef RDPLIB_DEBUG
    // The recovered UDP debug contract accepts only backend results 0 and 5;
    // result 1 is a maintained backend extension covered outside RDPLIB_DEBUG.
    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_acked = 1;
    c.tx_all_acked = &(uint32_t){1};
    c.tx_all_acked_event = (uevent_t *)(uintptr_t)1;
    test_backend_result = 1;
    assert(tx_send_packet(&c, NULL, 0, 0) == 1);
    assert(!c.tx_connected && c.tx_stopped);
    assert(c.tx_disconnect_reason == RDP_DISCONNECT_REASON_SEND_ERROR);
    assert(test_rx_flush_calls == 1 && test_event_signal_calls == 1);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    first = make_message(RDP_FLAG_MSGID, 101, payload, sizeof(payload));
    test_backend_result = 1;
    tx_send_virgin(&c, first);
    assert(!c.tx_connected && c.tx_stopped);
    assert(c.tx_disconnect_reason == RDP_DISCONNECT_REASON_SEND_ERROR);
    assert(c.tx_outstanding_packets.list.size == 1);
    assert(txq_peek_head(&c.tx_outstanding_packets) == first);
    assert(first->attempts == 1 && first->time_first_sent == test_now && first->time_last_sent == test_now);
    assert(test_statistics.guaranteed_packets_tx == 1);
    assert(c.stat.guaranteed_packets_tx == 1);
    destroy_connection(&c);
#else
    (void)first;
#endif
}

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
static void prepare_pending_ack(connection_t *c, uint32_t mask_bytes)
{
    test_ack_enabled = 1;
    test_ack_msgid = UINT16_C(0x2345);
    test_ack_mask_bytes = mask_bytes;
    c->rx_msgid_count = 1;
    c->rx_msgid_lo = test_ack_msgid;
    c->rx_msgid_hi = (uint16_t)(test_ack_msgid + mask_bytes * 8u);
    c->tx_delayed_ack = 1;
    c->tx_ack_time = test_now + 50u;
}

static void test_piggyback_ack_datagram_limit(void)
{
    static char fragment_zero[522];
    const uint16_t data_flags = RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT | RDP_FLAG_SEQUENCED;
    connection_t c;
    rdp_t rdp;
    uint16_t flags;

    // A four-byte mask produces exactly 536 CRC-framed bytes and remains
    // piggybacked on fragment zero.
    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    rdp.crc = 1;
    c.tx_next_seqnum = 77;
    prepare_pending_ack(&c, 4);
    assert(tx_send_packet(&c, fragment_zero, sizeof(fragment_zero), data_flags) == 0);
    assert(test_backend_calls == 1);
    assert(rdplib_usend_framed_size(test_backend_packet_sizes[0], 0, 1) == RDP_LEGACY_DATAGRAM_BYTES);
    flags = load_network_u16(test_backend_packets[0]);
    assert((flags & data_flags) == data_flags);
    assert((flags & RDP_FLAG_ACKTHRU) != 0);
    assert((flags & RDP_FLAG_ACK_MASK_LENGTH) == 4u << 4);
    assert(load_network_u16(test_backend_packets[0] + 2) == 77);
    assert(c.tx_next_seqnum == 78);
    assert(test_statistics.ack_and_data_packets_tx == 1);
    assert(test_statistics.ack_only_packets_tx == 0);
    destroy_connection(&c);

    // Adding the fifth mask byte would produce 537 bytes. The data is sent
    // without ACK fields, followed by a header-only ACK datagram.
    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    rdp.crc = 1;
    c.tx_next_seqnum = 77;
    prepare_pending_ack(&c, 5);
    assert(tx_send_packet(&c, fragment_zero, sizeof(fragment_zero), data_flags) == 0);
    assert(test_backend_calls == 2);
    assert(test_backend_packet_sizes[0] == RDP_WIRE_HEADER_BASE_BYTES + sizeof(fragment_zero));
    assert(test_backend_packet_sizes[1] == RDP_WIRE_HEADER_BASE_BYTES + 2u + 5u);
    assert(rdplib_usend_framed_size(test_backend_packet_sizes[0], 0, 1) <= RDP_LEGACY_DATAGRAM_BYTES);
    assert(rdplib_usend_framed_size(test_backend_packet_sizes[1], 0, 1) <= RDP_LEGACY_DATAGRAM_BYTES);

    flags = load_network_u16(test_backend_packets[0]);
    assert((flags & data_flags) == data_flags);
    assert((flags & (RDP_FLAG_ACKTHRU | RDP_FLAG_MASKOFFSET | RDP_FLAG_ACK_MASK_LENGTH)) == 0);
    assert(load_network_u16(test_backend_packets[0] + 2) == 77);

    flags = load_network_u16(test_backend_packets[1]);
    assert((flags & (RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT | RDP_FLAG_SEQUENCED)) == 0);
    assert((flags & RDP_FLAG_ACKTHRU) != 0);
    assert((flags & RDP_FLAG_ACK_MASK_LENGTH) == 5u << 4);
    assert(load_network_u16(test_backend_packets[1] + 2) == 78);
    assert(c.tx_next_seqnum == 79);
    assert(c.rx_msgid_count == 0 && c.tx_delayed_ack == 0);
    assert(test_statistics.ack_and_data_packets_tx == 0);
    assert(test_statistics.ack_only_packets_tx == 1);
    assert(test_statistics.sendto_calls == 2);
    assert(c.tx_bandwidth.queue_size ==
           RDP_WIRE_HEADER_BASE_BYTES + sizeof(fragment_zero) + 28u +
           RDP_WIRE_HEADER_BASE_BYTES + 2u + 5u + 28u);
    destroy_connection(&c);

    // If only the follow-up ACK meets backpressure, the data sequence remains
    // committed and the complete ACK report remains pending for retry.
    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    rdp.crc = 1;
    c.tx_next_seqnum = 77;
    prepare_pending_ack(&c, 5);
    test_backend_result_count = 2;
    test_backend_results[0] = 0;
    test_backend_results[1] = 5;
    assert(tx_send_packet(&c, fragment_zero, sizeof(fragment_zero), data_flags) == 0);
    assert(test_backend_calls == 2);
    assert(c.tx_next_seqnum == 78);
    assert(c.rx_msgid_count == 1 && c.tx_delayed_ack == 1);
    assert(load_network_u16(test_backend_packets[1] + 2) == 78);

    test_backend_result_count = 0;
    assert(tx_send_packet(&c, NULL, 0, 0) == 0);
    assert(test_backend_calls == 3);
    assert(load_network_u16(test_backend_packets[2] + 2) == 78);
    assert(c.tx_next_seqnum == 79);
    assert(c.rx_msgid_count == 0 && c.tx_delayed_ack == 0);
    destroy_connection(&c);
}
#endif

#ifdef RDPLIB_DEBUG
static void test_queue_delay_statistics(void)
{
    connection_t c;
    rdp_t rdp;
    msg_outgoing_t *message;
    uint32_t second_size;
    static const uint8_t payload[] = {0x41, 0x42, 0x43};

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    c.tx_syn_acked = 1;
    c.stat.tqd_last_interval = test_now;

    message = make_message(0, 0, payload, sizeof(payload));
    assert(message->enqueue_time == test_now);
    test_now += 7u;
    tx_send_virgin(&c, message);
    assert(c.stat.tqd_samples == 1);
    assert(c.stat.tqd_min == 7 && c.stat.tqd_max == 7 && c.stat.tqd_sum == 7);
    assert(c.stat.tqd_bytes == 0); // The recovered first sample branch does not record bytes.

    message = make_message(0, 0, payload, sizeof(payload));
    second_size = message->size;
    assert(message->enqueue_time == test_now);
    test_now += 13u;
    tx_send_virgin(&c, message);
    assert(c.stat.tqd_samples == 2);
    assert(c.stat.tqd_min == 7 && c.stat.tqd_max == 13 && c.stat.tqd_sum == 20);
    assert(c.stat.tqd_bytes == second_size);

    c.stat.tqd_last_interval = test_now - 10001u;
    message = make_message(0, 0, payload, sizeof(payload));
    assert(message->enqueue_time == test_now);
    tx_send_virgin(&c, message);
    assert(c.stat.tqd_last_interval == test_now);
    assert(c.stat.tqd_samples == 0 && c.stat.tqd_min == 0 && c.stat.tqd_max == 0 && c.stat.tqd_sum == 0 && c.stat.tqd_bytes == 0);
    destroy_connection(&c);
}
#endif

static void test_retransmission_rotation(void)
{
    connection_t c;
    rdp_t rdp;
    msg_outgoing_t *in_range;
    msg_outgoing_t *out_of_range;
    static const uint8_t first_data = 0x41;
    static const uint8_t second_data = 0x42;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    c.tx_syn_acked = 1;
    c.rx_time_last_arrival = test_now;

    in_range = make_message(RDP_FLAG_MSGID, 101, &first_data, 1);
    in_range->time_first_sent = test_now - 200u;
    in_range->time_last_sent = test_now - 100u;
    in_range->attempts = 1;
    out_of_range = make_message(RDP_FLAG_MSGID, 220, &second_data, 1);
    out_of_range->time_first_sent = test_now - 200u;
    out_of_range->time_last_sent = test_now - 100u;
    out_of_range->attempts = 1;
    txq_add_tail(&c.tx_outstanding_packets, in_range);
    txq_add_tail(&c.tx_outstanding_packets, out_of_range);

    tx_tx(&c);
    assert(test_backend_calls == 1);
    assert(test_backend_packet_size == RDP_WIRE_HEADER_BASE_BYTES + 3u);
    assert(load_network_u16(test_backend_packet + RDP_WIRE_HEADER_BASE_BYTES) == 101);
    assert(test_backend_packet[RDP_WIRE_HEADER_BASE_BYTES + 2u] == first_data);
    assert(in_range->time_last_sent == test_now && in_range->attempts == 2);
    assert(txq_peek_head(&c.tx_outstanding_packets) == out_of_range);
    assert(c.tx_outstanding_packets.list.tail->item == in_range);
    assert(test_statistics.guaranteed_packets_retx == 1 && test_statistics.guaranteed_bytes_retx == 3);
    assert(c.stat.guaranteed_packets_retx == 1 && c.stat.guaranteed_bytes_retx == 3);

#ifndef RDPLIB_DEBUG
    // The historical out of window branch begins with assert(0) in debug;
    // its retail stamp and rotate behavior is covered outside RDPLIB_DEBUG.
    tx_tx(&c);
    assert(test_backend_calls == 1);
    assert(out_of_range->time_last_sent == test_now && out_of_range->attempts == 2);
    assert(txq_peek_head(&c.tx_outstanding_packets) == in_range);
    assert(c.tx_outstanding_packets.list.tail->item == out_of_range);
    assert(test_statistics.guaranteed_packets_retx == 1 && test_statistics.guaranteed_bytes_retx == 3);
    assert(c.stat.guaranteed_packets_retx == 1 && c.stat.guaranteed_bytes_retx == 3);
#endif
    destroy_connection(&c);
}

static void test_event_scheduler_and_close(void)
{
    connection_t c;
    rdp_t rdp;
    timeout_data timeout;
    msg_outgoing_t *first;
    msg_outgoing_t *second;
    uint32_t all_acked;
    static const uint8_t data = 7;

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_bandwidth.bandwidth = 0;
    timeout.infinite = 0;
    timeout.time = UINT32_MAX;
    tx_get_event_time(&c, &timeout);
    assert(timeout.infinite == 1 && timeout.time == 0);
    assert(test_backend_calls == 0);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    tx_set_delayed_ack(&c);
    assert(c.tx_delayed_ack == 1 && c.tx_ack_time == test_now + 50u);
    test_now += 25u;
    tx_set_delayed_ack(&c);
    assert(c.tx_ack_time == test_now + 25u);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    first = make_message(0, 0, &data, 1);
    txq_add_tail(&c.tx_virgin_packets, first);
    tx_get_event_time(&c, &timeout);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(!timeout.infinite);
#else
    assert(timeout.infinite);
#endif
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    c.tx_syn_acked = 1;
    first = make_message(RDP_FLAG_MSGID, 101, &data, 1);
    second = make_message(RDP_FLAG_MSGID, 102, &data, 1);
    txq_add_tail(&c.tx_virgin_packets, first);
    txq_add_tail(&c.tx_virgin_packets, second);
    tx_tx(&c);
    assert(test_backend_calls == 1);
    assert(c.tx_outstanding_packets.list.size == 1);
    assert(c.tx_virgin_packets.list.size == 1);
    destroy_connection(&c);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    c.tx_syn_sent = 1;
    c.tx_syn_acked = 1;
    c.tx_bandwidth.queue_size = c.tx_bandwidth.bandwidth >> 3;
    c.tx_bandwidth.queue_time = test_now;
    assert(tx_send_fin(&c) == RDP_CONNECTION_SEND_OK);
    assert(c.tx_fin_sent && c.tx_fin_msgid == 101);
    assert(c.tx_virgin_packets.list.size == 1);
    all_acked = 0;
    c.tx_all_acked = &all_acked;
    c.tx_all_acked_event = (uevent_t *)(uintptr_t)2;
    tx_received_stopped(&c);
    assert(all_acked == 1 && c.tx_stopped);
    assert(test_event_signal_calls == 1 && test_last_event == (uevent_t *)(uintptr_t)2);
    assert(test_fast_live == 0);

    reset_harness();
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    all_acked = 1;
    c.tx_all_acked = &all_acked;
    c.tx_all_acked_event = (uevent_t *)(uintptr_t)3;
    destroy_connection(&c);
    assert(all_acked == 0 && test_event_signal_calls == 1);
}

static void test_trace_flow(void)
{
    connection_t c;
    rdp_t rdp;
    trace_probe_t *probes;

    reset_harness();
    assert(trace_start(NULL) == RDP_CONNECTION_SEND_INVALID_ARGUMENT);
    initialize_connection(&c, &rdp, RDP_TRANSMIT_ADDRESS_IPV4);
    rdp.trace_socket = -1;
    assert(trace_start(&c) == 8);
    rdp.trace_socket = 41;
    c.trace_socket = 41;
    c.tx_max_service_outage = 10000;
    assert(trace_start(&c) == 0);
    assert(c.trace_probes != NULL);
    assert(c.trace_next_ttl == 1 && c.trace_max_ttl == 30);
    assert(c.trace_next_index == 0 && c.trace_pass == 0);
    assert(c.trace_clock == UINT32_C(0x12345678));
    assert(trace_start(&c) == 9);

    probes = c.trace_probes;
    test_now += 10001;
    c.trace_max_ttl = 2;
    test_socket_result_count = 2;
    test_datagram_result = 1;
    assert(trace_send(&c) == 0);
    assert(test_socket_calls == 2 && test_socket_values[0] == 1 && test_socket_values[1] == c.trace_udp_ttl);
    assert(test_datagram_calls == 1 && test_datagram_byte == 0);
    assert(probes[0].time_sent == test_now && probes[0].ttl == 1);
    assert(c.trace_next_index == 1 && c.trace_next_ttl == 2 && c.trace_en_route == 1);

#ifndef RDPLIB_DEBUG
    // Recovered debug assertions require both socket operations and sendto to
    // succeed. Retail error handling and its orphan format calls are covered
    // by the normal and source faithful runs.
    test_socket_calls = 0;
    test_datagram_result = 0;
    assert(trace_send(&c) == 0);
    assert(c.trace_next_index == 1);
    assert(memcmp(&probes[1], &(trace_probe_t){0}, sizeof(probes[1])) == 0);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(test_last_socket_error_calls == 1);
#else
    assert(test_last_socket_error_calls == 0);
#endif

    test_socket_calls = 0;
    test_socket_result_count = 2;
    test_socket_results[0] = 0;
    test_socket_results[1] = -1;
    test_datagram_result = 1;
    assert(trace_send(&c) == UINT32_MAX);
    assert(c.trace_next_index == 2);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(test_last_socket_error_calls == 2);
#else
    assert(test_last_socket_error_calls == 0);
#endif

    test_socket_calls = 0;
    test_socket_result_count = 1;
    test_socket_results[0] = -1;
    assert(trace_send(&c) == UINT32_MAX);
    assert(c.trace_pass == 3);
#ifdef RDPLIB_SOURCE_FAITHFUL
    assert(test_last_socket_error_calls == 3);
#else
    assert(test_last_socket_error_calls == 0);
#endif
#endif

    rdplib_platform_free(c.trace_probes);
    c.trace_probes = NULL;
    destroy_connection(&c);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_init_and_create();
    test_helpers();
    test_send_ready_virgin_clock_sampling();
    test_queue_admission();
    test_vector_fragmentation();
    test_checked_and_faithful_edges();
    test_send_boundaries();
    test_send_packet_results();
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    test_piggyback_ack_datagram_limit();
#endif
#ifdef RDPLIB_DEBUG
    test_queue_delay_statistics();
#endif
    test_retransmission_rotation();
    test_event_scheduler_and_close();
    test_trace_flow();
    g_rdp_stat = NULL;
    puts("tx_core: ok");
    return 0;
}
