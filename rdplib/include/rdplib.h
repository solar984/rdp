// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Normal application API for rdplib.
#ifndef RDPLIB_H
#define RDPLIB_H

#include <stdint.h>

#include "rdplib_constants.h"

#if defined(RDPLIB_SHARED_BUILD)
#if defined(_WIN32)
#define RDPLIB_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define RDPLIB_API __attribute__((visibility("default")))
#else
#define RDPLIB_API
#endif
#elif defined(_WIN32) && defined(RDPLIB_SHARED)
#define RDPLIB_API __declspec(dllimport)
#else
#define RDPLIB_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct rdplib_runtime_t rdplib_runtime_t;
typedef struct rdplib_endpoint_t rdplib_endpoint_t;
typedef struct rdplib_connection_t rdplib_connection_t;
typedef struct rdplib_message_t rdplib_message_t;

// Return nonzero to discard a connected RDP datagram as if it were lost.
// The packet is borrowed and valid only during the callback.
typedef int (*rdplib_packet_drop_callback_t)(void *context, rdplib_packet_drop_direction_t direction, const uint8_t *packet, uint32_t packet_bytes);

typedef struct rdplib_connection_perf_stats_t
{
    uint32_t last_packet_receive_time_ms;
    uint64_t received_packet_sequence_history;
    uint16_t last_received_packet_sequence;
    uint32_t rtt_mean_ms;
    uint32_t rtt_deviation_ms;
    uint32_t last_ping_sample_ms;
    uint32_t queued_reliable_bytes;
    uint32_t transmit_stall_time_ms;
} rdplib_connection_perf_stats_t;

typedef struct rdplib_disconnect_info_t
{
    uint32_t reason;
    uint8_t icmp_type;
    uint8_t icmp_code;
    uint8_t icmp_source_ipv4[4];
} rdplib_disconnect_info_t;

// A runtime sets up the allocator and statistics used by every endpoint.
RDPLIB_API int rdplib_runtime_create(rdplib_runtime_t **output, uint32_t fast_allocator_bytes);

// Release every endpoint and original received message before the runtime.
RDPLIB_API int rdplib_runtime_destroy(rdplib_runtime_t *runtime);

// Normal endpoints are always IPv4.  Flags may contain RDPLIB_USE_CRC and RDPLIB_USE_ENCRYPTION.
RDPLIB_API int rdplib_endpoint_create(rdplib_runtime_t *runtime, rdplib_endpoint_t **output, uint16_t local_port, uint32_t expected_connections, uint32_t flags);

// Use an endpoint and all of its connections from a single application thread.  Destroy returns busy until every application connection handle has been released.
RDPLIB_API int rdplib_endpoint_destroy(rdplib_endpoint_t *endpoint);
RDPLIB_API uint16_t rdplib_endpoint_local_port(const rdplib_endpoint_t *endpoint);

// Wait for an arrival, then move everything currently ready to the application queues.
RDPLIB_API int rdplib_endpoint_process(rdplib_endpoint_t *endpoint, int32_t timeout_ms);

// Returns a handle owned by the caller, or NULL.  Release it exactly once when finished.
RDPLIB_API rdplib_connection_t *rdplib_endpoint_accept(rdplib_endpoint_t *endpoint);

// Connectionless arrivals do not create connection handles.
RDPLIB_API rdplib_message_t *rdplib_endpoint_pop_connectionless(rdplib_endpoint_t *endpoint);

// Configure keepalive, data rate, and buffer size explicitly after connecting.
RDPLIB_API int rdplib_connect(rdplib_endpoint_t *endpoint, rdplib_connection_t **output, const char *host, uint16_t port);

// Drain messages before releasing a connection.  Start close first when the
// peer should receive FIN.  Releasing an already dead connection cleans it up
// immediately.
RDPLIB_API void rdplib_connection_release(rdplib_connection_t *connection);
RDPLIB_API int rdplib_connection_is_usable(rdplib_connection_t *connection);

// Enable reliable keepalives using the current interval, which starts at RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS.  Calls may be repeated.
RDPLIB_API int rdplib_connection_enable_keepalive(rdplib_connection_t *connection);

// Set and enable a per connection interval in the normal build.  The source faithful build returns RDPLIB_ERROR_NOT_SUPPORTED.
RDPLIB_API int rdplib_connection_enable_keepalive_with_interval(rdplib_connection_t *connection, uint32_t interval_ms);

// Install or replace the normal build's packet drop callback.  A null
// callback removes it and waits for an already running callback to finish.  The
// callback runs under the connection lock, either during an application send
// or on the I/O thread, and must not reenter this connection.
// The source faithful build returns RDPLIB_ERROR_NOT_SUPPORTED.
RDPLIB_API int rdplib_connection_set_packet_drop_callback(rdplib_connection_t *connection, rdplib_packet_drop_callback_t callback, void *context);

// Returns a message owned by the caller, or NULL.  Release every returned message.
RDPLIB_API rdplib_message_t *rdplib_connection_pop_message(rdplib_connection_t *connection);

// Each transmit direction is established by its first reliable message.  The default build holds an earlier unreliable message until that reliable message is acknowledged.
RDPLIB_API int rdplib_connection_send(rdplib_connection_t *connection, const void *data, uint32_t bytes, uint32_t stream, uint32_t flags);

// Drain queued messages first.  Starts close and returns immediately.  Calls may be repeated.
RDPLIB_API int rdplib_connection_begin_close(rdplib_connection_t *connection, uint32_t linger_timeout_ms);

// The data rate must be greater than 0.
RDPLIB_API int rdplib_connection_set_data_rate(rdplib_connection_t *connection, uint32_t bytes_per_second);
RDPLIB_API int rdplib_connection_set_send_buffer_size(rdplib_connection_t *connection, uint32_t bytes);
RDPLIB_API int rdplib_connection_get_remote_ipv4(rdplib_connection_t *connection, uint8_t address[4], uint16_t *port);
RDPLIB_API int rdplib_connection_get_perf_stats(rdplib_connection_t *connection, rdplib_connection_perf_stats_t *statistics);
RDPLIB_API int rdplib_connection_get_disconnect_info(rdplib_connection_t *connection, rdplib_disconnect_info_t *information);

RDPLIB_API uint16_t rdplib_message_flags(const rdplib_message_t *message);
RDPLIB_API uint8_t rdplib_message_stream(const rdplib_message_t *message);
RDPLIB_API uint32_t rdplib_message_size(const rdplib_message_t *message);
RDPLIB_API const void *rdplib_message_data(const rdplib_message_t *message);
RDPLIB_API int rdplib_message_is_connectionless(const rdplib_message_t *message);
RDPLIB_API int rdplib_message_is_disconnect(const rdplib_message_t *message);
RDPLIB_API int rdplib_message_has_fin(const rdplib_message_t *message);
RDPLIB_API int rdplib_message_get_sender_ipv4(const rdplib_message_t *message, uint8_t address[4], uint16_t *port);

RDPLIB_API void rdplib_message_release(rdplib_message_t *message);

#ifdef __cplusplus
}
#endif

#endif // RDPLIB_H
