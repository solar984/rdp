// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "rdp.h"

#include <stdio.h>
#include <string.h>

#include "fast.h"
#include "framing.h"
#include "net_error.h"

enum
{
    RDP_SOCKET_TYPE_DATAGRAM = 2,
    RDP_SOCKET_TYPE_RAW = 3,
    RDP_SOCKET_LEVEL = 0xFFFF,
    RDP_SOCKET_BROADCAST = 32,
    RDP_IP_LEVEL = 0,
    RDP_IP_TTL = 7,
    RDP_SOCKET_ERROR_ADDRESS_IN_USE = 10048,
    RDP_SOCKET_ERROR_OPTION_UNSUPPORTED = 10042
};

static uint16_t load_native_u16(const uint8_t *bytes)
{
    uint16_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint16_t load_network_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
}

static int format_ipv4(char *output, const uint8_t address[16])
{
    return sprintf(output, "AF_INET %u.%u.%u.%u:%u", address[4], address[5], address[6], address[7], load_network_u16(address + 2));
}

int rdp_format_sockaddr_mac(char *output, const uint8_t address[16])
{
    uint32_t family = address[1];

    if (family == 6)
    {
        return (int)family;
    }
    if (family == 69)
    {
        uint32_t port = (uint32_t)(int32_t)(int16_t)load_native_u16(address + 2);
        return sprintf(output, "AF_COMPORT %u", port);
    }
    if (family == 2)
    {
        return format_ipv4(output, address);
    }
    return sprintf(output, "unknown address family (%u)", family);
}

int rdp_format_sockaddr_windows(char *output, const uint8_t address[16])
{
    uint32_t family = load_native_u16(address);

    switch (family)
    {
    case 2:
        return format_ipv4(output, address);
    case 6:
    {
        char node[16];
        char network[16];

        // The MSVC source passes both uninitialized arrays to %s. This can read past either array and overrun output; family 6 is not a normal game RDP endpoint.
        return sprintf(output, "AF_IPX %s:%s:%u", node, network, load_native_u16(address + 12));
    }
    case 69:
    {
        uint32_t port = (uint32_t)(int32_t)(int16_t)load_native_u16(address + 2);
        return sprintf(output, "AF_COMPORT %u", port);
    }
    default:
        return sprintf(output, "unknown address family (%u)", family);
    }
}

void rdp_init(rdp_t *owner)
{
    owner->sockets_initialized = 0;
    owner->ipv4_socket = -1;
    owner->icmp_receive_socket = -1;
    owner->icmp_probe_socket = -1;
    owner->ipx_socket = -1;
    memset(owner->ipv4_address, 0, sizeof(owner->ipv4_address));
    memset(owner->probe_address, 0, sizeof(owner->probe_address));
    memset(owner->ipx_address, 0, sizeof(owner->ipx_address));
    owner->probe_socket_default_ttl = 0;
    owner->ipx_broadcast_enabled = 0;
    owner->ipv4_broadcast_enabled = 0;
    owner->input_bytes = 0;
    owner->duplicate_input_bytes = 0;
    owner->duplicate_input_bytes_per_second = 0;

    serial_init(&owner->serial);
    connhash_init(&owner->connections);
    memset(&owner->events, 0, sizeof(owner->events));
    owner->io_wakeup_pending = 0;
    owner->io_thread = NULL;

    memset(&owner->application_receive, 0, sizeof(owner->application_receive));
    rdplib_platform_semaphore_init(&owner->application_receive.arrival_semaphore);
    list_init(&owner->application_receive.producer_queue.messages);
    list_init(&owner->application_receive.consumer_queue.messages);
    rdplib_platform_mutex_init(&owner->application_receive.producer_lock);
    rdplib_platform_mutex_init(&owner->application_receive.consumer_lock);
    owner->rate_sample_time_ms = rdplib_platform_current_time_ms();
}

