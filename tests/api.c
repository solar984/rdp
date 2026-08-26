// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rdplib.h"

_Static_assert(RDPLIB_USE_ENCRYPTION == UINT32_C(0x80000000), "endpoint flags must retain their recovered uint32_t values");
_Static_assert(RDP_CONNECTION_FEATURE_KEEPALIVE == UINT32_C(1), "keepalive must retain the recovered option bit");
_Static_assert(RDPLIB_CONNECTION_SEND_BUFFER_FULL == 14, "normal API send results must expose the recovered transport value");
_Static_assert(RDP_SEND_ERROR_TOO_BIG == 18, "the recovered too-big send result must retain its historical name and value");
_Static_assert(RDPLIB_CONNECTION_SEND_PAYLOAD_TOO_LARGE == RDP_SEND_ERROR_TOO_BIG, "the maintained API name must map to the recovered result");
_Static_assert(RDPLIB_DISCONNECT_REASON_CONNECTION_INACTIVITY == 0x00050000, "normal API disconnect reasons must expose the recovered value");
_Static_assert(RDPLIB_STREAM_COUNT == 20, "normal API stream count must expose the recovered protocol limit");
_Static_assert(RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS == UINT32_C(10000), "the normal keepalive interval must start at the recovered value");
_Static_assert(RDPLIB_PACKET_DROP_BOTH == (RDPLIB_PACKET_DROP_INBOUND | RDPLIB_PACKET_DROP_OUTBOUND), "packet drop directions must be usable as a mask");
_Static_assert(RDPLIB_ERROR_PLATFORM == -6, "platform error must remain -6");
_Static_assert(sizeof(rdplib_endpoint_options_t) == 3u * sizeof(uint32_t), "endpoint options must contain 3 uint32_t values");
_Static_assert(sizeof(rdplib_endpoint_input_rate_t) == 2u * sizeof(uint32_t), "endpoint input rate size");
_Static_assert(offsetof(rdplib_endpoint_input_rate_t, bytes_per_second) == 0, "endpoint input rate prefix");
_Static_assert(sizeof(rdplib_connection_counters_t) == 61u * sizeof(uint32_t), "connection counters size");
_Static_assert(offsetof(rdplib_connection_counters_t, unreliable_packets_tx) == 0, "connection counters prefix");
_Static_assert(offsetof(rdplib_connection_counters_t, icmp_unknown) + sizeof(uint32_t) == sizeof(rdplib_connection_counters_t), "connection counters tail");
_Static_assert(_Generic(&rdplib_endpoint_get_input_rate, int (*)(const rdplib_endpoint_t *, rdplib_endpoint_input_rate_t *): 1, default: 0),
               "rdplib_endpoint_get_input_rate signature");
_Static_assert(_Generic(&rdplib_connection_get_counters, int (*)(rdplib_connection_t *, rdplib_connection_counters_t *): 1, default: 0),
               "rdplib_connection_get_counters signature");

enum
{
    TEST_SOCKET_BUFFER_BYTES = 64u * 1024u,
    TEST_RUNTIME_SOCKET_BUFFER_BYTES = 96u * 1024u
};

typedef struct packet_drop_state_t
{
    uint32_t marker;
    uint32_t drop_directions;
} packet_drop_state_t;

static int packet_drop_callback(void *context, rdplib_packet_drop_direction_t direction, const uint8_t *packet, uint32_t packet_bytes)
{
    const packet_drop_state_t *state = (const packet_drop_state_t *)context;

    assert(state != NULL && state->marker == UINT32_C(0x52445054));
    assert(direction == RDPLIB_PACKET_DROP_INBOUND || direction == RDPLIB_PACKET_DROP_OUTBOUND);
    assert(packet != NULL);
    assert(packet_bytes >= 4);
    return (state->drop_directions & (uint32_t)direction) != 0;
}

static int socket_buffer_satisfies_request(uint32_t actual_bytes, uint32_t requested_bytes)
{
#ifdef __linux__
    return (uint64_t)actual_bytes >= 2u * (uint64_t)requested_bytes;
#else
    return actual_bytes >= requested_bytes;
#endif
}

