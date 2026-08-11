// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "rdp.h"

struct rdplib_runtime_t
{
    rdplib_platform_mutex_t lock;
    rdp_global_statistics_t statistics;
    uint32_t endpoint_count;
    uint32_t outstanding_messages;
};

struct rdplib_message_t
{
    struct rdplib_message_t *next;
    rdplib_runtime_t *runtime;
    msg_arrival_t *arrival;
    uint8_t sender_address[16];
    uint32_t payload_bytes;
    uint16_t flags;
    uint8_t stream_id;
    uint8_t disconnect;
};

struct rdplib_connection_t
{
    struct rdplib_connection_t *all_next;
    struct rdplib_connection_t *accept_next;
    rdplib_endpoint_t *endpoint;
    connection_t *raw;
    rdplib_message_t *message_head;
    rdplib_message_t *message_tail;
    uint8_t remote_address[16];
    uint8_t application_owned;
    uint8_t close_started;
    uint8_t peer_fin;
    uint8_t disconnected;
};

struct rdplib_endpoint_t
{
    rdplib_runtime_t *runtime;
    rdp_t *raw;
    uint32_t application_connection_count;
    rdplib_connection_t *connections;
    rdplib_connection_t *accept_head;
    rdplib_connection_t *accept_tail;
    rdplib_message_t *connectionless_head;
    rdplib_message_t *connectionless_tail;
};

static rdplib_runtime_t *rdplib_active_runtime;

static void rdplib_runtime_add_outstanding(rdplib_runtime_t *runtime)
{
    rdplib_platform_mutex_lock(&runtime->lock);
    ++runtime->outstanding_messages;
    rdplib_platform_mutex_unlock(&runtime->lock);
}

static void rdplib_runtime_remove_outstanding(rdplib_runtime_t *runtime)
{
    rdplib_platform_mutex_lock(&runtime->lock);
    assert(runtime->outstanding_messages != 0);
    --runtime->outstanding_messages;
    rdplib_platform_mutex_unlock(&runtime->lock);
}

static void rdplib_connection_destroy_handle(rdplib_connection_t *connection)
{
    assert(connection->raw == NULL);
    assert(connection->message_head == NULL);
    assert(connection->endpoint == NULL);
    assert(!connection->application_owned);
    rdplib_platform_free(connection);
}

static void rdplib_connection_clear_packet_drop_callback(connection_t *raw)
{
#ifndef RDPLIB_SOURCE_FAITHFUL
    rdplib_platform_mutex_lock(&raw->lock);
    raw->rdplib_packet_drop_callback = NULL;
    raw->rdplib_packet_drop_context = NULL;
    rdplib_platform_mutex_unlock(&raw->lock);
#else
    (void)raw;
#endif
}

static void rdplib_endpoint_remove_connection(rdplib_endpoint_t *endpoint, rdplib_connection_t *connection)
{
    rdplib_connection_t **link = &endpoint->connections;

    while (*link && *link != connection)
    {
        link = &(*link)->all_next;
    }

    // Every application owned connection remains linked to its endpoint until release.
    assert(*link != NULL);
    if (!*link)
    {
        return;
    }

    *link = connection->all_next;
    connection->all_next = NULL;
}

void rdplib_connection_release(rdplib_connection_t *connection)
{
    rdplib_endpoint_t *endpoint;
    connection_t *raw;

    if (!connection)
    {
        return;
    }

    assert(connection->application_owned);
    assert(!connection->raw || connection->close_started || connection->peer_fin || connection->disconnected);
    assert(connection->message_head == NULL);
    endpoint = connection->endpoint;
    assert(endpoint != NULL);
    assert(endpoint->application_connection_count != 0);

    raw = connection->raw;
    if (raw)
    {
        // A terminal peer state permits release without a prior explicit close.
        assert(!connection->close_started);
        connection->close_started = 1;
        rdplib_connection_clear_packet_drop_callback(raw);
        (void)connection_close(raw, 0, NULL, NULL);
        connection->raw = NULL;
    }

    rdplib_endpoint_remove_connection(endpoint, connection);
    --endpoint->application_connection_count;
    connection->application_owned = 0;
    connection->endpoint = NULL;
    rdplib_connection_destroy_handle(connection);
}