int rdp_create(rdp_t **output, uint16_t local_port, uint32_t expected_connections, uint32_t flags)
{
    rdp_t *owner;
    uint8_t bind_address[16];
    uint32_t address_bytes;
    uint32_t option_value;
#ifdef RDPLIB_SOURCE_FAITHFUL
    uint32_t option_bytes;
#endif
    uint16_t bucket_count;
    uint16_t address_family;
    uint16_t network_port;
    int udp_protocol;
    int result;

    *output = NULL;
    owner = (rdp_t *)rdplib_platform_malloc(sizeof(*owner));
    if (!owner)
    {
        return RDP_CREATE_ALLOCATION_FAILED;
    }

    rdp_init(owner);
    result = RDP_CREATE_FAILED;
    if (rdplib_platform_network_startup(UINT16_C(0x0101)) != 0)
    {
        goto failed;
    }
    owner->sockets_initialized = 1;

    result = serial_create(&owner->serial, rdplib_platform_next_serial_port());
    if (result != 0)
    {
        goto failed;
    }
    serial_set_time_next_recv(&owner->serial, rdplib_platform_current_time_ms());

    bucket_count = expected_connections == 1 ? 1 : 12;
    result = connhash_create(&owner->connections, bucket_count);
    if (result != 0)
    {
        goto failed;
    }
    result = eventq_create(&owner->events);
    if (result != 0)
    {
        goto failed;
    }
    result = rdplib_platform_semaphore_create(&owner->application_receive.arrival_semaphore);
    if (result != 0)
    {
        goto failed;
    }

    udp_protocol = rdplib_platform_protocol_number("udp", 17);
    if ((flags & (RDP_CREATE_ATTEMPT_BOTH | RDP_CREATE_REQUIRE_IPV4)) != 0)
    {
        owner->ipv4_socket = rdplib_platform_socket_create(RDP_TRANSMIT_ADDRESS_IPV4, RDP_SOCKET_TYPE_DATAGRAM, udp_protocol);
    }
    if ((flags & RDP_CREATE_REQUIRE_IPV4) != 0 && owner->ipv4_socket == -1)
    {
        result = RDP_CREATE_REQUIRED_TRANSPORT_UNAVAILABLE;
        goto failed;
    }

    if ((flags & (RDP_CREATE_ATTEMPT_BOTH | RDP_CREATE_REQUIRE_LEGACY)) != 0)
    {
        owner->ipx_socket = rdplib_platform_socket_create(RDP_TRANSMIT_ADDRESS_IPX, RDP_SOCKET_TYPE_DATAGRAM, 1000);
    }
    if ((flags & RDP_CREATE_REQUIRE_LEGACY) != 0 && owner->ipx_socket == -1)
    {
        result = RDP_CREATE_REQUIRED_TRANSPORT_UNAVAILABLE;
        goto failed;
    }

    if (owner->ipv4_socket != -1)
    {
        memset(owner->ipv4_address, 0, sizeof(owner->ipv4_address));
        address_family = RDP_TRANSMIT_ADDRESS_IPV4;
        network_port = htons(local_port);
        memcpy(owner->ipv4_address, &address_family, sizeof(address_family));
        memcpy(owner->ipv4_address + 2, &network_port, sizeof(network_port));
        if (rdplib_platform_socket_bind(owner->ipv4_socket, owner->ipv4_address, sizeof(owner->ipv4_address)) != 0)
        {
            result = rdplib_platform_last_socket_error() == RDP_SOCKET_ERROR_ADDRESS_IN_USE ? RDP_CREATE_ADDRESS_IN_USE : RDP_CREATE_FAILED;
            goto failed;
        }

        option_value = 1;
        owner->ipv4_broadcast_enabled = rdplib_platform_socket_set_option(owner->ipv4_socket, RDP_SOCKET_LEVEL, RDP_SOCKET_BROADCAST, &option_value, sizeof(option_value)) == 0;
        address_bytes = sizeof(owner->ipv4_address);
        if (rdplib_platform_socket_get_name(owner->ipv4_socket, owner->ipv4_address, &address_bytes) != 0)
        {
            result = RDP_CREATE_FAILED;
            goto failed;
        }
        result = rdplib_platform_socket_disable_blocking(owner->ipv4_socket);
        if (result != 0)
        {
            goto failed;
        }

#ifndef RDPLIB_SOURCE_FAITHFUL
#if defined(__linux__) || defined(_WIN32)
        // The checked build uses errors attributed by the UDP socket. This
        // works without raw socket privileges and identifies the exact peer
        // on a shared endpoint.
        if (rdplib_platform_enable_icmp_errors(owner->ipv4_socket) != 0)
        {
            rdplib_platform_record_socket_error(rdplib_platform_last_socket_error());
        }
#endif
#else
        owner->icmp_receive_socket = rdplib_platform_socket_create(RDP_TRANSMIT_ADDRESS_IPV4, RDP_SOCKET_TYPE_RAW, rdplib_platform_protocol_number("icmp", 1));
        if (owner->icmp_receive_socket == -1)
        {
            rdplib_platform_record_socket_error(rdplib_platform_last_socket_error());
            goto ipv4_complete;
        }

        result = rdplib_platform_socket_disable_blocking(owner->icmp_receive_socket);
        if (result != 0)
        {
            goto failed;
        }
        memset(bind_address, 0, sizeof(bind_address));
        memcpy(bind_address, &address_family, sizeof(address_family));
        if (rdplib_platform_socket_bind(owner->icmp_receive_socket, bind_address, sizeof(bind_address)) != 0)
        {
            result = RDP_CREATE_FAILED;
            goto failed;
        }

        owner->icmp_probe_socket = rdplib_platform_socket_create(RDP_TRANSMIT_ADDRESS_IPV4, RDP_SOCKET_TYPE_DATAGRAM, udp_protocol);
        if (owner->icmp_probe_socket == -1)
        {
            result = RDP_CREATE_FAILED;
            goto failed;
        }
        memset(owner->probe_address, 0, sizeof(owner->probe_address));
        memcpy(owner->probe_address, &address_family, sizeof(address_family));
        if (rdplib_platform_socket_bind(owner->icmp_probe_socket, owner->probe_address, sizeof(owner->probe_address)) != 0)
        {
            result = RDP_CREATE_FAILED;
            goto failed;
        }
        address_bytes = sizeof(owner->probe_address);
        if (rdplib_platform_socket_get_name(owner->icmp_probe_socket, owner->probe_address, &address_bytes) != 0)
        {
            result = RDP_CREATE_FAILED;
            goto failed;
        }
        result = rdplib_platform_socket_disable_blocking(owner->icmp_probe_socket);
        if (result != 0)
        {
            goto failed;
        }

        option_bytes = sizeof(owner->probe_socket_default_ttl);
        if (rdplib_platform_socket_get_option(owner->icmp_probe_socket, RDP_IP_LEVEL, RDP_IP_TTL, &owner->probe_socket_default_ttl, &option_bytes) != 0)
        {
            if (rdplib_platform_last_socket_error() == RDP_SOCKET_ERROR_OPTION_UNSUPPORTED)
            {
                rdplib_platform_socket_close(owner->icmp_probe_socket);
                owner->icmp_probe_socket = -1;
                goto ipv4_complete;
            }
            rdplib_platform_record_socket_error(rdplib_platform_last_socket_error());
            result = RDP_CREATE_FAILED;
            goto failed;
        }

        if (rdplib_platform_socket_set_option(owner->icmp_probe_socket, RDP_IP_LEVEL, RDP_IP_TTL, &owner->probe_socket_default_ttl, sizeof(owner->probe_socket_default_ttl)) != 0)
        {
            if (rdplib_platform_last_socket_error() == RDP_SOCKET_ERROR_OPTION_UNSUPPORTED)
            {
                rdplib_platform_socket_close(owner->icmp_probe_socket);
                owner->icmp_probe_socket = -1;
                goto ipv4_complete;
            }
            rdplib_platform_record_socket_error(rdplib_platform_last_socket_error());
            result = RDP_CREATE_FAILED;
            goto failed;
        }
#endif
    }

#ifdef RDPLIB_SOURCE_FAITHFUL
ipv4_complete:
#endif
    if (owner->ipx_socket != -1)
    {
        memset(bind_address, 0, 14);
        address_family = RDP_TRANSMIT_ADDRESS_IPX;
        memcpy(bind_address, &address_family, sizeof(address_family));
        memcpy(bind_address + 12, &local_port, sizeof(local_port));
        if (rdplib_platform_socket_bind(owner->ipx_socket, bind_address, 14) != 0)
        {
            result = rdplib_platform_last_socket_error() == RDP_SOCKET_ERROR_ADDRESS_IN_USE ? RDP_CREATE_ADDRESS_IN_USE : RDP_CREATE_FAILED;
            goto failed;
        }

        option_value = 1;
        owner->ipx_broadcast_enabled = rdplib_platform_socket_set_option(owner->ipx_socket, RDP_SOCKET_LEVEL, RDP_SOCKET_BROADCAST, &option_value, sizeof(option_value)) == 0;
        address_bytes = 14;
        if (rdplib_platform_socket_get_name(owner->ipx_socket, owner->ipx_address, &address_bytes) != 0)
        {
            result = RDP_CREATE_FAILED;
            goto failed;
        }
        result = rdplib_platform_socket_disable_blocking(owner->ipx_socket);
        if (result != 0)
        {
            goto failed;
        }
    }

    owner->use_encryption = flags >> 31;
    owner->use_crc = (flags >> 30) & 1u;
    owner->io_thread_running = 1;
    result = rdplib_platform_thread_create(&owner->io_thread, (rdplib_platform_thread_entry_t)rdp_io_thread, owner);
    if (result != 0)
    {
        owner->io_thread_running = 0;
        goto failed;
    }

    *output = owner;
    return RDP_CREATE_OK;

failed:
    owner->join_io_thread_on_destroy = 0;
    rdp_destroy_internal(owner);
    return result;
}

