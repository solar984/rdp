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

typedef struct rdplib_endpoint_options_t
{
    uint32_t structure_size;
    uint32_t receive_socket_buffer_bytes;
    uint32_t send_socket_buffer_bytes;
} rdplib_endpoint_options_t;

typedef struct rdplib_endpoint_input_rate_t
{
    uint32_t bytes_per_second;
    uint32_t duplicate_reliable_bytes_per_second;
} rdplib_endpoint_input_rate_t;

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

typedef struct rdplib_connection_counters_t
{
    uint32_t unreliable_packets_tx;
    uint32_t unreliable_bytes_tx;
    uint32_t reliable_packets_tx;
    uint32_t reliable_bytes_tx;
    uint32_t reliable_packets_retransmitted;
    uint32_t reliable_bytes_retransmitted;
    uint32_t ack_only_packets_tx;
    uint32_t ack_and_data_packets_tx;
    uint32_t unreliable_packets_rx;
    uint32_t unreliable_bytes_rx;
    uint32_t reliable_packets_rx;
    uint32_t reliable_bytes_rx;
    uint32_t duplicate_reliable_packets_rx;
    uint32_t duplicate_reliable_bytes_rx;
    uint32_t header_bytes_rx;
    uint32_t ack_only_packets_rx;
    uint32_t ack_and_data_packets_rx;
    uint32_t messages_acked;
    uint32_t duplicate_acks;
    uint32_t bytes_in_duplicate_acks;
    uint32_t acks_for_unsent_messages;
    uint32_t packets_rx_in_sequence;
    uint32_t bytes_rx_in_sequence;
    uint32_t packets_rx_out_of_sequence;
    uint32_t bytes_rx_out_of_sequence;
    uint32_t discarded_bad_options;
    uint32_t discarded_old_seqnum;
    uint32_t discarded_dup_seqnum;
    uint32_t discarded_old_msgid;
    uint32_t discarded_bad_fragment;
    uint32_t discarded_bad_stream;
    uint32_t discarded_too_short;
    uint32_t discarded_bad_fragment_size;
    uint32_t discarded_bad_ack_header;
    uint32_t discarded_bad_ackmask;
    uint32_t discarded_mask_wo_ack;
    uint32_t discarded_old_ack;
    uint32_t packets_updated_rtt;
    uint32_t packets_updated_rtt_attempts;
    uint32_t icmp_unreachable[16];
    uint32_t icmp_source_quench;
    uint32_t icmp_ttl_expired[2];
    uint32_t icmp_parameter_problem[2];
    uint32_t icmp_unknown;
} rdplib_connection_counters_t;

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

// Normal endpoints are always IPv4.  Flags may contain RDPLIB_USE_CRC and RDPLIB_USE_ENCRYPTION.  Socket buffers retain the operating system defaults.
RDPLIB_API int rdplib_endpoint_create(rdplib_runtime_t *runtime, rdplib_endpoint_t **output, uint16_t local_port, uint32_t expected_connections, uint32_t flags);

// Options may be null.  Set structure_size to sizeof(rdplib_endpoint_options_t).  A socket buffer value of 0 retains the operating system default.
RDPLIB_API int rdplib_endpoint_create_ex(rdplib_runtime_t *runtime, rdplib_endpoint_t **output, uint16_t local_port, uint32_t expected_connections, uint32_t flags,
                                         const rdplib_endpoint_options_t *options);

// Use an endpoint and all of its connections from a single application thread.  Destroy returns busy until every application connection handle has been released.
RDPLIB_API int rdplib_endpoint_destroy(rdplib_endpoint_t *endpoint);
RDPLIB_API uint16_t rdplib_endpoint_local_port(const rdplib_endpoint_t *endpoint);
RDPLIB_API int rdplib_endpoint_set_socket_receive_buffer_size(rdplib_endpoint_t *endpoint, uint32_t bytes);
RDPLIB_API int rdplib_endpoint_set_socket_send_buffer_size(rdplib_endpoint_t *endpoint, uint32_t bytes);
RDPLIB_API int rdplib_endpoint_get_socket_receive_buffer_size(const rdplib_endpoint_t *endpoint, uint32_t *bytes);
RDPLIB_API int rdplib_endpoint_get_socket_send_buffer_size(const rdplib_endpoint_t *endpoint, uint32_t *bytes);

RDPLIB_API int rdplib_endpoint_get_input_rate(const rdplib_endpoint_t *endpoint, rdplib_endpoint_input_rate_t *input_rate);

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

// Enable reliable keepalives after this direction has sent its first reliable SYN.  The current interval starts at RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS.  Calls may be repeated.
RDPLIB_API int rdplib_connection_enable_keepalive(rdplib_connection_t *connection);

// Set and enable a per connection interval in the normal build.  Keepalive does not establish an idle direction.  The source faithful build returns RDPLIB_ERROR_NOT_SUPPORTED.
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
RDPLIB_API int rdplib_connection_get_counters(rdplib_connection_t *connection, rdplib_connection_counters_t *counters);
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