static rdplib_connection_t *rdplib_find_connection(rdplib_endpoint_t *endpoint, connection_t *raw)
{
    rdplib_connection_t *connection;

    for (connection = endpoint->connections; connection; connection = connection->all_next)
    {
        if (connection->raw == raw)
        {
            return connection;
        }
    }
    return NULL;
}

static rdplib_connection_t *rdplib_create_connection_handle(rdplib_endpoint_t *endpoint, connection_t *borrowed, int accept_pending)
{
    rdplib_connection_t *connection;

    if (!borrowed)
    {
        return NULL;
    }

    connection = (rdplib_connection_t *)rdplib_platform_malloc(sizeof(*connection));
    if (!connection)
    {
        return NULL;
    }

    memset(connection, 0, sizeof(*connection));
    connection->endpoint = endpoint;
    connection->raw = borrowed;
    connection->application_owned = (uint8_t)(accept_pending == 0);
    memcpy(connection->remote_address, borrowed->transmit.remote_address, sizeof(connection->remote_address));
    connection->all_next = endpoint->connections;
    endpoint->connections = connection;

    if (!accept_pending)
    {
        ++endpoint->application_connection_count;
    }

    if (accept_pending)
    {
        if (endpoint->accept_tail)
        {
            endpoint->accept_tail->accept_next = connection;
        }
        else
        {
            endpoint->accept_head = connection;
        }
        endpoint->accept_tail = connection;
    }
    return connection;
}

static rdplib_message_t *rdplib_wrap_message(rdplib_endpoint_t *endpoint, msg_arrival_t *arrival)
{
    rdplib_message_t *message = (rdplib_message_t *)rdplib_platform_malloc(sizeof(*message));

    if (!message)
    {
        fast_free(arrival);
        return NULL;
    }

    memset(message, 0, sizeof(*message));
    message->runtime = endpoint->runtime;
    // The caller owns the original fast allocation.  The payload is not copied.
    message->arrival = arrival;
    message->payload_bytes = arrival->payload_bytes;
    message->flags = arrival->flags;
    message->stream_id = arrival->stream_id;
    message->disconnect = (uint8_t)(arrival->sender_connection != NULL && arrival->payload_bytes == 0 && (arrival->flags & RDP_FLAG_FIN) == 0);
    memcpy(message->sender_address, arrival->sender_address, sizeof(message->sender_address));

    rdplib_runtime_add_outstanding(endpoint->runtime);
    return message;
}

static void rdplib_queue_connection_message(rdplib_connection_t *connection, rdplib_message_t *message)
{
    if (connection->message_tail)
    {
        connection->message_tail->next = message;
    }
    else
    {
        connection->message_head = message;
    }
    connection->message_tail = message;
    if (rdplib_message_has_fin(message))
    {
        connection->peer_fin = 1;
    }
    if (rdplib_message_is_disconnect(message))
    {
        connection->disconnected = 1;
    }
}

static void rdplib_queue_connectionless(rdplib_endpoint_t *endpoint, rdplib_message_t *message)
{
    if (endpoint->connectionless_tail)
    {
        endpoint->connectionless_tail->next = message;
    }
    else
    {
        endpoint->connectionless_head = message;
    }
    endpoint->connectionless_tail = message;
}