int rdp_connection_create_internal(rdp_t *owner, connection_t **output, const uint8_t endpoint[16], uint32_t options)
{
    connection_t *connection = NULL;
    int result = RDP_CONNECT_INVALID_ARGUMENT;

    if ((options & ~RDP_CONNECTION_FEATURE_KEEPALIVE) == 0)
    {
        connection = (connection_t *)rdplib_platform_malloc(sizeof(*connection));
        result = RDP_CONNECT_ALLOCATION_FAILED;
        if (connection)
        {
            connection_init(connection, owner, endpoint, options);
            result = connection_create(connection);
            if (result == 0)
            {
                rdplib_platform_mutex_lock(&owner->events.lock);
                (void)pqueue_insert(&owner->events.queue, &connection->event_link);
                connhash_insert(&owner->connections, connection);
                rdplib_platform_mutex_unlock(&owner->events.lock);

                (void)connhash_lock(&owner->connections, connection->transmit.remote_address);
                *output = connection;
            }
        }
    }

    if (result != 0 && connection)
    {
        connection_destroy(connection);
        rdplib_platform_free(connection);
    }

    return result;
}

int rdp_connect(rdp_t *owner, connection_t **output, const char *host, uint16_t port, uint32_t options)
{
    connection_t *connection;
    uint8_t endpoint[16];
    int result;

    if (rdplib_platform_resolve_ipv4(host, port, endpoint) != 0)
    {
        return RDP_CONNECT_RESOLVE_FAILED;
    }

    ++g_rdp_stat->outgoing_connection_attempts;
    result = rdp_connection_create_internal(owner, &connection, endpoint, options);
    if (result == 0)
    {
        connection->locally_initiated = 1;
        rdp_unlock(connection);
        *output = connection;
    }
    return result;
}

void rdp_enqueue_arrival(rdp_application_receive_t *receive, msg_arrival_t *message)
{
    void *previous_head;

    if (message->sender_connection && ((connection_t *)message->sender_connection)->linger_active)
    {
        fast_free(message);
        return;
    }

    msg_arrival_prepare_for_rxq(message);
    rdplib_platform_mutex_lock(&receive->producer_lock);

    previous_head = receive->producer_queue.messages.head ? receive->producer_queue.messages.head->value : NULL;
    message->_f0028 = 0;
    list_add_tail(&receive->producer_queue.messages, &message->link);
    if (!previous_head)
    {
        // The clients signal under the producer lock and only for the empty to nonempty transition.
        rdplib_platform_semaphore_signal(&receive->arrival_semaphore);
    }

    rdplib_platform_mutex_unlock(&receive->producer_lock);
}

void rdp_handle_connectionless(rdp_t *owner, const uint8_t *payload, uint32_t payload_bytes, const uint8_t source[16])
{
    msg_arrival_t *message = (msg_arrival_t *)fast_malloc((uint32_t)sizeof(*message) + payload_bytes);

    // The clients initialize this unchecked allocation immediately.
    memset(message, 0, sizeof(*message));
    msg_arrival_init(message, 0);
    message->flags = UINT16_C(0xFFFF);
    message->payload_bytes = payload_bytes;
    memcpy(msg_arrival_get_data(message), payload, payload_bytes);
    memcpy(message->sender_address, source, sizeof(message->sender_address));
    rdp_enqueue_arrival(&owner->application_receive, message);
}

