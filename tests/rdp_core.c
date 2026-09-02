// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "connection.h"
#include "crc.h"
#include "cypher.h"
#include "eventq.h"
#include "fast.h"
#include "rdp.h"
#include "rdplib_rdp.h"
#include "rdpstat.h"
#include "rx.h"
#include "usend.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

_Static_assert(offsetof(uthread_t, handle) == 0, "uthread_t must begin with handle");
_Static_assert(offsetof(uthread_t, id) >= sizeof(void *), "uthread_t::id moved before handle");
_Static_assert(offsetof(uthread_t, proc) > offsetof(uthread_t, id), "uthread_t::proc moved");
_Static_assert(offsetof(uthread_t, data) > offsetof(uthread_t, proc), "uthread_t::data moved");
_Static_assert(sizeof(struct sockaddr_ipx) == 14, "sockaddr_ipx recovered size");

_Static_assert(offsetof(rdp_t, startup) == 0, "rdp_t must begin with startup");
_Static_assert(offsetof(rdp_t, local_udp_addr) > offsetof(rdp_t, ipx_socket), "rdp_t socket/address order changed");
_Static_assert(offsetof(rdp_t, addr_map) > offsetof(rdp_t, udp_broadcast), "rdp_t owner-container order changed");
_Static_assert(offsetof(rdp_t, io_thread) > offsetof(rdp_t, io_thread_running), "rdp_t thread order changed");
_Static_assert(offsetof(rdp_t, message_rxq) > offsetof(rdp_t, receive_semaphore), "rdp_t receive queue order changed");
_Static_assert(offsetof(rdp_t, external_rxq) > offsetof(rdp_t, message_rxq_mutex), "rdp_t external queue order changed");
_Static_assert(offsetof(rdp_t, serial) > offsetof(rdp_t, crc), "rdp_t serial tail changed");

#if defined(_WIN32) && UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(uthread_t) == 0x10, "uthread_t x86 size");
_Static_assert(offsetof(uthread_t, handle) == 0x00, "uthread_t::handle x86 offset");
_Static_assert(offsetof(uthread_t, id) == 0x04, "uthread_t::id x86 offset");
_Static_assert(offsetof(uthread_t, proc) == 0x08, "uthread_t::proc x86 offset");
_Static_assert(offsetof(uthread_t, data) == 0x0c, "uthread_t::data x86 offset");

