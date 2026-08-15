// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
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

int main(void)
{
    static const uint8_t payload[] = "zero-copy consumer message";
    rdplib_runtime_t *runtime = NULL;
    rdplib_endpoint_t *server = NULL;
    rdplib_endpoint_t *client = NULL;
    rdplib_connection_t *client_connection = NULL;
    rdplib_connection_t *server_connection = NULL;
    rdplib_message_t *server_message;
    rdplib_message_t *client_message;
    rdplib_connection_perf_stats_t statistics;
    rdplib_disconnect_info_t disconnect;
    uint8_t remote_address[4];
    uint16_t remote_port;
    packet_drop_state_t packet_drop = {UINT32_C(0x52445054), RDPLIB_PACKET_DROP_OUTBOUND};
    int saw_fin = 0;
    int attempts;

    assert(rdplib_runtime_create(&runtime, 1024u * 1024u) == RDPLIB_OK);
    assert(rdplib_endpoint_create(runtime, &server, 0, 8, RDP_CREATE_REQUIRE_IPV4) == RDPLIB_ERROR_INVALID_ARGUMENT);
    assert(server == NULL);
    assert(rdplib_endpoint_create(runtime, &server, 0, 8, RDPLIB_USE_CRC) == RDPLIB_ENDPOINT_CREATE_OK);
    assert(rdplib_endpoint_create(runtime, &client, 0, 1, RDPLIB_USE_CRC) == RDPLIB_ENDPOINT_CREATE_OK);
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