void rdp_handle_complete_arrival(rdp_t *owner, connection_t *connection, msg_arrival_t *message)
{
    rdp_receive_ownership_state_t *ownership = &connection->receive.ownership;

    if (message->flags & RDP_FLAG_FIN)
    {
        rx_save_fin_arrival(connection, message);
    }
    else if (message->flags & RDP_FLAG_SEQUENCED)
    {
        if (message->flags & RDP_FLAG_MSGID)
        {
            uint8_t stream_id = message->stream_id;
            msg_arrival_t *next;

            rx_sort_into_sequence(connection, message);
            while ((next = rx_get_next_in_sequence(connection, stream_id)) != NULL)
            {
                rdp_enqueue_arrival(&owner->application_receive, next);
            }
        }
        else if (rx_in_sequence(connection, message))
        {
            rdp_enqueue_arrival(&owner->application_receive, message);
        }
        else
        {
            fast_free(message);
        }
    }
    else
    {
        rdp_enqueue_arrival(&owner->application_receive, message);
    }

    if (ownership->fin_arrival_pending && connection->receive.ack.received_through_message_id == ownership->fin_message_id && ownership->saved_fin_arrival)
    {
        rdp_enqueue_arrival(&owner->application_receive, rx_load_fin_arrival(connection));
    }
}

uint32_t rdp_handle_data_recv(rdp_t *owner, uint8_t *packet, int32_t packet_bytes, const uint8_t source_address[16])
{
    connection_t *connection = NULL;
    _rdp_header_t header;
    msg_arrival_t *arrival;
    rdp_rx_arrival_disposition_t disposition;
    uint32_t duplicate_reliable = 1;
    uint32_t expected_crc;
    uint32_t network_crc;
    uint32_t wake_token;
    uint16_t network_flags;
    uint16_t source_family;
    uint16_t flags;

    if (packet_bytes < 2)
    {
        return 0;
    }

    memcpy(&source_family, source_address, sizeof(source_family));
    if ((source_family == RDP_TRANSMIT_ADDRESS_IPV4 && memcmp(source_address + 2, owner->ipv4_address + 2, 2) == 0 && source_address[4] == 127 && source_address[5] == 0 && source_address[6] == 0 &&
         source_address[7] == 1) ||
        memcmp(source_address, owner->ipx_address, 16) == 0)
    {
        if (packet_bytes == sizeof(wake_token))
        {
            memcpy(&wake_token, packet, sizeof(wake_token));
            return wake_token;
        }

        ++g_rdp_stat->invalid_local_wakeup_datagrams;
        return 0;
    }

    owner->input_bytes += packet_bytes + 28u;
    if (owner->use_encryption)
    {
        if (packet_bytes != 0 && (packet_bytes & 7u) == 0)
        {
            uint8_t padding_bytes;

            rdp_decode(packet, (int)(packet_bytes / 8u));
            padding_bytes = packet[packet_bytes - 1u] & 0x0Fu;
            packet_bytes = padding_bytes <= 8u ? packet_bytes - padding_bytes : 0;
        }
        else
        {
            ++g_rdp_stat->invalid_encrypted_datagram_sizes;
        }
    }

    memcpy(&network_flags, packet, sizeof(network_flags));
    if ((owner->use_crc || owner->use_encryption) && packet_bytes >= 6 && ntohs(network_flags) != UINT16_C(0xFFFF))
    {
        packet_bytes -= 4u;
        memcpy(&network_crc, packet + packet_bytes, sizeof(network_crc));
        expected_crc = ntohl(network_crc);
        if (packet_bytes == 0 || expected_crc != rdp_crc(0, packet, packet_bytes))
        {
            ++g_rdp_stat->illegal_framed_datagrams;
            return 0;
        }
    }

    if (packet_bytes < 2)
    {
        return 0;
    }

    memcpy(&network_flags, packet, sizeof(network_flags));
    flags = ntohs(network_flags);
    if (flags == UINT16_C(0xFFFF))
    {
        rdp_handle_connectionless(owner, packet + 2, packet_bytes - 2u, source_address);
        return 0;
    }

    connection = connhash_lock(&owner->connections, source_address);
    if (!connection)
    {
        if ((flags & RDP_FLAG_SYN) != 0)
        {
            (void)rdp_connection_create_internal(owner, &connection, source_address, 0);
            ++g_rdp_stat->incoming_connection_attempts;
        }
        else
        {
            ++g_rdp_stat->unknown_endpoint_datagrams;
        }
    }

    if (!connection || connection->reserved_receive_gate)
    {
        // A nonzero reserved gate leaks the locked temporary reference in all
        // 3 game clients. No transport writer for the field was found.
        return 0;
    }

#ifndef RDPLIB_SOURCE_FAITHFUL
    if (connection->rdplib_packet_drop_callback &&
        connection->rdplib_packet_drop_callback(connection->rdplib_packet_drop_context, RDPLIB_PACKET_DROP_INBOUND, packet, (uint32_t)packet_bytes))
    {
        rdp_unlock(connection);
        return 0;
    }
#endif

    disposition = connection_parse_and_validate_arrival(connection, packet, (uint16_t)packet_bytes, &header);
#ifdef RDPLIB_SOURCE_FAITHFUL
    (void)rdplib_platform_current_time_ms();
#endif
    if (disposition != RDP_RX_ACCEPT)
    {
        if ((header.flags & RDP_FLAG_SYN) != 0)
        {
            ++g_rdp_stat->rejected_incoming_syn_datagrams;
        }
    }
    else
    {
        connection_record_arrival(connection, &header, &duplicate_reliable);
        if (duplicate_reliable)
        {
            owner->duplicate_input_bytes += packet_bytes + 28u;
        }
    }

    if (!connection->transmit.connected && !connection->transmit.disconnect_message_queued)
    {
        arrival = (msg_arrival_t *)fast_malloc((uint32_t)sizeof(*arrival));
        msg_arrival_init_disconnect_msg(arrival, connection);
        rdp_enqueue_arrival(&owner->application_receive, arrival);
        connection->transmit.disconnect_message_queued = 1;
    }
    rdp_resort(connection, 0);

    if (connection->transmit.connected)
    {
        if (connection->linger_active)
        {
            ++g_rdp_stat->application_arrivals_discarded_during_linger;
        }
        else if ((header.flags & RDP_FLAG_SYSTEM) == 0 && !duplicate_reliable && disposition == RDP_RX_ACCEPT && (header.payload_bytes != 0 || (header.flags & RDP_FLAG_FIN) != 0))
        {
            arrival = rx_assemble(connection, &header, packet + header.header_bytes);
            if (arrival)
            {
                rdp_handle_complete_arrival(owner, connection, arrival);
            }
        }
    }
    else
    {
        ++g_rdp_stat->application_arrivals_discarded_after_disconnect;
    }

    rdp_unlock(connection);
    return 0;
}