static int rdplib_process_arrival(rdplib_endpoint_t *endpoint, msg_arrival_t *arrival)
{
    connection_t *raw = (connection_t *)arrival->sender_connection;
    rdplib_connection_t *connection = NULL;
    rdplib_message_t *message;

    if (raw)
    {
        int create_for_arrival = arrival->payload_bytes != 0 || (arrival->flags & RDP_FLAG_FIN) != 0;

        connection = rdplib_find_connection(endpoint, raw);
        if (!connection && create_for_arrival)
        {
            connection = rdplib_create_connection_handle(endpoint, raw, 1);
        }
        if (!connection)
        {
            fast_free(arrival);
            return create_for_arrival ? RDPLIB_ERROR_OUT_OF_MEMORY : RDPLIB_OK;
        }
    }

    message = rdplib_wrap_message(endpoint, arrival);
    if (!message)
    {
        return RDPLIB_ERROR_OUT_OF_MEMORY;
    }

    if (connection)
    {
        rdplib_queue_connection_message(connection, message);
    }
    else
    {
        rdplib_queue_connectionless(endpoint, message);
    }
    return RDPLIB_OK;
}

int rdplib_runtime_create(rdplib_runtime_t **output, uint32_t fast_allocator_bytes)
{
    rdplib_runtime_t *runtime;

    if (!output || fast_allocator_bytes == 0 || rdplib_active_runtime)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    *output = NULL;

    runtime = (rdplib_runtime_t *)rdplib_platform_malloc(sizeof(*runtime));
    if (!runtime)
    {
        return RDPLIB_ERROR_OUT_OF_MEMORY;
    }

    memset(runtime, 0, sizeof(*runtime));
    rdplib_platform_mutex_prepare(&runtime->lock);
    rdplib_platform_mutex_init(&runtime->lock);
    fast_malloc_init(fast_allocator_bytes);
    g_rdp_stat = &runtime->statistics;
    rdplib_active_runtime = runtime;
    *output = runtime;
    return RDPLIB_OK;
}

int rdplib_runtime_destroy(rdplib_runtime_t *runtime)
{
    uint32_t endpoint_count;
    uint32_t outstanding_messages;

    if (!runtime || runtime != rdplib_active_runtime)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }

    rdplib_platform_mutex_lock(&runtime->lock);
    endpoint_count = runtime->endpoint_count;
    outstanding_messages = runtime->outstanding_messages;
    rdplib_platform_mutex_unlock(&runtime->lock);
    assert(endpoint_count == 0 && outstanding_messages == 0);
    if (endpoint_count != 0 || outstanding_messages != 0)
    {
        return RDPLIB_ERROR_BUSY;
    }

    rdplib_active_runtime = NULL;
    g_rdp_stat = NULL;
    fast_malloc_destroy();
    rdplib_platform_mutex_destroy(&runtime->lock);
    rdplib_platform_free(runtime);
    return RDPLIB_OK;
}

int rdplib_endpoint_create(rdplib_runtime_t *runtime, rdplib_endpoint_t **output, uint16_t local_port, uint32_t expected_connections, uint32_t flags)
{
    rdplib_endpoint_t *endpoint;
    int result;

    if (!runtime || runtime != rdplib_active_runtime || !output || (flags & ~(RDPLIB_USE_CRC | RDPLIB_USE_ENCRYPTION)) != 0)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    *output = NULL;

    endpoint = (rdplib_endpoint_t *)rdplib_platform_malloc(sizeof(*endpoint));
    if (!endpoint)
    {
        return RDPLIB_ERROR_OUT_OF_MEMORY;
    }
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->runtime = runtime;

    result = rdp_create(&endpoint->raw, local_port, expected_connections, flags | RDP_CREATE_REQUIRE_IPV4);
    if (result != 0)
    {
        rdplib_platform_free(endpoint);
        return result;
    }

    rdplib_platform_mutex_lock(&runtime->lock);
    ++runtime->endpoint_count;
    rdplib_platform_mutex_unlock(&runtime->lock);
    *output = endpoint;
    return RDPLIB_OK;
}

static void rdplib_detach_connection(rdplib_connection_t *connection)
{
    rdplib_message_t *message;
    rdplib_message_t *next;

    assert(!connection->application_owned);
    connection->raw = NULL;
    connection->endpoint = NULL;
    message = connection->message_head;
    connection->message_head = NULL;
    connection->message_tail = NULL;

    for (; message; message = next)
    {
        next = message->next;
        message->next = NULL;
        rdplib_message_release(message);
    }
    rdplib_connection_destroy_handle(connection);
}

