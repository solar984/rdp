// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp.h"

static rdp_global_statistics_t example_statistics;

int main(int argc, char **argv)
{
    static const char greeting[] = "hello from the raw RDP interface";
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t port = argc > 2 ? (uint16_t)strtoul(argv[2], NULL, 10) : 9000;
    rdp_t *owner = NULL;
    connection_t *connection = NULL;
    msg_arrival_t *reply = NULL;
    int result;

    memset(&example_statistics, 0, sizeof(example_statistics));
    g_rdp_stat = &example_statistics;
    fast_malloc_init(1024u * 1024u);

    // These flags, connection options, and limits mirror the historical game wrapper.
    result = rdp_create(&owner, 0, 1, RDP_CREATE_USE_CRC | RDP_CREATE_REQUIRE_IPV4);
    if (result == 0)
    {
        result = rdp_connect(owner, &connection, host, port, RDP_CONNECTION_FEATURE_KEEPALIVE);
    }
    if (result == 0)
    {
        *(uint32_t *)connection_app_ptr(connection) = UINT32_C(0x200);
        (void)connection_set_max_data_rate(connection, 5120);
        connection_set_send_buffer_size(connection, UINT32_C(0x40000));
        result = connection_send(connection, greeting, (uint32_t)strlen(greeting), 0, RDP_SEND_RELIABLE);
    }

    if (result == 0)
    {
        reply = rdp_receive(owner, 5000);
        if (!reply || msg_arrival_get_size(reply) == 0)
        {
            result = 1;
        }
    }
    if (result == 0)
    {
        printf("reply: %.*s\n", (int)msg_arrival_get_size(reply), (const char *)msg_arrival_get_data(reply));
    }

    if (reply)
    {
        fast_free(reply);
    }
    if (connection)
    {
        // The historical game wrapper waited but did not consume the optional protocol result.
        (void)connection_close_wait(connection, 10000, NULL);
    }
    if (owner)
    {
        // The client wrapper destroyed asynchronously; the example joins before allocator teardown.
        rdp_destroy(owner, 1);
    }
    fast_malloc_destroy();
    g_rdp_stat = NULL;

    if (result != 0)
    {
        fprintf(stderr, "raw client failed: %d\n", result);
        return 1;
    }
    return 0;
}