void rdp_handle_reported_icmp(rdp_t *owner, const uint8_t remote_address[16], uint8_t type, uint8_t code, uint8_t trace_response, uint8_t trace_sample,
                              const uint8_t source_address[16])
{
    connection_t *connection;
    msg_arrival_t *arrival;

    connection = connhash_lock(&owner->connections, remote_address);
    if (!connection)
    {
        return;
    }

#ifndef RDPLIB_SOURCE_FAITHFUL
    // Multiple errors can already be queued for retransmissions to the
    // same peer. Do not publish the same disconnect more than once.
    if (!trace_response && !connection->transmit.connected && connection->transmit.disconnect_message_queued)
    {
        rdp_unlock(connection);
        return;
    }
#endif

    connection_handle_icmp(connection, type, code, trace_response, trace_sample, source_address);
    if (!connection->transmit.connected && !connection->transmit.disconnect_message_queued)
    {
        arrival = (msg_arrival_t *)fast_malloc((uint32_t)sizeof(*arrival));
        msg_arrival_init_disconnect_msg(arrival, connection);
        rdp_enqueue_arrival(&owner->application_receive, arrival);
        connection->transmit.disconnect_message_queued = 1;
    }
    rdp_resort(connection, 0);
    rdp_unlock(connection);
}

void rdp_handle_icmp_recv(rdp_t *owner, const uint8_t *packet, uint32_t packet_bytes, const uint8_t source_address[16])
{
    const uint8_t *quoted_ipv4;
    const uint8_t *quoted_udp;
    const uint8_t *icmp;
    uint8_t remote_address[16] = {0};
    uint32_t outer_header_bytes;
    uint32_t quoted_header_bytes;
    uint32_t sum;
    uint16_t address_family;
    uint16_t network_word;
    uint16_t remote_port;
    uint8_t trace_response;
    uint8_t trace_sample = 0;

    if (packet_bytes == UINT32_MAX)
    {
        return;
    }

    owner->input_bytes += packet_bytes + 28u;
    if (packet_bytes < 28u)
    {
        return;
    }

    outer_header_bytes = (packet[0] & 0x0Fu) * 4u;
    if (packet_bytes < outer_header_bytes + 8u)
    {
        return;
    }

    icmp = packet + outer_header_bytes;
    if ((icmp[0] != 3 && icmp[0] != 4 && icmp[0] != 11) || packet_bytes < outer_header_bytes + 28u)
    {
        return;
    }

    quoted_ipv4 = icmp + 8;
    quoted_header_bytes = (quoted_ipv4[0] & 0x0Fu) * 4u;
    if (packet_bytes < outer_header_bytes + quoted_header_bytes + 16u || quoted_ipv4[9] != 17)
    {
        return;
    }

    quoted_udp = quoted_ipv4 + quoted_header_bytes;
    if (memcmp(quoted_udp, owner->ipv4_address + 2, 2) != 0 && memcmp(quoted_udp, owner->probe_address + 2, 2) != 0)
    {
        return;
    }

    trace_response = (uint8_t)(memcmp(quoted_udp, owner->probe_address + 2, 2) == 0);
    memcpy(&network_word, quoted_udp + 2, sizeof(network_word));
    remote_port = ntohs(network_word);
    if (trace_response)
    {
        sum = 0;
        memcpy(&network_word, quoted_ipv4 + 12, sizeof(network_word));
        sum += ntohs(network_word);
        memcpy(&network_word, quoted_ipv4 + 14, sizeof(network_word));
        sum += ntohs(network_word);
        memcpy(&network_word, quoted_ipv4 + 16, sizeof(network_word));
        sum += ntohs(network_word);
        memcpy(&network_word, quoted_ipv4 + 18, sizeof(network_word));
        sum += ntohs(network_word);
        sum += quoted_ipv4[9];
        memcpy(&network_word, quoted_udp + 4, sizeof(network_word));
        sum += ntohs(network_word);
        memcpy(&network_word, quoted_udp, sizeof(network_word));
        sum += ntohs(network_word);
        memcpy(&network_word, quoted_udp + 2, sizeof(network_word));
        sum += ntohs(network_word);
        memcpy(&network_word, quoted_udp + 4, sizeof(network_word));
        sum += ntohs(network_word);
        memcpy(&network_word, quoted_udp + 6, sizeof(network_word));
        sum += ntohs(network_word);
        sum = (sum & UINT32_C(0xFFFF)) + (sum >> 16);
        sum += sum >> 16;
        sum = (sum & UINT32_C(0xFF)) + (sum >> 8);
        sum += sum >> 8;
        trace_sample = (uint8_t)~sum;
        remote_port &= UINT16_C(0x7FFF);
    }

    address_family = RDP_TRANSMIT_ADDRESS_IPV4;
    network_word = htons(remote_port);
    memcpy(remote_address, &address_family, sizeof(address_family));
    memcpy(remote_address + 2, &network_word, sizeof(network_word));
    memcpy(remote_address + 4, quoted_ipv4 + 16, 4);
    rdp_handle_reported_icmp(owner, remote_address, icmp[0], icmp[1], trace_response, trace_sample, source_address);
}