int rdplib_endpoint_destroy(rdplib_endpoint_t *endpoint)
{
    rdplib_connection_t *connection;
    rdplib_connection_t *next_connection;
    rdplib_message_t *message;
    rdplib_message_t *next_message;
    rdplib_runtime_t *runtime;

    if (!endpoint)
    {
        return RDPLIB_OK;
    }
    if (endpoint->application_connection_count != 0)
    {
        return RDPLIB_ERROR_BUSY;
    }

    runtime = endpoint->runtime;
    endpoint->accept_head = NULL;
    endpoint->accept_tail = NULL;
    for (connection = endpoint->connections; connection; connection = next_connection)
    {
        next_connection = connection->all_next;
        connection->all_next = NULL;
        connection->accept_next = NULL;
        rdplib_detach_connection(connection);
    }
    endpoint->connections = NULL;

    for (message = endpoint->connectionless_head; message; message = next_message)
    {
        next_message = message->next;
        message->next = NULL;
        rdplib_message_release(message);
    }

    rdp_destroy(endpoint->raw, 1);
    rdplib_platform_mutex_lock(&runtime->lock);
    assert(runtime->endpoint_count != 0);
    --runtime->endpoint_count;
    rdplib_platform_mutex_unlock(&runtime->lock);
    rdplib_platform_free(endpoint);
    return RDPLIB_OK;
}

uint16_t rdplib_endpoint_local_port(const rdplib_endpoint_t *endpoint)
{
    return endpoint && endpoint->raw ? (uint16_t)(((uint16_t)endpoint->raw->ipv4_address[2] << 8) | endpoint->raw->ipv4_address[3]) : 0;
}

int rdplib_endpoint_process(rdplib_endpoint_t *endpoint, int32_t timeout_ms)
{
    msg_arrival_t *arrival;
    int count = 0;
    int result;

    if (!endpoint || !endpoint->raw)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }

    arrival = rdp_receive(endpoint->raw, timeout_ms);
    while (arrival)
    {
        result = rdplib_process_arrival(endpoint, arrival);
        if (result != RDPLIB_OK)
        {
            return result;
        }
        ++count;
        arrival = rdp_receive(endpoint->raw, 0);
    }
    return count;
}

rdplib_connection_t *rdplib_endpoint_accept(rdplib_endpoint_t *endpoint)
{
    rdplib_connection_t *connection;

    if (!endpoint)
    {
        return NULL;
    }

    connection = endpoint->accept_head;
    if (!connection)
    {
        return NULL;
    }
    endpoint->accept_head = connection->accept_next;
    if (!endpoint->accept_head)
    {
        endpoint->accept_tail = NULL;
    }
    connection->accept_next = NULL;
    assert(!connection->application_owned);
    connection->application_owned = 1;
    ++endpoint->application_connection_count;
    return connection;
}

rdplib_message_t *rdplib_endpoint_pop_connectionless(rdplib_endpoint_t *endpoint)
{
    rdplib_message_t *message;

    if (!endpoint)
    {
        return NULL;
    }
    message = endpoint->connectionless_head;
    if (message)
    {
        endpoint->connectionless_head = message->next;
        if (!endpoint->connectionless_head)
        {
            endpoint->connectionless_tail = NULL;
        }
        message->next = NULL;
    }
    return message;
}

int rdplib_connect(rdplib_endpoint_t *endpoint, rdplib_connection_t **output, const char *host, uint16_t port)
{
    connection_t *raw = NULL;
    rdplib_connection_t *connection;
    int result;

    if (!endpoint || !output || !host)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    *output = NULL;

    result = rdp_connect(endpoint->raw, &raw, host, port, 0);
    if (result != 0)
    {
        return result;
    }

    connection = rdplib_create_connection_handle(endpoint, raw, 0);
    if (!connection)
    {
        (void)connection_close(raw, 0, NULL, NULL);
        return RDPLIB_ERROR_OUT_OF_MEMORY;
    }
    *output = connection;
    return RDPLIB_OK;
}

