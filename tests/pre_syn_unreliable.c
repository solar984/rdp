// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

#include "rdplib.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

static rdplib_message_t *wait_for_message(rdplib_endpoint_t *endpoint, rdplib_connection_t *connection)
{
    int attempts;

    for (attempts = 0; attempts < 10; ++attempts)
    {
        rdplib_message_t *message = rdplib_connection_pop_message(connection);
        if (message)
        {
            return message;
        }

        assert(rdplib_endpoint_process(endpoint, 500) >= 0);
    }
    return rdplib_connection_pop_message(connection);
}

static void check_payload(rdplib_message_t *message, const uint8_t *expected, uint32_t bytes, uint16_t required_flags, uint16_t forbidden_flags)
{
    assert(message != NULL);
    assert((rdplib_message_flags(message) & required_flags) == required_flags);
    assert((rdplib_message_flags(message) & forbidden_flags) == 0);
    assert(rdplib_message_stream(message) == 1);
    assert(rdplib_message_size(message) == bytes);
    assert(memcmp(rdplib_message_data(message), expected, bytes) == 0);
    rdplib_message_release(message);
}

int main(void)
{
    static const uint8_t request[] = {1, 2, 3, 4};
    static const uint8_t queued_unreliable[] = {5, 6, 7, 8};
    static const uint8_t reliable_bootstrap[] = {9, 10, 11, 12};
    static const uint8_t live_unreliable[] = {13, 14, 15, 16};
    rdplib_runtime_t *runtime = NULL;
    rdplib_endpoint_t *server = NULL;
    rdplib_endpoint_t *client = NULL;
    rdplib_connection_t *server_connection = NULL;
    rdplib_connection_t *client_connection = NULL;
    rdplib_message_t *message;
    int attempts;
    int saw_fin = 0;

#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    assert(rdplib_runtime_create(&runtime, 1024u * 1024u) == RDPLIB_OK);
    assert(rdplib_endpoint_create(runtime, &server, 0, 8, RDPLIB_USE_CRC) == RDPLIB_OK);
    assert(rdplib_endpoint_create(runtime, &client, 0, 1, RDPLIB_USE_CRC) == RDPLIB_OK);
    assert(rdplib_connect(client, &client_connection, "127.0.0.1", rdplib_endpoint_local_port(server)) == RDPLIB_OK);

    assert(rdplib_connection_send(client_connection, request, sizeof(request), 1, RDPLIB_SEND_RELIABLE) == RDPLIB_OK);
    assert(rdplib_endpoint_process(server, 5000) > 0);
    server_connection = rdplib_endpoint_accept(server);
    assert(server_connection != NULL);
    message = rdplib_connection_pop_message(server_connection);
    check_payload(message, request, sizeof(request), RDP_FLAG_SYN | RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, RDP_FLAG_SYSTEM);

    assert(rdplib_connection_send(server_connection, queued_unreliable, sizeof(queued_unreliable), 1, RDPLIB_SEND_UNRELIABLE) == RDPLIB_OK);
    assert(rdplib_connection_enable_keepalive_with_interval(server_connection, 20) == RDPLIB_OK);
    for (attempts = 0; attempts < 4; ++attempts)
    {
        assert(rdplib_endpoint_process(client, 100) >= 0);
        assert(rdplib_connection_pop_message(client_connection) == NULL);
    }
    assert(rdplib_connection_enable_keepalive_with_interval(server_connection, RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS) == RDPLIB_OK);

    // Neither the unreliable message nor keepalive can establish this
    // direction. The first reliable application message carries SYN and must
    // not block behind the earlier unreliable message.
    assert(rdplib_connection_send(server_connection, reliable_bootstrap, sizeof(reliable_bootstrap), 1, RDPLIB_SEND_RELIABLE) == RDPLIB_OK);
    message = wait_for_message(client, client_connection);
    check_payload(message, reliable_bootstrap, sizeof(reliable_bootstrap), RDP_FLAG_SYN | RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, RDP_FLAG_SYSTEM);
    message = wait_for_message(client, client_connection);
    check_payload(message, queued_unreliable, sizeof(queued_unreliable), RDP_FLAG_SEQUENCED, RDP_FLAG_SYN | RDP_FLAG_MSGID | RDP_FLAG_SYSTEM);

    assert(rdplib_connection_send(server_connection, live_unreliable, sizeof(live_unreliable), 1, RDPLIB_SEND_UNRELIABLE) == RDPLIB_OK);
    message = wait_for_message(client, client_connection);
    check_payload(message, live_unreliable, sizeof(live_unreliable), RDP_FLAG_SEQUENCED, RDP_FLAG_SYN | RDP_FLAG_MSGID | RDP_FLAG_SYSTEM);

    assert(rdplib_connection_begin_close(client_connection, 1000) == RDPLIB_OK);
    for (attempts = 0; attempts < 10 && !saw_fin; ++attempts)
    {
        assert(rdplib_endpoint_process(server, 500) >= 0);
        while ((message = rdplib_connection_pop_message(server_connection)) != NULL)
        {
            saw_fin = saw_fin || rdplib_message_has_fin(message);
            rdplib_message_release(message);
        }
    }
    assert(saw_fin);

    rdplib_connection_release(client_connection);
    rdplib_connection_release(server_connection);
    assert(rdplib_endpoint_destroy(client) == RDPLIB_OK);
    assert(rdplib_endpoint_destroy(server) == RDPLIB_OK);
    assert(rdplib_runtime_destroy(runtime) == RDPLIB_OK);
    return 0;
}