uint32_t rdp_io_thread(rdp_t *owner)
{
    uint8_t packet[536];
    uint8_t source_address[16];
    uint8_t remote_address[16];
#if defined(_WIN32) && !defined(RDPLIB_SOURCE_FAITHFUL)
    const uint8_t unknown_icmp_source[16] = {0};
#endif
    rdp_timeval_t event_timeout_storage;
    rdp_timeval_t serial_timeout_storage;
    rdp_timeval_t *wait_timeout;
    connection_t *connection;
    msg_arrival_t *arrival;
    rdp_timeout_data_t next_timeout;
    uint32_t enabled_sources;
    uint32_t ready_sources;
    int32_t packet_bytes;
    uint32_t wake_token;
    uint32_t now_ms;
    uint32_t elapsed_ms;
    uint32_t last_event_pass_time_ms;
    uint32_t serial_timeout_microseconds;
    int32_t serial_time_remaining_ms;
    int wait_result; // Intentionally uninitialized on the serial only sleep path, as in all 3 clients.

    last_event_pass_time_ms = rdplib_platform_current_time_ms();
#ifdef RDPLIB_SOURCE_FAITHFUL
    (void)rdplib_platform_current_time_ms();
#endif
    while (owner->io_thread_running)
    {
        enabled_sources = RDP_IO_SOURCE_NONE;
        if (owner->ipv4_socket != -1)
        {
            enabled_sources |= RDP_IO_SOURCE_IPV4;
        }
        if (owner->ipx_socket != -1)
        {
            enabled_sources |= RDP_IO_SOURCE_LEGACY;
        }
        if (owner->icmp_receive_socket != -1)
        {
            enabled_sources |= RDP_IO_SOURCE_ICMP;
        }
        ready_sources = enabled_sources;

        rdplib_platform_mutex_lock(&owner->events.lock);
        wait_timeout = eventq_get_event_timeout(&owner->events, &event_timeout_storage);
        owner->io_wakeup_pending = 0;
        rdplib_platform_mutex_unlock(&owner->events.lock);

        if (owner->serial.endpoint != -1)
        {
            serial_time_remaining_ms = (int32_t)(serial_get_time_next_recv(&owner->serial) - rdplib_platform_current_time_ms());
            serial_timeout_microseconds = serial_time_remaining_ms > 0 ? 1000u * (uint32_t)serial_time_remaining_ms : 0;
            if (!wait_timeout || wait_timeout->seconds != 0 || wait_timeout->microseconds > serial_timeout_microseconds)
            {
                serial_timeout_storage.seconds = 0;
                serial_timeout_storage.microseconds = serial_timeout_microseconds;
                wait_timeout = &serial_timeout_storage;
            }
        }

        if (owner->serial.endpoint != -1 && owner->ipx_socket == -1 && owner->ipv4_socket == -1)
        {
            rdplib_platform_sleep_ms(wait_timeout->microseconds / 1000u);
#ifndef RDPLIB_SOURCE_FAITHFUL
            wait_result = 0;
#endif
        }
        else if (!wait_timeout || wait_timeout->seconds != 0 || wait_timeout->microseconds != 0)
        {
            if (owner->ipx_socket == -1 && owner->ipv4_socket == -1)
            {
                rdplib_platform_sleep_ms(100);
                wait_result = 0;
            }
            else
            {
                wait_result = rdplib_platform_wait(owner->ipv4_socket, owner->ipx_socket, owner->icmp_receive_socket, enabled_sources, wait_timeout == NULL, wait_timeout ? wait_timeout->seconds : 0,
                                                   wait_timeout ? wait_timeout->microseconds : 0, &ready_sources);
            }
        }
        else
        {
            elapsed_ms = rdplib_platform_current_time_ms() - last_event_pass_time_ms;
            if (elapsed_ms < 4)
            {
                rdplib_platform_sleep_ms(4u - elapsed_ms);
            }
            wait_result = 2;
        }

#ifdef RDPLIB_SOURCE_FAITHFUL
        (void)rdplib_platform_current_time_ms();
#endif
        if (owner->serial.endpoint != -1 && (int32_t)(rdplib_platform_current_time_ms() - serial_get_time_next_recv(&owner->serial)) > 0)
        {
            do
            {
#ifdef _WIN32
                packet_bytes = serial_recv_from_windows(&owner->serial, packet, sizeof(packet), source_address);
#else
                packet_bytes = serial_recv_from_mac(&owner->serial, packet, sizeof(packet), source_address);
#endif
                (void)rdp_handle_data_recv(owner, packet, packet_bytes, source_address);
            } while (packet_bytes != -1);
            serial_set_time_next_recv(&owner->serial, rdplib_platform_current_time_ms() + 100u);
        }

        if (wait_result <= 0)
        {
            ++g_rdp_stat->io_wait_nonpositive_returns;
        }
        else
        {
            if ((ready_sources & RDP_IO_SOURCE_IPV4) != 0 && owner->ipv4_socket != -1)
            {
#if defined(__linux__) && !defined(RDPLIB_SOURCE_FAITHFUL)
                if (owner->icmp_receive_socket == -1)
                {
                    rdplib_platform_icmp_error_t error;

                    while (rdplib_platform_receive_icmp_error(owner->ipv4_socket, &error))
                    {
                        // Match the recovered raw receiver's accepted ICMP
                        // types. Only type 3/code 3 aborts the connection.
                        if (error.type == 3 || error.type == 4 || error.type == 11)
                        {
                            rdp_handle_reported_icmp(owner, error.remote_address, error.type, error.code, 0, 0, error.source_address);
                        }
                    }
                }
#endif
                do
                {
                    packet_bytes = rdplib_platform_receive_datagram(owner->ipv4_socket, packet, sizeof(packet), source_address);
#if defined(_WIN32) && !defined(RDPLIB_SOURCE_FAITHFUL)
                    if (packet_bytes == RDPLIB_PLATFORM_RECEIVE_ICMP_PORT_UNREACHABLE)
                    {
                        wake_token = 0;
                        rdp_handle_reported_icmp(owner, source_address, 3, 3, 0, 0, unknown_icmp_source);
                    }
                    else
#endif
                    {
                        wake_token = rdp_handle_data_recv(owner, packet, packet_bytes, source_address);
                    }
                } while (!wake_token && packet_bytes != -1);
            }

            if ((ready_sources & RDP_IO_SOURCE_LEGACY) != 0 && owner->ipx_socket != -1)
            {
                do
                {
                    memset(source_address, 0, sizeof(source_address));
                    packet_bytes = rdplib_platform_receive_datagram(owner->ipx_socket, packet, sizeof(packet), source_address);
                    wake_token = rdp_handle_data_recv(owner, packet, packet_bytes, source_address);
                } while (!wake_token && packet_bytes != -1);
            }

            if ((ready_sources & RDP_IO_SOURCE_ICMP) != 0 && owner->icmp_receive_socket != -1)
            {
                do
                {
                    memset(source_address, 0, sizeof(source_address));
                    packet_bytes = rdplib_platform_receive_datagram(owner->icmp_receive_socket, packet, sizeof(packet), source_address);
                    rdp_handle_icmp_recv(owner, packet, (uint32_t)packet_bytes, source_address);
                } while (packet_bytes != -1);
            }
        }

        now_ms = rdplib_platform_current_time_ms();
        elapsed_ms = now_ms - owner->rate_sample_time_ms;
        if (elapsed_ms > 1000)
        {
            owner->input_bytes_per_second = 1000u * owner->input_bytes / elapsed_ms;
            owner->duplicate_input_bytes_per_second = 1000u * owner->duplicate_input_bytes / elapsed_ms;
            owner->input_bytes = 0;
            owner->duplicate_input_bytes = 0;
            owner->rate_sample_time_ms = now_ms;
        }

        last_event_pass_time_ms = rdplib_platform_current_time_ms();
        for (;;)
        {
            rdplib_platform_mutex_lock(&owner->events.lock);
            connection = (connection_t *)pqueue_peek_head(&owner->events.queue);
            if (!connection || connection->event_timeout.infinite || (int32_t)(last_event_pass_time_ms - connection->event_timeout.deadline_ms) < 0)
            {
                connection = NULL;
            }
            else
            {
                memcpy(remote_address, connection->transmit.remote_address, sizeof(remote_address));
            }
            rdplib_platform_mutex_unlock(&owner->events.lock);

            if (!connection)
            {
                break;
            }

            connection = connhash_lock(&owner->connections, remote_address);
            if (!connection)
            {
                continue;
            }

            connection_event_process(connection, last_event_pass_time_ms, &next_timeout);
            if (connection_linger_expired(connection))
            {
                (void)connhash_subref(&owner->connections, connection);
            }
            else
            {
                if (!connection->transmit.connected && !connection->transmit.disconnect_message_queued)
                {
                    arrival = (msg_arrival_t *)fast_malloc((uint32_t)sizeof(*arrival));
                    msg_arrival_init_disconnect_msg(arrival, connection);
                    rdp_enqueue_arrival(&owner->application_receive, arrival);
                    connection->transmit.disconnect_message_queued = 1;
                }

                rdplib_platform_mutex_lock(&owner->events.lock);
                if (memcmp(&connection->event_timeout, &next_timeout, sizeof(next_timeout)) != 0)
                {
                    connection->event_timeout = next_timeout;
                    pqueue_resort_by_link(&owner->events.queue, &connection->event_link);
                }
                rdplib_platform_mutex_unlock(&owner->events.lock);
            }
            rdp_unlock(connection);
        }
    }

    rdp_destroy_internal(owner);
    return 0;
}