int main(void)
{
    static const uint8_t payload[] = "zero-copy consumer message";
    rdplib_runtime_t *runtime = NULL;
    rdplib_endpoint_t *default_endpoint = NULL;
    rdplib_endpoint_t *server = NULL;
    rdplib_endpoint_t *client = NULL;
    rdplib_connection_t *client_connection = NULL;
    rdplib_connection_t *server_connection = NULL;
    rdplib_message_t *server_message;
    rdplib_message_t *client_message;
    rdplib_connection_perf_stats_t statistics;
    rdplib_connection_counters_t counters;
    rdplib_connection_counters_t counters_before;
    rdplib_disconnect_info_t disconnect;
    rdplib_endpoint_options_t endpoint_options;
    rdplib_endpoint_input_rate_t input_rate;
    rdplib_endpoint_input_rate_t input_rate_before;
    uint32_t default_receive_socket_buffer_bytes;
    uint32_t default_send_socket_buffer_bytes;
    uint32_t receive_socket_buffer_bytes;
    uint32_t send_socket_buffer_bytes;
    uint8_t remote_address[4];
    uint16_t remote_port;
    packet_drop_state_t packet_drop = {UINT32_C(0x52445054), RDPLIB_PACKET_DROP_OUTBOUND};
    int saw_fin = 0;
    int attempts;

    memset(&endpoint_options, 0, sizeof(endpoint_options));
    endpoint_options.structure_size = sizeof(endpoint_options);
    endpoint_options.receive_socket_buffer_bytes = TEST_SOCKET_BUFFER_BYTES;
    endpoint_options.send_socket_buffer_bytes = TEST_SOCKET_BUFFER_BYTES;

    assert(rdplib_endpoint_create_ex(NULL, &server, 0, 8, RDPLIB_USE_CRC, &endpoint_options) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(server == NULL);
    assert(rdplib_runtime_create(&runtime, 1024u * 1024u) == RDPLIB_OK);
    assert(rdplib_endpoint_create(runtime, &server, 0, 8, RDP_CREATE_REQUIRE_IPV4) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(server == NULL);

    endpoint_options.structure_size = sizeof(endpoint_options) - 1u;
    assert(rdplib_endpoint_create_ex(runtime, &server, 0, 8, RDPLIB_USE_CRC, &endpoint_options) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(server == NULL);
    endpoint_options.structure_size = sizeof(endpoint_options);
    endpoint_options.receive_socket_buffer_bytes = UINT32_MAX;
    assert(rdplib_endpoint_create_ex(runtime, &server, 0, 8, RDPLIB_USE_CRC, &endpoint_options) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(server == NULL);

    endpoint_options.receive_socket_buffer_bytes = 0;
    endpoint_options.send_socket_buffer_bytes = 0;
    assert(rdplib_endpoint_create_ex(runtime, &default_endpoint, 0, 1, RDPLIB_USE_CRC, &endpoint_options) == RDPLIB_ENDPOINT_CREATE_OK);
    assert(rdplib_endpoint_get_socket_receive_buffer_size(default_endpoint, &default_receive_socket_buffer_bytes) == RDPLIB_OK);
    assert(rdplib_endpoint_get_socket_send_buffer_size(default_endpoint, &default_send_socket_buffer_bytes) == RDPLIB_OK);
    assert(default_receive_socket_buffer_bytes != 0);
    assert(default_send_socket_buffer_bytes != 0);
    assert(rdplib_endpoint_destroy(default_endpoint) == RDPLIB_OK);
    default_endpoint = NULL;

    endpoint_options.receive_socket_buffer_bytes = TEST_SOCKET_BUFFER_BYTES;
    endpoint_options.send_socket_buffer_bytes = TEST_SOCKET_BUFFER_BYTES;
    assert(rdplib_endpoint_create_ex(runtime, &server, 0, 8, RDPLIB_USE_CRC, &endpoint_options) == RDPLIB_ENDPOINT_CREATE_OK);

    memset(&input_rate, 0x5A, sizeof(input_rate));
    input_rate_before = input_rate;
    assert(rdplib_endpoint_get_input_rate(NULL, &input_rate) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(memcmp(&input_rate, &input_rate_before, sizeof(input_rate)) == 0);
    assert(rdplib_endpoint_get_input_rate(server, NULL) == RDPLIB_ERROR_INVALID_ARGUMENT);
    memset(&input_rate, 0x5A, sizeof(input_rate));
    assert(rdplib_endpoint_get_input_rate(server, &input_rate) == RDPLIB_OK);
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(input_rate.bytes_per_second == 0);
#endif
    assert(input_rate.duplicate_reliable_bytes_per_second == 0);

    assert(rdplib_endpoint_get_socket_receive_buffer_size(NULL, &receive_socket_buffer_bytes) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_get_socket_receive_buffer_size(server, NULL) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_get_socket_send_buffer_size(NULL, &send_socket_buffer_bytes) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_get_socket_send_buffer_size(server, NULL) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_get_socket_receive_buffer_size(server, &receive_socket_buffer_bytes) == RDPLIB_OK);
    assert(rdplib_endpoint_get_socket_send_buffer_size(server, &send_socket_buffer_bytes) == RDPLIB_OK);
    assert(socket_buffer_satisfies_request(receive_socket_buffer_bytes, TEST_SOCKET_BUFFER_BYTES));
    assert(socket_buffer_satisfies_request(send_socket_buffer_bytes, TEST_SOCKET_BUFFER_BYTES));
    assert(rdplib_endpoint_set_socket_receive_buffer_size(NULL, TEST_RUNTIME_SOCKET_BUFFER_BYTES) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_set_socket_receive_buffer_size(server, 0) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_set_socket_receive_buffer_size(server, UINT32_MAX) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_set_socket_send_buffer_size(NULL, TEST_RUNTIME_SOCKET_BUFFER_BYTES) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_set_socket_send_buffer_size(server, 0) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_set_socket_send_buffer_size(server, UINT32_MAX) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_endpoint_set_socket_receive_buffer_size(server, TEST_RUNTIME_SOCKET_BUFFER_BYTES) == RDPLIB_OK);
    assert(rdplib_endpoint_set_socket_send_buffer_size(server, TEST_RUNTIME_SOCKET_BUFFER_BYTES) == RDPLIB_OK);
    assert(rdplib_endpoint_get_socket_receive_buffer_size(server, &receive_socket_buffer_bytes) == RDPLIB_OK);
    assert(rdplib_endpoint_get_socket_send_buffer_size(server, &send_socket_buffer_bytes) == RDPLIB_OK);
    assert(socket_buffer_satisfies_request(receive_socket_buffer_bytes, TEST_RUNTIME_SOCKET_BUFFER_BYTES));
    assert(socket_buffer_satisfies_request(send_socket_buffer_bytes, TEST_RUNTIME_SOCKET_BUFFER_BYTES));

    assert(rdplib_endpoint_create(runtime, &client, 0, 1, RDPLIB_USE_CRC) == RDPLIB_ENDPOINT_CREATE_OK);
    assert(rdplib_endpoint_get_socket_receive_buffer_size(client, &receive_socket_buffer_bytes) == RDPLIB_OK);
    assert(rdplib_endpoint_get_socket_send_buffer_size(client, &send_socket_buffer_bytes) == RDPLIB_OK);
    assert(receive_socket_buffer_bytes == default_receive_socket_buffer_bytes);
    assert(send_socket_buffer_bytes == default_send_socket_buffer_bytes);
    assert(rdplib_connect(client, &client_connection, "127.0.0.1", rdplib_endpoint_local_port(server)) == RDPLIB_CONNECT_OK);
    assert(rdplib_connection_enable_keepalive(NULL) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_connection_enable_keepalive_with_interval(NULL, 500) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_connection_enable_keepalive_with_interval(client_connection, 0) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_connection_enable_keepalive_with_interval(client_connection, UINT32_MAX) == RDPLIB_ERROR_INVALID_ARGUMENT);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(rdplib_connection_enable_keepalive_with_interval(client_connection, 500) == RDPLIB_ERROR_NOT_SUPPORTED);
#else
    assert(rdplib_connection_enable_keepalive_with_interval(client_connection, 500) == RDPLIB_OK);
#endif
    assert(rdplib_connection_enable_keepalive(client_connection) == RDPLIB_OK);
    assert(rdplib_connection_set_packet_drop_callback(NULL, packet_drop_callback, &packet_drop) == RDPLIB_ERROR_INVALID_ARGUMENT);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(rdplib_connection_set_packet_drop_callback(client_connection, packet_drop_callback, &packet_drop) == RDPLIB_ERROR_NOT_SUPPORTED);
#else
    assert(rdplib_connection_set_packet_drop_callback(client_connection, packet_drop_callback, &packet_drop) == RDPLIB_OK);
    assert(rdplib_connection_set_packet_drop_callback(client_connection, NULL, &packet_drop) == RDPLIB_OK);
    assert(rdplib_connection_set_packet_drop_callback(client_connection, packet_drop_callback, &packet_drop) == RDPLIB_OK);
#endif
    assert(rdplib_connection_set_data_rate(NULL, 5120) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_connection_set_data_rate(client_connection, 0) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(rdplib_connection_set_data_rate(client_connection, 5120) == RDPLIB_OK);
    assert(rdplib_connection_set_send_buffer_size(client_connection, 262144) == RDPLIB_OK);
    assert(rdplib_connection_send(client_connection, payload, (uint32_t)sizeof(payload), 1, RDPLIB_SEND_RELIABLE) == RDPLIB_CONNECTION_SEND_OK);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(rdplib_endpoint_process(server, 300) >= 0);
    assert(rdplib_endpoint_accept(server) == NULL);
    assert(rdplib_connection_set_packet_drop_callback(client_connection, NULL, NULL) == RDPLIB_OK);
    packet_drop.drop_directions = 0;
#endif
    assert(rdplib_endpoint_process(server, 5000) > 0);
    server_connection = rdplib_endpoint_accept(server);
    assert(server_connection != NULL);
    assert(rdplib_connection_enable_keepalive(server_connection) == RDPLIB_OK);
    assert(rdplib_connection_enable_keepalive(server_connection) == RDPLIB_OK);
    assert(rdplib_connection_set_data_rate(server_connection, 5120) == RDPLIB_OK);
    assert(rdplib_connection_get_remote_ipv4(server_connection, remote_address, &remote_port) == RDPLIB_OK);
    assert(remote_address[0] == 127 && remote_address[1] == 0 && remote_address[2] == 0 && remote_address[3] == 1);

    server_message = rdplib_connection_pop_message(server_connection);
    assert(server_message != NULL);
    assert(rdplib_message_stream(server_message) == 1);
    assert(rdplib_message_size(server_message) == sizeof(payload));
    assert(memcmp(rdplib_message_data(server_message), payload, sizeof(payload)) == 0);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    packet_drop.drop_directions = RDPLIB_PACKET_DROP_INBOUND;
    assert(rdplib_connection_set_packet_drop_callback(client_connection, packet_drop_callback, &packet_drop) == RDPLIB_OK);
#endif
    assert(rdplib_connection_send(server_connection, rdplib_message_data(server_message), rdplib_message_size(server_message), 1, RDPLIB_SEND_RELIABLE) == RDPLIB_CONNECTION_SEND_OK);
    rdplib_message_release(server_message);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    for (attempts = 0; attempts < 3; ++attempts)
    {
        assert(rdplib_endpoint_process(client, 100) >= 0);
    }
    assert(rdplib_connection_pop_message(client_connection) == NULL);
    assert(rdplib_connection_set_packet_drop_callback(client_connection, NULL, NULL) == RDPLIB_OK);
    packet_drop.drop_directions = 0;
#endif
    assert(rdplib_endpoint_process(client, 5000) > 0);
    client_message = rdplib_connection_pop_message(client_connection);
    assert(client_message != NULL);
    assert(rdplib_message_size(client_message) == sizeof(payload));
    assert(memcmp(rdplib_message_data(client_message), payload, sizeof(payload)) == 0);
    rdplib_message_release(client_message);

    memset(&counters, 0x5A, sizeof(counters));
    counters_before = counters;
    assert(rdplib_connection_get_counters(NULL, &counters) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(memcmp(&counters, &counters_before, sizeof(counters)) == 0);
    assert(rdplib_connection_get_counters(client_connection, NULL) == RDPLIB_ERROR_INVALID_ARGUMENT);

    memset(&counters, 0, sizeof(counters));
    assert(rdplib_connection_get_counters(client_connection, &counters) == RDPLIB_OK);
    assert(counters.reliable_packets_tx != 0 && counters.reliable_bytes_tx != 0);
    assert(counters.reliable_packets_rx != 0 && counters.reliable_bytes_rx != 0);

    assert(rdplib_connection_get_perf_stats(client_connection, &statistics) == RDPLIB_OK);
    assert(rdplib_connection_get_disconnect_info(client_connection, &disconnect) == RDPLIB_OK);
    assert(disconnect.reason == 0);
    assert(disconnect.icmp_type == 0);
    assert(disconnect.icmp_code == 0);
    assert(disconnect.icmp_source_ipv4[0] == 0 && disconnect.icmp_source_ipv4[1] == 0 && disconnect.icmp_source_ipv4[2] == 0 && disconnect.icmp_source_ipv4[3] == 0);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    packet_drop.drop_directions = RDPLIB_PACKET_DROP_OUTBOUND;
    assert(rdplib_connection_set_packet_drop_callback(client_connection, packet_drop_callback, &packet_drop) == RDPLIB_OK);
#endif
    assert(rdplib_connection_begin_close(client_connection, 1000) == RDPLIB_CONNECTION_SEND_OK);

    memset(&counters, 0x5A, sizeof(counters));
    counters_before = counters;
    assert(rdplib_connection_get_counters(client_connection, &counters) == RDPLIB_ERROR_NOT_USABLE);
    assert(memcmp(&counters, &counters_before, sizeof(counters)) == 0);
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    // Close must remove the callback before the application can release its context.  The FIN below also proves that outbound dropping is no longer active.
    packet_drop.marker = 0;
#endif
    assert(!rdplib_connection_is_usable(client_connection));
    assert(rdplib_connection_enable_keepalive(client_connection) == RDPLIB_ERROR_NOT_USABLE);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(rdplib_connection_enable_keepalive_with_interval(client_connection, 500) == RDPLIB_ERROR_NOT_SUPPORTED);
#else
    assert(rdplib_connection_enable_keepalive_with_interval(client_connection, 500) == RDPLIB_ERROR_NOT_USABLE);
#endif
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(rdplib_connection_set_packet_drop_callback(client_connection, packet_drop_callback, &packet_drop) == RDPLIB_ERROR_NOT_SUPPORTED);
#else
    assert(rdplib_connection_set_packet_drop_callback(client_connection, packet_drop_callback, &packet_drop) == RDPLIB_ERROR_NOT_USABLE);
#endif
    assert(rdplib_connection_set_data_rate(client_connection, 5120) == RDPLIB_ERROR_NOT_USABLE);
    assert(rdplib_connection_set_send_buffer_size(client_connection, 262144) == RDPLIB_ERROR_NOT_USABLE);
    for (attempts = 0; attempts < 10 && !saw_fin; ++attempts)
    {
        assert(rdplib_endpoint_process(server, 500) >= 0);
        while ((server_message = rdplib_connection_pop_message(server_connection)) != NULL)
        {
            saw_fin = saw_fin || rdplib_message_has_fin(server_message);
            rdplib_message_release(server_message);
        }
    }
    assert(saw_fin);
    memset(&counters, 0, sizeof(counters));
    assert(rdplib_connection_get_counters(server_connection, &counters) == RDPLIB_OK);
    assert(rdplib_connection_enable_keepalive(server_connection) == RDPLIB_ERROR_NOT_USABLE);
    assert(rdplib_endpoint_destroy(client) == RDPLIB_ERROR_BUSY);
    assert(rdplib_endpoint_destroy(server) == RDPLIB_ERROR_BUSY);

    rdplib_connection_release(client_connection);
    // Peer terminal release performs immediate cleanup without a second close call.
    rdplib_connection_release(server_connection);
    assert(rdplib_endpoint_destroy(client) == RDPLIB_OK);
    assert(rdplib_endpoint_destroy(server) == RDPLIB_OK);
    assert(rdplib_runtime_destroy(runtime) == RDPLIB_OK);
    return 0;
}
