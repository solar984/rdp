// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"
#include <stdint.h>
#include <string.h>
#include <threads.h>

#include "rdp.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

static void check_public_api_signatures(void)
{
    void **(*app_ptr)(connection_t *) = connection_app_ptr;
    struct sockaddr *(*remote_addr)(connection_t *) = connection_get_remote_addr;
    void (*perf_stats)(connection_t *, perf_stats_t *) = connection_get_perf_stats;

    (void)app_ptr;
    (void)remote_addr;
    (void)perf_stats;
}

typedef struct close_thread_context_t
{
    connection_t *connection;
    uint32_t call_result;
    uint32_t clean_result;
} close_thread_context_t;

static int close_connection_thread(void *argument)
{
    close_thread_context_t *context = (close_thread_context_t *)argument;

    context->call_result = connection_close_wait(context->connection, 1000, &context->clean_result);
    return 0;
}

static void run_scenario(uint32_t create_flags)
{
    uint8_t payload[1300];
    rdp_t *server = NULL;
    rdp_t *client = NULL;
    rdp_t *conflict = NULL;
    connection_t *client_connection = NULL;
    msg_arrival_t *server_message;
    msg_arrival_t *client_message;
    close_thread_context_t client_close;
    thrd_t client_close_thread;
    uint32_t server_close_result;
    int thread_result;
    uint32_t index;

    for (index = 0; index < sizeof(payload); ++index)
    {
        payload[index] = (uint8_t)(index * 37u + 11u);
    }

    assert(rdp_create(&server, 0, 8, create_flags) == 0);
    assert(rdp_create(&conflict, ntohs(server->local_udp_addr.sin_port), 1, RDP_CREATE_REQUIRE_IPV4) == 11);
    assert(conflict == NULL);
    assert(rdp_create(&client, 0, 1, create_flags) == 0);
    assert(server->local_udp_addr.sin_port != 0);
    assert(rdp_connect(client, &client_connection, "127.0.0.1", ntohs(server->local_udp_addr.sin_port), 0) == 0);
    assert(connection_set_max_data_rate(client_connection, 64000) == 3000);
    connection_set_send_buffer_size(client_connection, 262144);
    assert(connection_send(client_connection, payload, sizeof(payload), 1, RDP_SEND_RELIABLE) == 0);

    server_message = rdp_receive(server, 5000);
    assert(server_message != NULL);
    assert(msg_arrival_get_sender(server_message) != NULL);
    assert(connection_set_max_data_rate((connection_t *)msg_arrival_get_sender(server_message), 64000) == 3000);
    connection_set_send_buffer_size((connection_t *)msg_arrival_get_sender(server_message), 262144);
    assert(msg_arrival_get_size(server_message) == sizeof(payload));
    assert(memcmp(msg_arrival_get_data(server_message), payload, sizeof(payload)) == 0);
    assert(connection_send((connection_t *)msg_arrival_get_sender(server_message), msg_arrival_get_data(server_message), msg_arrival_get_size(server_message), 1, RDP_SEND_RELIABLE) == 0);
    fast_free(server_message);

    client_message = rdp_receive(client, 5000);
    assert(client_message != NULL);
    assert(msg_arrival_get_size(client_message) == sizeof(payload));
    assert(memcmp(msg_arrival_get_data(client_message), payload, sizeof(payload)) == 0);
    fast_free(client_message);

    memset(&client_close, 0, sizeof(client_close));
    client_close.connection = client_connection;
    assert(thrd_create(&client_close_thread, close_connection_thread, &client_close) == thrd_success);

    server_message = rdp_receive(server, 5000);
    assert(server_message != NULL);
    assert(msg_arrival_has_fin(server_message));
    assert(connection_close_wait((connection_t *)msg_arrival_get_sender(server_message), 1000, &server_close_result) == 0);
    assert(server_close_result == 1);
    fast_free(server_message);

    assert(thrd_join(client_close_thread, &thread_result) == thrd_success);
    assert(thread_result == 0);
    assert(client_close.call_result == 0);
    assert(client_close.clean_result == 1);
    rdp_destroy(client, 1);
    rdp_destroy(server, 1);
}

int main(void)
{
    static rdp_stat statistics;
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    check_public_api_signatures();
    memset(&statistics, 0, sizeof(statistics));
    g_rdp_stat = &statistics;
    fast_malloc_init(1024u * 1024u);
    run_scenario(RDP_CREATE_REQUIRE_IPV4);
    run_scenario(RDP_CREATE_REQUIRE_IPV4 | RDP_CREATE_USE_CRC);
    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return 0;
}