void rdp_resort(connection_t *connection, int wake)
{
    rdp_t *owner = connection->owner;
    rdp_timeout_data_t timeout;

    connection_recalc_event_timeout(connection, &timeout);
    rdplib_platform_mutex_lock(&owner->events.lock);
    if (memcmp(&connection->event_timeout, &timeout, sizeof(timeout)) != 0)
    {
        connection->event_timeout = timeout;
        pqueue_resort_by_link(&owner->events.queue, &connection->event_link);

        if (pqueue_peek_head(&owner->events.queue) == connection && owner->io_wakeup_pending == 0 && wake)
        {
            rdplib_platform_send_wakeup(owner->ipv4_socket, owner->ipv4_address, 0);
            owner->io_wakeup_pending = 1;
        }
    }
    rdplib_platform_mutex_unlock(&owner->events.lock);
}

void rdp_destroy_internal(rdp_t *owner)
{
    connection_t *connection;
    msg_arrival_t *message;

    while ((connection = (connection_t *)pqueue_remove_head(&owner->events.queue)) != NULL)
    {
        connection = rdp_connection_mark_for_delete(owner, connection);
        connection_destroy(connection);
        rdplib_platform_free(connection);
    }

    while ((message = (msg_arrival_t *)list_remove_head(&owner->application_receive.producer_queue.messages)) != NULL)
    {
        fast_free(message);
    }
    while ((message = (msg_arrival_t *)list_remove_head(&owner->application_receive.consumer_queue.messages)) != NULL)
    {
        fast_free(message);
    }

    if (owner->ipv4_socket != -1)
    {
        rdplib_platform_socket_close(owner->ipv4_socket);
    }
    owner->ipv4_socket = -1;
    if (owner->icmp_receive_socket != -1)
    {
        rdplib_platform_socket_close(owner->icmp_receive_socket);
    }
    owner->icmp_receive_socket = -1;
    if (owner->icmp_probe_socket != -1)
    {
        rdplib_platform_socket_close(owner->icmp_probe_socket);
    }
    owner->icmp_probe_socket = -1;
    if (owner->ipx_socket != -1)
    {
        rdplib_platform_socket_close(owner->ipx_socket);
    }
    owner->ipx_socket = -1;
    if (owner->sockets_initialized)
    {
        rdplib_platform_network_cleanup();
        owner->sockets_initialized = 0;
    }

#ifdef _WIN32
    serial_destroy_windows(&owner->serial);
#else
    serial_destroy_mac(&owner->serial);
#endif
    connhash_destroy(&owner->connections);
    eventq_destroy(&owner->events);
    rdplib_platform_semaphore_destroy(&owner->application_receive.arrival_semaphore);
    list_destroy(&owner->application_receive.producer_queue.messages);
    rdplib_platform_mutex_destroy(&owner->application_receive.producer_lock);
    list_destroy(&owner->application_receive.consumer_queue.messages);
    rdplib_platform_mutex_destroy(&owner->application_receive.consumer_lock);

    if (!owner->join_io_thread_on_destroy)
    {
        rdplib_platform_thread_destroy(owner->io_thread);
        rdplib_platform_free(owner);
    }
}

