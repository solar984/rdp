// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp.h"

static rdp_stat example_statistics;

static uint16_t raw_local_port(const rdp_t *owner)
{
    return ntohs(owner->local_udp_addr.sin_port);
}

int main(int argc, char **argv)
{
    uint16_t port = argc > 1 ? (uint16_t)strtoul(argv[1], NULL, 10) : 9000;
    rdp_t *owner = NULL;
    int result;
    int done = 0;

    memset(&example_statistics, 0, sizeof(example_statistics));
    g_rdp_stat = &example_statistics;
    fast_malloc_init(1024u * 1024u);

    result = rdp_create(&owner, port, 32, RDP_CREATE_USE_CRC | RDP_CREATE_REQUIRE_IPV4);
    if (result != 0)
    {
        fprintf(stderr, "rdp_create failed: %d\n", result);
        fast_malloc_destroy();
        g_rdp_stat = NULL;
        return 1;
    }

    printf("raw server listening on 127.0.0.1 UDP port %u\n", raw_local_port(owner));
    while (!done)
    {
        msg_arrival_t *message = rdp_receive(owner, -1);
        connection_t *connection;
        uint32_t bytes;

        if (!message)
        {
            continue;
        }
        connection = (connection_t *)msg_arrival_get_sender(message);
        bytes = msg_arrival_get_size(message);
        if (connection && bytes)
        {
            printf("received %u bytes\n", bytes);
            result = connection_send(connection, msg_arrival_get_data(message), bytes, 0, RDP_SEND_RELIABLE);
        }
        if (connection && msg_arrival_has_fin(message))
        {
            uint32_t clean_close = 0;
            (void)connection_close_wait(connection, 10000, &clean_close);
            printf("peer close completed: %s\n", clean_close ? "clean" : "not clean");
            done = 1;
        }
        fast_free(message);
    }

    rdp_destroy(owner, 1);
    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return result == 0 ? 0 : 1;
}