int rdplib_connection_is_usable(rdplib_connection_t *connection)
{
    connection_t *raw;
    int usable;

    if (!connection)
    {
        return 0;
    }
    raw = connection->raw;
    usable = raw && !connection->close_started && !connection->peer_fin && !connection->disconnected;
    if (usable)
    {
        rdplib_platform_mutex_lock(&raw->lock);
        usable = raw->transmit.connected && !raw->transmit.transmit_stopped && !raw->transmit.fin_sent;
        rdplib_platform_mutex_unlock(&raw->lock);
    }
    return usable;
}

static int rdplib_connection_enable_keepalive_internal(rdplib_connection_t *connection, uint32_t interval_ms, int replace_interval)
{
    connection_t *raw;
    int result = RDPLIB_OK;
    int resort = 0;

    if (!connection)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    raw = connection->raw;
    if (!raw || connection->close_started || connection->peer_fin || connection->disconnected)
    {
        return RDPLIB_ERROR_NOT_USABLE;
    }

    rdplib_platform_mutex_lock(&raw->lock);
    if (!raw->transmit.connected || raw->transmit.fin_sent || raw->transmit.transmit_stopped)
    {
        result = RDPLIB_ERROR_NOT_USABLE;
    }
    else
    {
#ifndef RDPLIB_SOURCE_FAITHFUL
        if (replace_interval && raw->rdplib_keepalive_interval_ms != interval_ms)
        {
            raw->rdplib_keepalive_interval_ms = interval_ms;
            resort = 1;
        }
#else
        (void)interval_ms;
        (void)replace_interval;
        assert(!replace_interval);
#endif
        if ((raw->options & RDP_CONNECTION_FEATURE_KEEPALIVE) == 0)
        {
            raw->options |= RDP_CONNECTION_FEATURE_KEEPALIVE;
            resort = 1;
        }
        if (resort)
        {
            rdp_resort(raw, 1);
        }
    }
    rdplib_platform_mutex_unlock(&raw->lock);
    return result;
}

int rdplib_connection_enable_keepalive(rdplib_connection_t *connection)
{
    return rdplib_connection_enable_keepalive_internal(connection, RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS, 0);
}

int rdplib_connection_enable_keepalive_with_interval(rdplib_connection_t *connection, uint32_t interval_ms)
{
    if (!connection || interval_ms == 0 || interval_ms > INT32_MAX)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
#ifdef RDPLIB_SOURCE_FAITHFUL
    return RDPLIB_ERROR_NOT_SUPPORTED;
#else
    return rdplib_connection_enable_keepalive_internal(connection, interval_ms, 1);
#endif
}

int rdplib_connection_set_packet_drop_callback(rdplib_connection_t *connection, rdplib_packet_drop_callback_t callback, void *context)
{
#ifndef RDPLIB_SOURCE_FAITHFUL
    connection_t *raw;
    int result = RDPLIB_OK;
#endif

    if (!connection)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
#ifdef RDPLIB_SOURCE_FAITHFUL
    (void)callback;
    (void)context;
    return RDPLIB_ERROR_NOT_SUPPORTED;
#else
    raw = connection->raw;
    if (!raw || connection->close_started || (callback && (connection->peer_fin || connection->disconnected)))
    {
        return RDPLIB_ERROR_NOT_USABLE;
    }

    rdplib_platform_mutex_lock(&raw->lock);
    if (callback && (!raw->transmit.connected || raw->transmit.fin_sent || raw->transmit.transmit_stopped))
    {
        result = RDPLIB_ERROR_NOT_USABLE;
    }
    else
    {
        raw->rdplib_packet_drop_callback = callback;
        raw->rdplib_packet_drop_context = callback ? context : NULL;
    }
    rdplib_platform_mutex_unlock(&raw->lock);
    return result;
#endif
}