void rdp_destroy(rdp_t *owner, int wait_for_io_thread)
{
    uint32_t exit_code;

    if (owner->io_thread_running != 1)
    {
        return;
    }

    owner->join_io_thread_on_destroy = wait_for_io_thread != 0;
    owner->io_thread_running = 0;
    rdplib_platform_send_wakeup(owner->ipv4_socket, owner->ipv4_address, 1);

    if (wait_for_io_thread)
    {
        rdplib_platform_thread_wait(owner->io_thread, &exit_code);
        rdplib_platform_thread_destroy(owner->io_thread);
        rdplib_platform_free(owner);
    }
}

connection_t *rdp_connection_mark_for_delete(rdp_t *owner, connection_t *connection)
{
    return connhash_subref(&owner->connections, connection);
}

int connection_close_wait(connection_t *connection, uint32_t timeout_ms, int *result)
{
    rdplib_platform_event_t completion_event;
    int create_result;

    rdplib_platform_event_init(&completion_event);
    create_result = rdplib_platform_event_create(&completion_event);
    if (create_result != 0)
    {
        create_result = 1;
    }
    else
    {
        create_result = 0;
        (void)connection_close(connection, timeout_ms, result, &completion_event);
        (void)rdplib_platform_event_wait(&completion_event);
    }
    rdplib_platform_event_destroy(&completion_event);
    return create_result;
}

msg_arrival_t *rdp_receive(rdp_t *owner, int32_t timeout_ms)
{
    rdp_application_receive_t *receive;
    msg_arrival_t *message;
    uint32_t deadline = 0;
    int32_t remaining = timeout_ms;

    if (!owner)
    {
        return NULL;
    }
    receive = &owner->application_receive;

    rdplib_platform_mutex_lock(&receive->consumer_lock);
    message = (msg_arrival_t *)list_remove_head(&receive->consumer_queue.messages);
    rdplib_platform_mutex_unlock(&receive->consumer_lock);
    if (message)
    {
        return message;
    }

    if (timeout_ms != 0 && timeout_ms != -1)
    {
        deadline = rdplib_platform_current_time_ms() + (uint32_t)timeout_ms;
    }

    do
    {
        if (owner->serial.endpoint != -1)
        {
            uint8_t packet[536];
            uint8_t source_address[16];
            int32_t packet_bytes;

            do
            {
#ifdef _WIN32
                packet_bytes = serial_recv_from_windows(&owner->serial, packet, sizeof(packet), source_address);
#else
                packet_bytes = serial_recv_from_mac(&owner->serial, packet, sizeof(packet), source_address);
#endif
                (void)rdp_handle_data_recv(owner, packet, packet_bytes, source_address);
            } while (packet_bytes != -1);
            serial_set_time_next_recv(&owner->serial, rdplib_platform_current_time_ms() + 100u);
        }

        if (!rdplib_platform_semaphore_wait(&receive->arrival_semaphore, remaining))
        {
            return NULL;
        }

        rdplib_platform_mutex_lock(&receive->producer_lock);
        rdplib_platform_mutex_lock(&receive->consumer_lock);

        // The receiving application thread guarantees this list is empty. The
        // clients copy all 5 list words rather than splice or append.
        receive->consumer_queue.messages = receive->producer_queue.messages;
        list_init(&receive->producer_queue.messages);
        message = (msg_arrival_t *)list_remove_head(&receive->consumer_queue.messages);

        rdplib_platform_mutex_unlock(&receive->consumer_lock);
        rdplib_platform_mutex_unlock(&receive->producer_lock);
        if (message)
        {
            return message;
        }

        if (timeout_ms != 0 && timeout_ms != -1)
        {
            remaining = (int32_t)(deadline - rdplib_platform_current_time_ms());
        }
    } while (timeout_ms != 0 && (timeout_ms == -1 || remaining > 0));

    return NULL;
}

void rdp_unlock(connection_t *connection)
{
    rdp_t *owner = connection->owner;
    connection_t *released;

    rdplib_platform_mutex_unlock(&connection->lock);
    released = connhash_subref(&owner->connections, connection);
    if (released)
    {
        rdplib_platform_mutex_lock(&owner->events.lock);
        pqueue_remove_by_link(&owner->events.queue, &released->event_link);
        rdplib_platform_mutex_unlock(&owner->events.lock);
        connection_destroy(released);
        rdplib_platform_free(released);
    }
}

uint32_t rdp_get_input_rate(const rdp_t *owner)
{
    return owner->input_bytes_per_second;
}

int rdp_serial_tx_ready(rdp_t *owner)
{
    return serial_tx_ready(&owner->serial);
}

uint32_t rdp_serial_get_time_empty(rdp_t *owner)
{
    return serial_get_time_empty(&owner->serial);
}

uint32_t rdp_get_serial_stall_time(rdp_t *owner)
{
    return serial_get_stall_time(&owner->serial);
}