_Static_assert(sizeof(rdp_t) == 0x1ac + 4u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t x86 size");
_Static_assert(offsetof(rdp_t, startup) == 0x000, "rdp_t::startup x86 offset");
_Static_assert(offsetof(rdp_t, udp_socket) == 0x004, "rdp_t::udp_socket x86 offset");
_Static_assert(offsetof(rdp_t, icmp_socket) == 0x008, "rdp_t::icmp_socket x86 offset");
_Static_assert(offsetof(rdp_t, trace_socket) == 0x00c, "rdp_t::trace_socket x86 offset");
_Static_assert(offsetof(rdp_t, ipx_socket) == 0x010, "rdp_t::ipx_socket x86 offset");
_Static_assert(offsetof(rdp_t, local_udp_addr) == 0x014, "rdp_t::local_udp_addr x86 offset");
_Static_assert(offsetof(rdp_t, trace_local_addr) == 0x024, "rdp_t::trace_local_addr x86 offset");
_Static_assert(offsetof(rdp_t, local_ipx_addr) == 0x034, "rdp_t::local_ipx_addr x86 offset");
_Static_assert(offsetof(rdp_t, udp_socket_ttl) == 0x044, "rdp_t::udp_socket_ttl x86 offset");
_Static_assert(offsetof(rdp_t, ipx_broadcast) == 0x048, "rdp_t::ipx_broadcast x86 offset");
_Static_assert(offsetof(rdp_t, udp_broadcast) == 0x04c, "rdp_t::udp_broadcast x86 offset");
_Static_assert(offsetof(rdp_t, addr_map) == 0x050, "rdp_t::addr_map x86 offset");
_Static_assert(offsetof(rdp_t, conn_eventq) == 0x058, "rdp_t::conn_eventq x86 offset");
_Static_assert(offsetof(rdp_t, wake_sent) == 0x084 + RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::wake_sent x86 offset");
_Static_assert(offsetof(rdp_t, app_is_waiting_for_exit) == 0x088 + RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::app_is_waiting_for_exit x86 offset");
_Static_assert(offsetof(rdp_t, io_thread_running) == 0x08c + RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::io_thread_running x86 offset");
_Static_assert(offsetof(rdp_t, io_thread) == 0x090 + RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::io_thread x86 offset");
_Static_assert(offsetof(rdp_t, receive_semaphore) == 0x0a0 + RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::receive_semaphore x86 offset");
_Static_assert(offsetof(rdp_t, message_rxq) == 0x0a4 + RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::message_rxq x86 offset");
_Static_assert(offsetof(rdp_t, message_rxq_mutex) == 0x0b8 + RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::message_rxq_mutex x86 offset");
_Static_assert(offsetof(rdp_t, external_rxq) == 0x0d0 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::external_rxq x86 offset");
_Static_assert(offsetof(rdp_t, external_rxq_mutex) == 0x0e4 + 2u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::external_rxq_mutex x86 offset");
_Static_assert(offsetof(rdp_t, bytes_recvd) == 0x0fc + 3u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::bytes_recvd x86 offset");
_Static_assert(offsetof(rdp_t, duplicate_bytes_recvd) == 0x100 + 3u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::duplicate_bytes_recvd x86 offset");
_Static_assert(offsetof(rdp_t, last_sample) == 0x104 + 3u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::last_sample x86 offset");
_Static_assert(offsetof(rdp_t, bytes_per_second) == 0x108 + 3u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::bytes_per_second x86 offset");
_Static_assert(offsetof(rdp_t, duplicate_bytes_per_second) == 0x10c + 3u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::duplicate_bytes_per_second x86 offset");
_Static_assert(offsetof(rdp_t, encrypt) == 0x110 + 3u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::encrypt x86 offset");
_Static_assert(offsetof(rdp_t, crc) == 0x114 + 3u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::crc x86 offset");
_Static_assert(offsetof(rdp_t, serial) == 0x118 + 3u * RDP_WIN32_UMUTEX_OWNER_BYTES, "rdp_t::serial x86 offset");
#endif

_Static_assert(_Generic(&g_next_local_port, uint16_t *: 1, default: 0), "g_next_local_port type");
_Static_assert(_Generic(&rdp_wake, uint32_t (*)(rdp_t *, uint32_t): 1, default: 0), "rdp_wake signature");
_Static_assert(_Generic(&rdp_resort, void (*)(connection_t *, uint32_t): 1, default: 0), "rdp_resort signature");
_Static_assert(_Generic(&rdp_unlock, void (*)(connection_t *): 1, default: 0), "rdp_unlock signature");
_Static_assert(_Generic(&rdp_enqueue_arrival, void (*)(rdp_t *, msg_arrival_t *): 1, default: 0), "rdp_enqueue_arrival signature");
_Static_assert(_Generic(&rdp_handle_complete_arrival, void (*)(rdp_t *, connection_t *, msg_arrival_t *): 1, default: 0), "rdp_handle_complete_arrival signature");
_Static_assert(_Generic(&rdp_handle_connectionless, void (*)(rdp_t *, const char *, uint32_t, struct sockaddr *): 1, default: 0),
               "rdp_handle_connectionless signature");
_Static_assert(_Generic(&rdp_verify_crc, int32_t (*)(char *, int32_t): 1, default: 0), "rdp_verify_crc signature");
_Static_assert(_Generic(&rdp_decode_data, int32_t (*)(char *, int32_t): 1, default: 0), "rdp_decode_data signature");
_Static_assert(_Generic(&rdp_handle_data_recv, uint32_t (*)(rdp_t *, char *, int32_t, struct sockaddr *): 1, default: 0), "rdp_handle_data_recv signature");
_Static_assert(_Generic(&rdp_handle_icmp_recv, void (*)(rdp_t *, char *, int32_t, struct sockaddr_in *): 1, default: 0), "rdp_handle_icmp_recv signature");
_Static_assert(_Generic(&rdp_serial_drain, void (*)(rdp_t *): 1, default: 0), "rdp_serial_drain signature");
_Static_assert(_Generic(&rdp_destroy_internal, void (*)(rdp_t *): 1, default: 0), "rdp_destroy_internal signature");
_Static_assert(_Generic(&rdp_io_thread, void (*)(void *): 1, default: 0), "rdp_io_thread signature");
_Static_assert(_Generic(&rdp_enqueue_disconnect_msg, void (*)(rdp_t *, connection_t *): 1, default: 0), "rdp_enqueue_disconnect_msg signature");
_Static_assert(_Generic(&rdp_init, void (*)(rdp_t *): 1, default: 0), "rdp_init signature");
_Static_assert(_Generic(&rdp_create, uint32_t (*)(rdp_t **, uint16_t, uint32_t, uint32_t): 1, default: 0), "rdp_create signature");
_Static_assert(_Generic(&rdp_destroy, void (*)(rdp_t *, int): 1, default: 0), "rdp_destroy signature");
_Static_assert(_Generic(&rdp_connect_sa, uint32_t (*)(rdp_t *, connection_t **, struct sockaddr *, uint32_t): 1, default: 0), "rdp_connect_sa signature");
#if defined(RDPLIB_DEBUG) || defined(RDPLIB_SOURCE_FAITHFUL)
_Static_assert(_Generic(&format_sockaddr, void (*)(char *, struct sockaddr *): 1, default: 0), "format_sockaddr signature");
#endif
_Static_assert(_Generic(&rdp_connection_create_internal, uint32_t (*)(rdp_t *, connection_t **, struct sockaddr *, uint32_t): 1, default: 0),
               "rdp_connection_create_internal signature");
_Static_assert(_Generic(&rdp_connect, uint32_t (*)(rdp_t *, connection_t **, char *, uint16_t, uint32_t): 1, default: 0), "rdp_connect signature");
_Static_assert(_Generic(&rdp_connection_mark_for_delete, void (*)(rdp_t *, connection_t *): 1, default: 0), "rdp_connection_mark_for_delete signature");
_Static_assert(_Generic(&connection_close_wait, uint32_t (*)(connection_t *, uint32_t, uint32_t *): 1, default: 0), "connection_close_wait signature");
_Static_assert(_Generic(&rdp_receive, msg_arrival_t *(*)(rdp_t *, uint32_t): 1, default: 0), "rdp_receive signature");
_Static_assert(_Generic(&rdplib_rdp_get_input_rate, void (*)(rdp_t *, rdplib_rdp_input_rate_t *): 1, default: 0), "rdplib_rdp_get_input_rate signature");
_Static_assert(_Generic(&rdp_get_input_rate, uint32_t (*)(rdp_t *): 1, default: 0), "rdp_get_input_rate signature");
_Static_assert(_Generic(&rdp_serial_tx_ready, uint32_t (*)(rdp_t *): 1, default: 0), "rdp_serial_tx_ready signature");
_Static_assert(_Generic(&rdp_serial_get_time_empty, uint32_t (*)(rdp_t *): 1, default: 0), "rdp_serial_get_time_empty signature");
_Static_assert(_Generic(&rdp_get_serial_stall_time, uint32_t (*)(rdp_t *): 1, default: 0), "rdp_get_serial_stall_time signature");
_Static_assert(_Generic(&rdp_lock_addr, connection_t *(*)(rdp_t *, struct sockaddr *): 1, default: 0), "rdp_lock_addr signature");
_Static_assert(_Generic(&rdp_lock_connection, connection_t *(*)(connection_t *): 1, default: 0), "rdp_lock_connection signature");
_Static_assert(_Generic(&usemaphore_decrement, uint32_t (*)(usemaphore_t *, uint32_t): 1, default: 0), "semaphore timeout must retain its full uint32_t range");

#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&rdp_set_socket_rcvbuf, void (*)(rdp_t *, int): 1, default: 0), "rdp_set_socket_rcvbuf signature");
_Static_assert(_Generic(&rdp_set_socket_sndbuf, void (*)(rdp_t *, int): 1, default: 0), "rdp_set_socket_sndbuf signature");
_Static_assert(_Generic(&rdp_get_socket_sndbuf, int (*)(rdp_t *): 1, default: 0), "rdp_get_socket_sndbuf signature");
_Static_assert(_Generic(&rdp_attach, uint32_t (*)(rdp_t *, int32_t): 1, default: 0), "rdp_attach signature");
_Static_assert(_Generic(&rdp_shutdown, void (*)(uint32_t): 1, default: 0), "rdp_shutdown signature");
_Static_assert(_Generic(&rdp_send, uint32_t (*)(rdp_t *, struct sockaddr *, uint16_t, const char *, uint32_t): 1, default: 0), "rdp_send signature");
_Static_assert(_Generic(&rdp_send_oversized, uint32_t (*)(rdp_t *, struct sockaddr *, uint16_t, const char *, uint32_t): 1, default: 0),
               "rdp_send_oversized signature");
_Static_assert(_Generic(&rdp_trace_capable, uint32_t (*)(rdp_t *): 1, default: 0), "rdp_trace_capable signature");
_Static_assert(_Generic(&rdp_get_local_addr, struct sockaddr *(*)(rdp_t *, int16_t): 1, default: 0), "rdp_get_local_addr signature");
_Static_assert(_Generic(&rdp_get_transport_mask, uint32_t (*)(void): 1, default: 0), "rdp_get_transport_mask signature");
_Static_assert(_Generic(&rdp_get_duplicate_input_rate, uint32_t (*)(rdp_t *): 1, default: 0), "rdp_get_duplicate_input_rate signature");
_Static_assert(_Generic(&rdp_get_serial_stats, void (*)(rdp_t *, serial_stat_t *): 1, default: 0), "rdp_get_serial_stats signature");
#endif

typedef struct owner_fixture_t
{
    rdp_t owner;
    int semaphore_created;
} owner_fixture_t;

typedef struct connection_fixture_t
{
    owner_fixture_t owner;
    connection_t *connection;
    struct sockaddr_in remote_addr;
} connection_fixture_t;

static rdp_stat test_statistics;

static int bytes_equal(const void *data, uint8_t value, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0; index < size; ++index)
    {
        if (bytes[index] != value)
        {
            return 0;
        }
    }
    return 1;
}

static msg_arrival_t *make_arrival(connection_t *sender, uint16_t options, uint16_t msgid, uint8_t stream, uint8_t stream_seqnum)
{
    msg_arrival_t *arrival = (msg_arrival_t *)fast_malloc((uint32_t)sizeof(*arrival));

    assert(arrival != NULL);
    memset(arrival, 0, sizeof(*arrival));
    msg_arrival_init(arrival, 0);
    arrival->sender = sender;
    arrival->options = options;
    arrival->msgid = msgid;
    arrival->stream = stream;
    arrival->stream_seqnum = stream_seqnum;
    return arrival;
}

static void owner_fixture_init(owner_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    rdp_init(&fixture->owner);
    assert(usemaphore_create(&fixture->owner.receive_semaphore) == 0);
    fixture->semaphore_created = 1;
}

static void drain_and_free(rxq_t *queue)
{
    msg_arrival_t *message;

    while ((message = rxq_remove_head(queue)) != NULL)
    {
        fast_free(message);
    }
}

static void owner_fixture_destroy(owner_fixture_t *fixture)
{
    drain_and_free(&fixture->owner.message_rxq);
    drain_and_free(&fixture->owner.external_rxq);
    rxq_destroy(&fixture->owner.message_rxq);
    rxq_destroy(&fixture->owner.external_rxq);
    if (fixture->semaphore_created)
    {
        usemaphore_destroy(&fixture->owner.receive_semaphore);
    }
    umutex_destroy(&fixture->owner.message_rxq_mutex);
    umutex_destroy(&fixture->owner.external_rxq_mutex);
}

static void make_ipv4_address(struct sockaddr_in *address, uint16_t port, uint32_t ipv4)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = RDP_TRANSMIT_ADDRESS_IPV4;
    address->sin_port = htons(port);
    address->sin_addr.s_addr = ipv4;
}

static uint16_t load_native_u16(const uint8_t *bytes)
{
    uint16_t value;

    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint8_t trace_index_reference(const uint8_t *quoted_ip, const uint8_t *quoted_udp)
{
    uint32_t sum;

    sum = load_native_u16(quoted_ip + 12);
    sum += load_native_u16(quoted_ip + 14);
    sum += load_native_u16(quoted_ip + 16);
    sum += load_native_u16(quoted_ip + 18);
    sum += quoted_ip[9];
    sum += load_native_u16(quoted_udp + 4);
    sum += load_native_u16(quoted_udp);
    sum += load_native_u16(quoted_udp + 2);
    sum += load_native_u16(quoted_udp + 4);
    sum += load_native_u16(quoted_udp + 6);
    sum = (sum & UINT32_C(0xffff)) + (sum >> 16);
    sum += sum >> 16;
    sum = (sum & UINT32_C(0xff)) + (sum >> 8);
    sum += sum >> 8;
    return (uint8_t)~sum;
}

static void connection_fixture_init(connection_fixture_t *fixture)
{
    connection_t *connection;

    memset(fixture, 0, sizeof(*fixture));
    owner_fixture_init(&fixture->owner);
    assert(connhash_create(&fixture->owner.owner.addr_map, 1) == 0);
    assert(eventq_create(&fixture->owner.owner.conn_eventq, 2) == 0);
    make_ipv4_address(&fixture->remote_addr, 9001, htonl(UINT32_C(0x7f000002)));

    connection = (connection_t *)rdplib_platform_malloc(sizeof(*connection));
    assert(connection != NULL);
    memset(connection, 0, sizeof(*connection));
    connection_init(connection, &fixture->owner.owner, (struct sockaddr *)&fixture->remote_addr, 0);
    assert(connection_create(connection) == 0);
    eventq_lock(&fixture->owner.owner.conn_eventq);
    assert(eventq_insert(&fixture->owner.owner.conn_eventq, connection) == 0);
    connhash_insert(&fixture->owner.owner.addr_map, connection);
    eventq_unlock(&fixture->owner.owner.conn_eventq);
    fixture->connection = connection;
}

static void connection_fixture_destroy(connection_fixture_t *fixture)
{
    connection_t *connection;

    if (fixture->connection)
    {
        connection = rdp_lock_connection(fixture->connection);
        assert(connection == fixture->connection);
        rdp_connection_mark_for_delete(&fixture->owner.owner, connection);
        fixture->connection = NULL;
        rdp_unlock(connection);
    }
    assert(eventq_peek_head(&fixture->owner.owner.conn_eventq) == NULL);
    eventq_destroy(&fixture->owner.owner.conn_eventq);
    connhash_destroy(&fixture->owner.owner.addr_map);
    owner_fixture_destroy(&fixture->owner);
}

static void test_init_selectivity(void)
{
    rdp_t owner;
    uint32_t before;
    uint32_t after;

    memset(&owner, 0xa5, sizeof(owner));
    before = time_get_ms();
    rdp_init(&owner);
    after = time_get_ms();

    assert(owner.startup == 0);
    assert(owner.udp_socket == -1 && owner.icmp_socket == -1 && owner.trace_socket == -1 && owner.ipx_socket == -1);
    assert(bytes_equal(&owner.local_udp_addr, 0, sizeof(owner.local_udp_addr)));
    assert(bytes_equal(&owner.trace_local_addr, 0, sizeof(owner.trace_local_addr)));
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(bytes_equal((const uint8_t *)&owner + offsetof(rdp_t, local_ipx_addr), 0xa5,
                       offsetof(rdp_t, udp_socket_ttl) - offsetof(rdp_t, local_ipx_addr)));
#else
    assert(bytes_equal((const uint8_t *)&owner + offsetof(rdp_t, local_ipx_addr), 0,
                       offsetof(rdp_t, udp_socket_ttl) - offsetof(rdp_t, local_ipx_addr))); // Maintained checked initialization includes ABI padding.
#endif
    assert(owner.udp_socket_ttl == 0 && owner.ipx_broadcast == 0 && owner.udp_broadcast == 0);
    assert(owner.addr_map.table_size == 0 && owner.addr_map.table == NULL);
    assert(owner.conn_eventq.q.array == NULL && owner.conn_eventq.q.next_element == 0);
    assert(owner.wake_sent == 0);
    assert(owner.app_is_waiting_for_exit == UINT32_C(0xa5a5a5a5));
    assert(owner.io_thread_running == UINT32_C(0xa5a5a5a5));
    assert(owner.io_thread.handle == NULL);
    assert(owner.message_rxq.list.head == NULL && owner.message_rxq.list.size == 0);
    assert(owner.external_rxq.list.head == NULL && owner.external_rxq.list.size == 0);
    assert(owner.bytes_recvd == 0 && owner.duplicate_bytes_recvd == 0);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(owner.bytes_per_second == UINT32_C(0xa5a5a5a5)); // The recovered duplicate assignment leaves this field untouched.
#else
    assert(owner.bytes_per_second == 0);
#endif
    assert(owner.duplicate_bytes_per_second == 0);
    assert(owner.encrypt == UINT32_C(0xa5a5a5a5) && owner.crc == UINT32_C(0xa5a5a5a5));
    assert((int32_t)(owner.last_sample - before) >= 0 && (int32_t)(after - owner.last_sample) >= 0);
    assert((intptr_t)owner.serial.file == -1 && owner.serial.rx_state == 0);

    rxq_destroy(&owner.message_rxq);
    rxq_destroy(&owner.external_rxq);
    usemaphore_destroy(&owner.receive_semaphore);
    umutex_destroy(&owner.message_rxq_mutex);
    umutex_destroy(&owner.external_rxq_mutex);
}

static void test_local_port_global(void)
{
    uint16_t saved = g_next_local_port;
    uint16_t first;
    uint16_t second;

    assert(saved == 1024);
    first = g_next_local_port++;
    second = g_next_local_port++;
    assert(first == 1024 && second == 1025 && g_next_local_port == 1026);
    g_next_local_port = saved;
}

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
static void test_create_consumes_local_ports(void)
{
    rdp_t *first = NULL;
    rdp_t *second = NULL;
    uint16_t saved = g_next_local_port;

    // No network transport is requested. The checked I/O loop remains a
    // bounded sleeping worker, which lets this exercise the real create call
    // site without opening a COM port or requiring an IPX stack.
    assert(rdp_create(&first, 0, 1, 0) == 0);
    assert(first != NULL && first->serial.local_port == saved && g_next_local_port == (uint16_t)(saved + 1u));
    assert(rdp_create(&second, 0, 1, 0) == 0);
    assert(second != NULL && second->serial.local_port == (uint16_t)(saved + 1u) && g_next_local_port == (uint16_t)(saved + 2u));
    rdp_destroy(second, 1);
    rdp_destroy(first, 1);
    g_next_local_port = saved;
}
#endif

static void test_crc_and_decode(void)
{
    uint8_t crc_buffer[32];
    uint8_t encoded[32];
    uint8_t invalid_padding[8];
    const uint8_t plain[] = {0x41, 0x52, 0x44, 0x50, 0x2d, 0x72, 0x64, 0x70, 0x21};
    uint32_t crc;
    int32_t encoded_size;

    memcpy(crc_buffer, plain, sizeof(plain));
    crc = htonl(rdp_crc(0, (const char *)crc_buffer, (uint32_t)sizeof(plain)));
    memcpy(crc_buffer + sizeof(plain), &crc, sizeof(crc));
    assert(rdp_verify_crc((char *)crc_buffer, (int32_t)(sizeof(plain) + sizeof(crc))) == (int32_t)sizeof(plain));
    crc_buffer[2] ^= 1;
    assert(rdp_verify_crc((char *)crc_buffer, (int32_t)(sizeof(plain) + sizeof(crc))) == 0);
    assert(rdp_verify_crc((char *)crc_buffer, 5) == 0);

    memcpy(encoded, plain, sizeof(plain));
    encoded_size = rdp_encode_data((char *)encoded, (int32_t)sizeof(plain));
    assert(encoded_size == 16);
    assert(rdp_decode_data((char *)encoded, encoded_size) == (int32_t)sizeof(plain));
    assert(memcmp(encoded, plain, sizeof(plain)) == 0);

    memset(invalid_padding, 0, sizeof(invalid_padding));
    invalid_padding[sizeof(invalid_padding) - 1] = 9;
    rdp_encode((char *)invalid_padding, 1);
    assert(rdp_decode_data((char *)invalid_padding, (int32_t)sizeof(invalid_padding)) == 0);
}

static void test_arrival_fifo_signal_and_receive(void)
{
    owner_fixture_t fixture;
    connection_t closed_sender;
    msg_arrival_t *first;
    msg_arrival_t *second;
    msg_arrival_t *received;
    uint32_t before;

    owner_fixture_init(&fixture);
    first = make_arrival(NULL, 0, 11, 0, 0);
    second = make_arrival(NULL, 0, 12, 0, 0);
    first->enqueue_time = UINT32_C(0xa5a5a5a5);
    second->enqueue_time = UINT32_C(0x5a5a5a5a);
    rdp_enqueue_arrival(&fixture.owner, first);
    rdp_enqueue_arrival(&fixture.owner, second);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(first->enqueue_time == 0 && second->enqueue_time == 0);
#else
    assert(first->enqueue_time == UINT32_C(0xa5a5a5a5) && second->enqueue_time == UINT32_C(0x5a5a5a5a));
#endif
    assert(fixture.owner.message_rxq.list.size == 2);
    assert(usemaphore_decrement(&fixture.owner.receive_semaphore, 0));
    assert(!usemaphore_decrement(&fixture.owner.receive_semaphore, 0)); // Only the empty to nonempty transition signals.
    assert(rxq_remove_head(&fixture.owner.message_rxq) == first);
    assert(rxq_remove_head(&fixture.owner.message_rxq) == second);
    fast_free(first);
    fast_free(second);

    first = make_arrival(NULL, 0, 21, 0, 0);
    second = make_arrival(NULL, 0, 22, 0, 0);
    rdp_enqueue_arrival(&fixture.owner, first);
    rdp_enqueue_arrival(&fixture.owner, second);
    received = rdp_receive(&fixture.owner, 0);
    assert(received == first);
    assert(fixture.owner.message_rxq.list.size == 0 && fixture.owner.external_rxq.list.size == 1);
    fast_free(received);
    received = rdp_receive(&fixture.owner, 0);
    assert(received == second);
    fast_free(received);
    assert(rdp_receive(&fixture.owner, 0) == NULL);
    before = time_get_ms();
    assert(rdp_receive(&fixture.owner, 2) == NULL);
    assert((uint32_t)(time_get_ms() - before) < 1000u);
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(rdp_receive(NULL, 0) == NULL);
#endif

    memset(&closed_sender, 0, sizeof(closed_sender));
    closed_sender.cn_rdp = &fixture.owner;
    closed_sender.cn_closed = 1;
    rdp_enqueue_arrival(&fixture.owner, make_arrival(&closed_sender, 0, 23, 0, 0));
    assert(fixture.owner.message_rxq.list.size == 0 && fixture.owner.external_rxq.list.size == 0);
    owner_fixture_destroy(&fixture);
}

static void test_connectionless_delivery(void)
{
    owner_fixture_t fixture;
    struct sockaddr_in source;
    msg_arrival_t *message;
    const char payload[] = "connectionless";

    owner_fixture_init(&fixture);
    make_ipv4_address(&source, 8123, htonl(UINT32_C(0x0a010203)));
    rdp_handle_connectionless(&fixture.owner, payload, (uint32_t)sizeof(payload), (struct sockaddr *)&source);
    message = rdp_receive(&fixture.owner, 0);
    assert(message != NULL && message->sender == NULL && message->options == UINT16_MAX);
    assert(message->size == sizeof(payload) && memcmp(msg_arrival_get_data(message), payload, sizeof(payload)) == 0);
    assert(memcmp(&message->from, &source, sizeof(message->from)) == 0);
    fast_free(message);
    owner_fixture_destroy(&fixture);
}

static void test_icmp_exact_boundary_and_trace_index(void)
{
    enum
    {
        OUTER_IP_BYTES = 20,
        ICMP_BYTES = 8,
        QUOTED_IP_BYTES = 20,
        PACKET_BYTES = OUTER_IP_BYTES + ICMP_BYTES + QUOTED_IP_BYTES + 8,
        EXPECTED_INDEX = 3
    };
    connection_fixture_t fixture;
    struct sockaddr_in gateway;
    trace_probe_t *probe;
    uint8_t packet[PACKET_BYTES];
    uint8_t *icmp;
    uint8_t *quoted_ip;
    uint8_t *quoted_udp;
    uint16_t network_word;
    uint32_t checksum;

    connection_fixture_init(&fixture);
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x45;
    icmp = packet + OUTER_IP_BYTES;
    icmp[0] = 11;
    quoted_ip = icmp + ICMP_BYTES;
    quoted_ip[0] = 0x45;
    quoted_ip[9] = 17;
    quoted_ip[12] = 10;
    quoted_ip[15] = 1;
    memcpy(quoted_ip + 16, &fixture.remote_addr.sin_addr, sizeof(fixture.remote_addr.sin_addr));
    quoted_udp = quoted_ip + QUOTED_IP_BYTES;
    fixture.owner.owner.trace_local_addr.sin_port = htons(32000);
    memcpy(quoted_udp, &fixture.owner.owner.trace_local_addr.sin_port, sizeof(fixture.owner.owner.trace_local_addr.sin_port));
    network_word = htons((uint16_t)(UINT16_C(0x8000) | ntohs(fixture.remote_addr.sin_port)));
    memcpy(quoted_udp + 2, &network_word, sizeof(network_word));
    network_word = htons(8);
    memcpy(quoted_udp + 4, &network_word, sizeof(network_word));

    for (checksum = 0; checksum <= UINT16_MAX; ++checksum)
    {
        network_word = (uint16_t)checksum;
        memcpy(quoted_udp + 6, &network_word, sizeof(network_word));
        if (trace_index_reference(quoted_ip, quoted_udp) == EXPECTED_INDEX)
        {
            break;
        }
    }
    assert(checksum <= UINT16_MAX);

    fixture.connection->trace_probes = (trace_probe_t *)rdplib_platform_malloc(4u * sizeof(*fixture.connection->trace_probes));
    assert(fixture.connection->trace_probes != NULL);
    memset(fixture.connection->trace_probes, 0, 4u * sizeof(*fixture.connection->trace_probes));
    fixture.connection->trace_next_index = 4;
    fixture.connection->trace_en_route = 1;
    probe = &fixture.connection->trace_probes[EXPECTED_INDEX];
    probe->time_sent = time_get_ms();
    make_ipv4_address(&gateway, 0, htonl(UINT32_C(0x0a0000fe)));

    rdp_handle_icmp_recv(&fixture.owner.owner, (char *)packet, PACKET_BYTES - 1, &gateway);
    assert(probe->icmp_type == 0 && fixture.connection->trace_en_route == 1);
    packet[0] = 0x4f;
    rdp_handle_icmp_recv(&fixture.owner.owner, (char *)packet, PACKET_BYTES, &gateway);
    assert(probe->icmp_type == 0 && fixture.connection->trace_en_route == 1);
    packet[0] = 0x45;
    rdp_handle_icmp_recv(&fixture.owner.owner, (char *)packet, PACKET_BYTES, &gateway);
    assert(probe->icmp_type == 11 && probe->icmp_code == 0);
    assert(probe->icmp_from.s_addr == gateway.sin_addr.s_addr);
    assert(fixture.connection->trace_en_route == 0);

    connection_fixture_destroy(&fixture);
}

static void test_complete_arrival_sequence_and_fin(void)
{
    owner_fixture_t fixture;
    connection_t connection;
    msg_arrival_t *late;
    msg_arrival_t *first;
    msg_arrival_t *ordinary;
    msg_arrival_t *fin;
    msg_arrival_t *received;

    owner_fixture_init(&fixture);
    memset(&connection, 0, sizeof(connection));
    connection.cn_rdp = &fixture.owner;
    rx_init(&connection);
    assert(rx_create(&connection) == 0);

    late = make_arrival(&connection, RDP_FLAG_SEQUENCED | RDP_FLAG_MSGID, 101, 3, 1);
    first = make_arrival(&connection, RDP_FLAG_SEQUENCED | RDP_FLAG_MSGID, 100, 3, 0);
    rdp_handle_complete_arrival(&fixture.owner, &connection, late);
    assert(fixture.owner.message_rxq.list.size == 0 && connection.rx_sequencer[3].list.size == 1);
    rdp_handle_complete_arrival(&fixture.owner, &connection, first);
    assert(fixture.owner.message_rxq.list.size == 2 && connection.rx_sequencer[3].list.size == 0);
    received = rdp_receive(&fixture.owner, 0);
    assert(received == first);
    fast_free(received);
    received = rdp_receive(&fixture.owner, 0);
    assert(received == late && connection.rx_guaranteed_stream_seqnum[3] == 2);
    fast_free(received);

    connection.rx_received_all_thru = 4;
    fin = make_arrival(&connection, RDP_FLAG_FIN | RDP_FLAG_MSGID, 5, 0, 0);
    rdp_handle_complete_arrival(&fixture.owner, &connection, fin);
    assert(connection.rx_fin_recvd == 1 && connection.rx_fin_storage == fin && fixture.owner.message_rxq.list.size == 0);
    connection.rx_received_all_thru = 5;
    ordinary = make_arrival(&connection, 0, 0, 0, 0);
    rdp_handle_complete_arrival(&fixture.owner, &connection, ordinary);
    received = rdp_receive(&fixture.owner, 0);
    assert(received == ordinary);
    fast_free(received);
    received = rdp_receive(&fixture.owner, 0);
    assert(received == fin && connection.rx_fin_storage == NULL);
    fast_free(received);

    rx_destroy(&connection);
    owner_fixture_destroy(&fixture);
}

static void test_disconnect_enqueue_once(void)
{
    owner_fixture_t fixture;
    connection_t connection;
    msg_arrival_t *message;

    owner_fixture_init(&fixture);
    memset(&connection, 0, sizeof(connection));
    connection.cn_rdp = &fixture.owner;
    rdp_enqueue_disconnect_msg(&fixture.owner, &connection);
    assert(connection.tx_enqueued_disconnect_msg == 1 && fixture.owner.message_rxq.list.size == 1);
    message = rdp_receive(&fixture.owner, 0);
    assert(message != NULL && message->sender == &connection && message->size == 0 && message->options == 0);
    fast_free(message);
    assert(rdp_receive(&fixture.owner, 0) == NULL);
    owner_fixture_destroy(&fixture);
}

static void test_serial_drain_and_forwarders(void)
{
    owner_fixture_t fixture;
    uint32_t before;
    uint32_t after;
    uint32_t scheduled;
    uint32_t empty_time;
    rdplib_rdp_input_rate_t input_rate;

    owner_fixture_init(&fixture);
    assert(serial_create(&fixture.owner.serial, 4321) == 0);
    serial_set_time_next_recv(&fixture.owner.serial, 1);
    before = time_get_ms();
    rdp_serial_drain(&fixture.owner);
    after = time_get_ms();
    scheduled = serial_get_time_next_recv(&fixture.owner.serial);
    assert((int32_t)(scheduled - before) >= 100 && (int32_t)(scheduled - after) <= 100);

    fixture.owner.bytes_per_second = 34567;
    fixture.owner.duplicate_bytes_per_second = 76543;
    memset(&input_rate, 0, sizeof(input_rate));
    rdplib_rdp_get_input_rate(&fixture.owner, &input_rate);
    assert(input_rate.bytes_per_second == 34567 && input_rate.duplicate_bytes_per_second == 76543);
    assert(rdp_get_input_rate(&fixture.owner) == 34567);
#ifdef RDP_DEAD_CODE
    assert(rdp_get_duplicate_input_rate(&fixture.owner) == 76543);
#endif
    assert(rdp_serial_tx_ready(&fixture.owner) == serial_tx_ready(&fixture.owner.serial));
    empty_time = rdp_serial_get_time_empty(&fixture.owner);
    assert((int32_t)(empty_time - time_get_ms()) >= -2 && (int32_t)(empty_time - time_get_ms()) <= 2);
    assert(rdp_get_serial_stall_time(&fixture.owner) == 0);
#ifdef RDP_DEAD_CODE
    {
        serial_stat_t stats;

        fixture.owner.serial.stats.bad_checksum = 17;
        memset(&stats, 0, sizeof(stats));
        rdp_get_serial_stats(&fixture.owner, &stats);
        assert(stats.bad_checksum == 17 && stats.tx_operations == 0 && stats.tx_bytes == 0);
    }
#endif
    serial_destroy(&fixture.owner.serial);
    owner_fixture_destroy(&fixture);
}

static void test_mark_delete_ownership(void)
{
    connection_fixture_t fixture;
    connection_t *locked;

    connection_fixture_init(&fixture);
    assert(fixture.connection->cn_ref_count == 1);
    locked = rdp_lock_connection(fixture.connection);
    assert(locked == fixture.connection && locked->cn_ref_count == 2);
    rdp_connection_mark_for_delete(&fixture.owner.owner, locked);
    assert(locked->cn_ref_count == 1);
    fixture.connection = NULL;
    rdp_unlock(locked);
    assert(eventq_peek_head(&fixture.owner.owner.conn_eventq) == NULL);
    assert(fixture.owner.owner.addr_map.table[0].list.size == 0);
    eventq_destroy(&fixture.owner.owner.conn_eventq);
    connhash_destroy(&fixture.owner.owner.addr_map);
    owner_fixture_destroy(&fixture.owner);
}

static void test_resort_latches_failed_wake(void)
{
    connection_fixture_t fixture;
    connection_t *locked;

    connection_fixture_init(&fixture);
    locked = rdp_lock_connection(fixture.connection);
    assert(locked == fixture.connection);
    locked->tx_connected = 1;
    locked->tx_delayed_ack = 1;
    locked->tx_ack_time = time_get_ms() + 50u;
    locked->cn_event_time.infinite = 1;
    fixture.owner.owner.wake_sent = 0;
    fixture.owner.owner.udp_socket = (intptr_t)-2; // A selected but invalid endpoint makes the backend send fail deterministically.
    fixture.owner.owner.ipx_socket = -1;
    rdp_resort(locked, 1);
    assert(fixture.owner.owner.wake_sent == 1); // The historical coalescing latch records the attempt, not its result.
    assert(locked->cn_event_time.infinite == 0 && locked->cn_event_time.time == locked->tx_ack_time);
    rdp_connection_mark_for_delete(&fixture.owner.owner, locked);
    fixture.connection = NULL;
    rdp_unlock(locked);
    eventq_destroy(&fixture.owner.owner.conn_eventq);
    connhash_destroy(&fixture.owner.owner.addr_map);
    owner_fixture_destroy(&fixture.owner);
}

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
static void test_immediate_close_rejects_pending_lookup(void)
{
    connection_fixture_t fixture;
    connection_t *connection;

    connection_fixture_init(&fixture);
    connection = fixture.connection;

    // Model an address lookup that took its temporary reference before close and is waiting for the connection lock.
    assert(fixture.owner.owner.addr_map.table_size == 1);
    umutex_lock(&fixture.owner.owner.addr_map.table[0].lock);
    ++connection->cn_ref_count;
    umutex_unlock(&fixture.owner.owner.addr_map.table[0].lock);

    assert(connection_close(connection, 0, NULL, NULL) == RDP_CONNECTION_SEND_OK);
    assert(connection->cn_abort == 1);
    assert(rdp_lock_addr(&fixture.owner.owner, (struct sockaddr *)&fixture.remote_addr) == NULL);

    umutex_lock(&connection->cn_lock);
    fixture.connection = NULL;
    rdp_unlock(connection);
    assert(eventq_peek_head(&fixture.owner.owner.conn_eventq) == NULL);
    connection_fixture_destroy(&fixture);
}

static void test_rejected_parser_full_path(void)
{
    connection_fixture_t fixture;
    uint8_t packet[2];
    uint16_t network_options;

    connection_fixture_init(&fixture);
    fixture.connection->tx_connected = 1;
    network_options = htons(RDP_FLAG_MSGID);
    memcpy(packet, &network_options, sizeof(network_options));
    assert(rdp_handle_data_recv(&fixture.owner.owner, (char *)packet, (int32_t)sizeof(packet), (struct sockaddr *)&fixture.remote_addr) == 0);
    assert(fixture.connection->stat.discarded_too_short == 1);
    assert(fixture.connection->tx_connected == 0 && fixture.connection->tx_disconnect_reason == RDP_DISCONNECT_REASON_PROTOCOL_ERROR);
    assert(fixture.owner.owner.message_rxq.list.size == 1); // Only the disconnect notification may be published; no uninitialized decoded header is consumed.
    assert(rdp_handle_data_recv(&fixture.owner.owner, (char *)packet, (int32_t)sizeof(packet), (struct sockaddr *)&fixture.remote_addr) == 0);
    assert(fixture.connection->stat.discarded_too_short == 2 && fixture.connection->tx_enqueued_disconnect_msg == 1);
    assert(fixture.owner.owner.message_rxq.list.size == 1); // Further rejected traffic cannot enqueue the disconnect a second time.
    connection_fixture_destroy(&fixture);
}
#endif

#ifdef RDP_DEAD_CODE
static void test_dead_symbol_references(void)
{
    (void)rdp_set_socket_rcvbuf;
    (void)rdp_set_socket_sndbuf;
    (void)rdp_get_socket_sndbuf;
    (void)rdp_attach;
    (void)rdp_shutdown;
    (void)rdp_send;
    (void)rdp_send_oversized;
    (void)rdp_trace_capable;
    (void)rdp_get_local_addr;
    (void)rdp_get_transport_mask;
    (void)rdp_get_duplicate_input_rate;
    (void)rdp_get_serial_stats;
}
#endif

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    fast_malloc_init(1024u * 1024u);

    test_local_port_global();
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    test_create_consumes_local_ports();
#endif
    test_init_selectivity();
    test_crc_and_decode();
    test_arrival_fifo_signal_and_receive();
    test_connectionless_delivery();
    test_icmp_exact_boundary_and_trace_index();
    test_complete_arrival_sequence_and_fin();
    test_disconnect_enqueue_once();
    test_serial_drain_and_forwarders();
    test_mark_delete_ownership();
    test_resort_latches_failed_wake();
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    test_immediate_close_rejects_pending_lookup();
    test_rejected_parser_full_path();
#endif
#ifdef RDP_DEAD_CODE
    test_dead_symbol_references();
#endif

    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return 0;
}