rdplib_message_t *rdplib_connection_pop_message(rdplib_connection_t *connection)
{
    rdplib_message_t *message = NULL;

    if (!connection)
    {
        return NULL;
    }
    message = connection->message_head;
    if (message)
    {
        connection->message_head = message->next;
        if (!connection->message_head)
        {
            connection->message_tail = NULL;
        }
        message->next = NULL;
    }
    return message;
}

int rdplib_connection_send(rdplib_connection_t *connection, const void *data, uint32_t bytes, uint32_t stream, uint32_t flags)
{
    int result;

    if (!connection)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    if (!connection->raw || connection->close_started || connection->peer_fin || connection->disconnected)
    {
        return RDPLIB_ERROR_NOT_USABLE;
    }
    result = connection_send(connection->raw, data, bytes, stream, flags);
    return result;
}

int rdplib_connection_begin_close(rdplib_connection_t *connection, uint32_t linger_timeout_ms)
{
    connection_t *raw;
    int result;

    if (!connection)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    if (connection->close_started)
    {
        return RDPLIB_OK;
    }
    if (!connection->raw)
    {
        return RDPLIB_ERROR_NOT_USABLE;
    }
    assert(connection->message_head == NULL);
    if (connection->message_head)
    {
        return RDPLIB_ERROR_BUSY;
    }
    raw = connection->raw;
    connection->close_started = 1;
    rdplib_connection_clear_packet_drop_callback(raw);
    result = connection_close(raw, linger_timeout_ms, NULL, NULL);
    connection->raw = NULL;
    return result;
}

int rdplib_connection_set_data_rate(rdplib_connection_t *connection, uint32_t bytes_per_second)
{
    connection_t *raw;
    int result = RDPLIB_OK;

    if (!connection || bytes_per_second == 0)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    raw = connection->raw;
    if (!raw || connection->close_started || connection->peer_fin || connection->disconnected)
    {
        return RDPLIB_ERROR_NOT_USABLE;
    }

    rdplib_platform_mutex_lock(&raw->lock);
    if (!raw->transmit.connected || raw->transmit.fin_sent || raw->transmit.transmit_stopped)
    {
        result = RDPLIB_ERROR_NOT_USABLE;
    }
    else
    {
        (void)connection_set_max_data_rate(raw, bytes_per_second);
    }
    rdplib_platform_mutex_unlock(&raw->lock);
    return result;
}

int rdplib_connection_set_send_buffer_size(rdplib_connection_t *connection, uint32_t bytes)
{
    connection_t *raw;
    int result = RDPLIB_OK;

    if (!connection)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    raw = connection->raw;
    if (!raw || connection->close_started || connection->peer_fin || connection->disconnected)
    {
        return RDPLIB_ERROR_NOT_USABLE;
    }

    rdplib_platform_mutex_lock(&raw->lock);
    if (!raw->transmit.connected || raw->transmit.fin_sent || raw->transmit.transmit_stopped)
    {
        result = RDPLIB_ERROR_NOT_USABLE;
    }
    else
    {
        connection_set_send_buffer_size(raw, bytes);
    }
    rdplib_platform_mutex_unlock(&raw->lock);
    return result;
}

static int rdplib_decode_ipv4_address(const uint8_t endpoint[16], uint8_t address[4], uint16_t *port)
{
    if (!endpoint || !address || !port || endpoint[0] != 2 || endpoint[1] != 0)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    memcpy(address, endpoint + 4, 4);
    *port = (uint16_t)(((uint16_t)endpoint[2] << 8) | endpoint[3]);
    return RDPLIB_OK;
}

int rdplib_connection_get_remote_ipv4(rdplib_connection_t *connection, uint8_t address[4], uint16_t *port)
{
    uint8_t endpoint[16];

    if (!connection || !address || !port)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    memcpy(endpoint, connection->remote_address, sizeof(endpoint));
    return rdplib_decode_ipv4_address(endpoint, address, port);
}

