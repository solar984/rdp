// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "rdplib.h"

static void print_connection_statistics(rdplib_connection_t *connection)
{
    rdplib_connection_perf_stats_t statistics;
    rdplib_disconnect_info_t disconnect;

    if (rdplib_connection_get_perf_stats(connection, &statistics) == RDPLIB_OK)
    {
        printf("connection statistics:\n"
               "  last packet receive time: %" PRIu32 " ms\n"
               "  packet sequence history: 0x%016" PRIx64 "\n"
               "  last packet sequence: %" PRIu16 "\n"
               "  round trip mean: %" PRIu32 " ms\n"
               "  round trip deviation: %" PRIu32 " ms\n"
               "  last ping sample: %" PRIu32 " ms\n"
               "  queued reliable data: %" PRIu32 " bytes\n"
               "  transmit stall time: %" PRIu32 " ms\n",
               statistics.last_packet_receive_time_ms, statistics.received_packet_sequence_history, statistics.last_received_packet_sequence, statistics.rtt_mean_ms, statistics.rtt_deviation_ms,
               statistics.last_ping_sample_ms, statistics.queued_reliable_bytes, statistics.transmit_stall_time_ms);
    }

    if (rdplib_connection_get_disconnect_info(connection, &disconnect) == RDPLIB_OK)
    {
        printf("  disconnect reason: 0x%08" PRIx32 "\n", disconnect.reason);
        if (disconnect.reason == RDPLIB_DISCONNECT_REASON_ICMP)
        {
            printf("  ICMP type and code: %u, %u\n"
                   "  ICMP source: %u.%u.%u.%u\n",
                   (unsigned)disconnect.icmp_type, (unsigned)disconnect.icmp_code, (unsigned)disconnect.icmp_source_ipv4[0], (unsigned)disconnect.icmp_source_ipv4[1],
                   (unsigned)disconnect.icmp_source_ipv4[2], (unsigned)disconnect.icmp_source_ipv4[3]);
        }
    }
}

int main(int argc, char **argv)
{
    uint16_t port = argc > 1 ? (uint16_t)strtoul(argv[1], NULL, 10) : 9000;
    rdplib_runtime_t *runtime = NULL;
    rdplib_endpoint_t *endpoint = NULL;
    rdplib_connection_t *connection = NULL;
    int result;
    int done = 0;
    int saw_disconnect = 0;
    int saw_fin = 0;

    result = rdplib_runtime_create(&runtime, 1024u * 1024u);
    if (result == RDPLIB_OK)
    {
        result = rdplib_endpoint_create(runtime, &endpoint, port, 32, RDPLIB_USE_CRC);
    }
    if (result != RDPLIB_OK)
    {
        fprintf(stderr, "rdplib server setup failed: %d\n", result);
        if (runtime)
        {
            (void)rdplib_runtime_destroy(runtime);
        }
        return 1;
    }

    printf("rdplib server listening on 127.0.0.1 UDP port %u\n", rdplib_endpoint_local_port(endpoint));
    while (!done && result == RDPLIB_OK)
    {
        rdplib_connection_t *accepted;
        rdplib_message_t *message;
        int process_result = rdplib_endpoint_process(endpoint, -1);

        if (process_result < 0)
        {
            result = process_result;
            break;
        }
        while ((accepted = rdplib_endpoint_accept(endpoint)) != NULL)
        {
            if (connection)
            {
                rdplib_message_t *rejected;

                while ((rejected = rdplib_connection_pop_message(accepted)) != NULL)
                {
                    rdplib_message_release(rejected);
                }
                (void)rdplib_connection_begin_close(accepted, 0);
                rdplib_connection_release(accepted);
                continue;
            }
            connection = accepted;
            result = rdplib_connection_enable_keepalive(connection);
            if (result == RDPLIB_OK)
            {
                result = rdplib_connection_set_data_rate(connection, 5120);
            }
            if (result == RDPLIB_OK)
            {
                result = rdplib_connection_set_send_buffer_size(connection, UINT32_C(0x40000));
            }
            if (result != RDPLIB_OK)
            {
                break;
            }
        }

        while (result == RDPLIB_OK && connection && (message = rdplib_connection_pop_message(connection)) != NULL)
        {
            int send_result = RDPLIB_CONNECTION_SEND_OK;

            if (rdplib_message_size(message))
            {
                printf("received %" PRIu32 " bytes\n", rdplib_message_size(message));
                send_result = rdplib_connection_send(connection, rdplib_message_data(message), rdplib_message_size(message), 0, RDPLIB_SEND_RELIABLE);
            }
            saw_fin = saw_fin || rdplib_message_has_fin(message);
            saw_disconnect = saw_disconnect || rdplib_message_is_disconnect(message);

            // The data view is valid until this matching release.
            rdplib_message_release(message);
            if (send_result != RDPLIB_CONNECTION_SEND_OK)
            {
                fprintf(stderr, "rdplib send failed: %d\n", send_result);
                result = send_result;
            }
        }
        done = saw_fin || saw_disconnect;
    }

    if (connection)
    {
        rdplib_message_t *message;
        int close_started = 0;

        while ((message = rdplib_connection_pop_message(connection)) != NULL)
        {
            saw_fin = saw_fin || rdplib_message_has_fin(message);
            saw_disconnect = saw_disconnect || rdplib_message_is_disconnect(message);
            rdplib_message_release(message);
        }

        // Statistics must be copied before begin_close detaches the application handle from the transport connection.
        print_connection_statistics(connection);
        if (!saw_disconnect)
        {
            int close_result = rdplib_connection_begin_close(connection, 1000);

            if (close_result == RDPLIB_CONNECTION_SEND_OK)
            {
                close_started = 1;
            }
            else if (result == RDPLIB_OK)
            {
                result = close_result;
            }
        }
        rdplib_connection_release(connection);
        connection = NULL;

        // A long running server keeps processing this endpoint naturally.  This short example gives the nonblocking close time to finish before destroying it.
        if (close_started)
        {
            (void)rdplib_endpoint_process(endpoint, 1000);
        }
    }
    if (rdplib_endpoint_destroy(endpoint) != RDPLIB_OK)
    {
        result = 1;
    }
    if (rdplib_runtime_destroy(runtime) != RDPLIB_OK)
    {
        result = 1;
    }
    return result == RDPLIB_OK ? 0 : 1;
}