int rdplib_connection_get_perf_stats(rdplib_connection_t *connection, rdplib_connection_perf_stats_t *statistics)
{
    rdp_connection_perf_stats_t raw_statistics;

    if (!connection || !statistics)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    if (!connection->raw)
    {
        return RDPLIB_ERROR_NOT_USABLE;
    }
    connection_get_perf_stats(connection->raw, &raw_statistics);
    statistics->last_packet_receive_time_ms = raw_statistics.last_packet_receive_time_ms;
    statistics->received_packet_sequence_history = raw_statistics.received_packet_sequence_history;
    statistics->last_received_packet_sequence = raw_statistics.last_received_packet_sequence;
    statistics->rtt_mean_ms = raw_statistics.rtt_mean_ms;
    statistics->rtt_deviation_ms = raw_statistics.rtt_deviation_ms;
    statistics->last_ping_sample_ms = raw_statistics.last_ping_sample_ms;
    statistics->queued_reliable_bytes = raw_statistics.queued_reliable_bytes;
    statistics->transmit_stall_time_ms = raw_statistics.transmit_stall_time_ms;
    return RDPLIB_OK;
}

int rdplib_connection_get_disconnect_info(rdplib_connection_t *connection, rdplib_disconnect_info_t *information)
{
    rdp_connection_disconnect_info_t raw_information;

    if (!connection || !information)
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    if (!connection->raw)
    {
        return RDPLIB_ERROR_NOT_USABLE;
    }
    rdplib_platform_mutex_lock(&connection->raw->lock);
    connection_get_disconnect_info(connection->raw, &raw_information, sizeof(raw_information));
    rdplib_platform_mutex_unlock(&connection->raw->lock);
    information->reason = raw_information.reason;
    if (raw_information.reason == RDP_DISCONNECT_REASON_ICMP)
    {
        information->icmp_type = raw_information.icmp_type;
        information->icmp_code = raw_information.icmp_code;
        memcpy(information->icmp_source_ipv4, &raw_information.icmp_source_ipv4, sizeof(information->icmp_source_ipv4));
    }
    else
    {
        information->icmp_type = 0;
        information->icmp_code = 0;
        memset(information->icmp_source_ipv4, 0, sizeof(information->icmp_source_ipv4));
    }
    return RDPLIB_OK;
}

uint16_t rdplib_message_flags(const rdplib_message_t *message)
{
    return message ? message->flags : 0;
}

uint8_t rdplib_message_stream(const rdplib_message_t *message)
{
    return message ? message->stream_id : 0;
}

uint32_t rdplib_message_size(const rdplib_message_t *message)
{
    return message ? message->payload_bytes : 0;
}

const void *rdplib_message_data(const rdplib_message_t *message)
{
    if (!message || message->payload_bytes == 0)
    {
        return NULL;
    }
    return msg_arrival_get_data(message->arrival);
}

int rdplib_message_is_connectionless(const rdplib_message_t *message)
{
    return message && message->flags == UINT16_C(0xFFFF);
}

int rdplib_message_is_disconnect(const rdplib_message_t *message)
{
    return message && message->disconnect;
}

int rdplib_message_has_fin(const rdplib_message_t *message)
{
    return message && (message->flags & RDP_FLAG_FIN) != 0;
}

int rdplib_message_get_sender_ipv4(const rdplib_message_t *message, uint8_t address[4], uint16_t *port)
{
    if (!rdplib_message_is_connectionless(message))
    {
        return RDPLIB_ERROR_INVALID_ARGUMENT;
    }
    return rdplib_decode_ipv4_address(message->sender_address, address, port);
}

void rdplib_message_release(rdplib_message_t *message)
{
    if (!message)
    {
        return;
    }
    assert(message->arrival != NULL);
    assert(message->runtime != NULL);
    fast_free(message->arrival);
    rdplib_runtime_remove_outstanding(message->runtime);
    rdplib_platform_free(message);
}
