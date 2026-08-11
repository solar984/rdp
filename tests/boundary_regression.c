// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp.h"
#include "connection.h"
#include "connhash.h"
#include "rdplib_platform.h"
#include "stats.h"
#include "usend.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>

#if defined(MAP_ANONYMOUS)
#define TEST_MAP_ANONYMOUS MAP_ANONYMOUS
#elif defined(MAP_ANON)
#define TEST_MAP_ANONYMOUS MAP_ANON
#else
#error "A supported anonymous mmap flag is required"
#endif
#endif

enum
{
    TEST_INVALID_ARGUMENT = 6,
    TEST_HISTORY_FULL = 15,
    TEST_CAPACITY_EXCEEDED = 18,
    TEST_NETWORK_VERSION = 0x0202,
    TEST_AF_INET = 2,
    TEST_SOCKET_DATAGRAM = 2,
    TEST_PROTOCOL_UDP = 17
};

static rdp_global_statistics_t test_statistics;

static const char *selected_mode(void)
{
#ifndef RDPLIB_SOURCE_FAITHFUL
    return "default";
#else
    return "source-faithful";
#endif
}

static void store_native_u16(uint8_t *destination, uint16_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static void store_network_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static int write_bytes(const char *path, const void *data, size_t bytes)
{
    FILE *output = NULL;

#ifdef _WIN32
    if (fopen_s(&output, path, "wb") != 0)
    {
        output = NULL;
    }
#else
    output = fopen(path, "wb");
#endif
    if (!output)
    {
        return 0;
    }
    if (fwrite(data, 1, bytes, output) != bytes || fclose(output) != 0)
    {
        return 0;
    }
    return 1;
}

static int write_valid_result(const char *path, uint32_t wire_bytes, rdp_rx_arrival_disposition_t disposition, const _rdp_header_t *header)
{
    char text[256];
    int text_bytes = snprintf(text, sizeof(text), "wire_bytes=%u\ndisposition=%d\nflags=0x%04X\nsequence=%u\nheader_bytes=%u\npayload_bytes=%u\n", wire_bytes, disposition, header->flags,
                              header->sequence, header->header_bytes, header->payload_bytes);

    return text_bytes > 0 && (size_t)text_bytes < sizeof(text) && write_bytes(path, text, (size_t)text_bytes);
}

static int run_valid_wire(const char *wire_path, const char *result_path)
{
    static const uint8_t header_bytes[] = {0x00, 0x00, 0x00, 0x01};
    static const uint8_t payload_bytes[] = {0x52, 0x44, 0x50, 0x2D, 0x43, 0x48, 0x45, 0x43, 0x4B, 0x45, 0x44};
    rdp_buffer_t buffers[2];
    uint8_t receiver_address[16] = {0};
    uint8_t source_address[16] = {0};
    uint8_t received[64];
    uint32_t address_bytes = sizeof(receiver_address);
    intptr_t receiver = -1;
    intptr_t sender = -1;
    int32_t received_bytes;
    connection_t connection;
    _rdp_header_t parsed;
    rdp_rx_arrival_disposition_t disposition;
    int result = 1;

    buffers[0].data = header_bytes;
    buffers[0].bytes = sizeof(header_bytes);
    buffers[1].data = payload_bytes;
    buffers[1].bytes = sizeof(payload_bytes);

    if (rdplib_platform_network_startup(TEST_NETWORK_VERSION) != 0)
    {
        fprintf(stderr, "network startup failed\n");
        return 1;
    }
    receiver = rdplib_platform_socket_create(TEST_AF_INET, TEST_SOCKET_DATAGRAM, TEST_PROTOCOL_UDP);
    sender = rdplib_platform_socket_create(TEST_AF_INET, TEST_SOCKET_DATAGRAM, TEST_PROTOCOL_UDP);
    store_native_u16(receiver_address, TEST_AF_INET);
    receiver_address[4] = 127;
    receiver_address[5] = 0;
    receiver_address[6] = 0;
    receiver_address[7] = 1;
    if (receiver == -1 || sender == -1 || rdplib_platform_socket_bind(receiver, receiver_address, sizeof(receiver_address)) != 0 ||
        rdplib_platform_socket_get_name(receiver, receiver_address, &address_bytes) != 0)
    {
        fprintf(stderr, "loopback socket setup failed\n");
        goto done;
    }

    result = usend(sender, buffers, 2, receiver_address, 0, 1);
    if (result != 0)
    {
        fprintf(stderr, "selected usend returned %d for valid input\n", result);
        result = 1;
        goto done;
    }
    received_bytes = rdplib_platform_receive_datagram(receiver, received, sizeof(received), source_address);
    if (received_bytes != (int32_t)(sizeof(header_bytes) + sizeof(payload_bytes) + sizeof(uint32_t)))
    {
        fprintf(stderr, "received %d valid-wire bytes\n", received_bytes);
        result = 1;
        goto done;
    }

    memset(&connection, 0, sizeof(connection));
    memset(&parsed, 0, sizeof(parsed));
    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    disposition = connection_parse_and_validate_arrival(&connection, received, (uint16_t)(received_bytes - (int32_t)sizeof(uint32_t)), &parsed);
    if (disposition != RDP_RX_ACCEPT || parsed.flags != 0 || parsed.sequence != 1 || parsed.header_bytes != sizeof(header_bytes) || parsed.payload_bytes != sizeof(payload_bytes))
    {
        fprintf(stderr, "selected parser changed valid input: disposition %d flags 0x%04X sequence %u header %u payload %u\n", disposition, parsed.flags, parsed.sequence, parsed.header_bytes,
                parsed.payload_bytes);
        result = 1;
        goto done;
    }
    if (!write_bytes(wire_path, received, (size_t)received_bytes) || !write_valid_result(result_path, (uint32_t)received_bytes, disposition, &parsed))
    {
        fprintf(stderr, "could not retain valid differential output\n");
        result = 1;
        goto done;
    }

    printf("build=%s usend=usend parser=connection_parse_and_validate_arrival wire_bytes=%d\n", selected_mode(), received_bytes);
    result = 0;

done:
    if (sender != -1)
    {
        rdplib_platform_socket_close(sender);
    }
    if (receiver != -1)
    {
        rdplib_platform_socket_close(receiver);
    }
    rdplib_platform_network_cleanup();
    g_rdp_stat = NULL;
    return result;
}

static int run_usend_overflow(void)
{
    rdp_buffer_t buffer;
    uint8_t destination[16] = {0};
    uint8_t *payload = (uint8_t *)malloc(65507);
    int result;

    if (!payload)
    {
        return 2;
    }
    memset(payload, 0xA5, 65507);
    store_native_u16(destination, TEST_AF_INET);
    destination[4] = 127;
    destination[7] = 1;
    buffer.data = payload;
    buffer.bytes = 65507;
    result = usend(-1, &buffer, 1, destination, 0, 1);
    printf("build=%s function=usend result=%d\n", selected_mode(), result);
    free(payload);
#ifndef RDPLIB_SOURCE_FAITHFUL
    return result == TEST_CAPACITY_EXCEEDED ? 0 : 3;
#else
    return 0;
#endif
}

static uint8_t *allocate_guarded_packet(void **allocation, size_t *allocation_bytes)
{
#ifdef _WIN32
    SYSTEM_INFO system_info;
    DWORD old_protection;
    uint8_t *pages;

    GetSystemInfo(&system_info);
    *allocation_bytes = (size_t)system_info.dwPageSize * 2u;
    pages = (uint8_t *)VirtualAlloc(NULL, *allocation_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!pages || !VirtualProtect(pages + system_info.dwPageSize, system_info.dwPageSize, PAGE_NOACCESS, &old_protection))
    {
        if (pages)
        {
            VirtualFree(pages, 0, MEM_RELEASE);
        }
        return NULL;
    }
    *allocation = pages;
    return pages + system_info.dwPageSize - 2u;
#else
    long page_bytes = sysconf(_SC_PAGESIZE);
    uint8_t *pages;

    if (page_bytes <= 0)
    {
        return NULL;
    }
    *allocation_bytes = (size_t)page_bytes * 2u;
    pages = (uint8_t *)mmap(NULL, *allocation_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | TEST_MAP_ANONYMOUS, -1, 0);
    if (pages == MAP_FAILED || mprotect(pages + page_bytes, (size_t)page_bytes, PROT_NONE) != 0)
    {
        if (pages != MAP_FAILED)
        {
            munmap(pages, *allocation_bytes);
        }
        return NULL;
    }
    *allocation = pages;
    return pages + page_bytes - 2;
#endif
}

static void free_guarded_packet(void *allocation, size_t allocation_bytes)
{
#ifdef _WIN32
    (void)allocation_bytes;
    VirtualFree(allocation, 0, MEM_RELEASE);
#else
    munmap(allocation, allocation_bytes);
#endif
}

static int run_parser_short(void)
{
    connection_t connection;
    _rdp_header_t parsed;
    void *allocation = NULL;
    size_t allocation_bytes = 0;
    uint8_t *packet = allocate_guarded_packet(&allocation, &allocation_bytes);
    rdp_rx_arrival_disposition_t result;

    if (!packet)
    {
        return 2;
    }
    packet[0] = 0x1A;
    packet[1] = 0x00;
    memset(&connection, 0, sizeof(connection));
    memset(&parsed, 0xA5, sizeof(parsed));
    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    result = connection_parse_and_validate_arrival(&connection, packet, 2, &parsed);
    printf("build=%s function=connection_parse_and_validate_arrival result=%d header_unchanged=%d\n", selected_mode(), result, parsed.flags == UINT16_C(0xA5A5));
    free_guarded_packet(allocation, allocation_bytes);
    g_rdp_stat = NULL;
#ifndef RDPLIB_SOURCE_FAITHFUL
    return result == RDP_RX_ABORT && parsed.flags == UINT16_C(0xA5A5) ? 0 : 3;
#else
    return 0;
#endif
}

static int run_fragment_final(uint32_t payload_bytes)
{
    enum
    {
        TEST_FRAGMENT_HEADER_BYTES = 12,
        TEST_FRAGMENT_MAX_PAYLOAD_BYTES = 513
    };
    uint8_t packet[TEST_FRAGMENT_HEADER_BYTES + TEST_FRAGMENT_MAX_PAYLOAD_BYTES];
    connection_t connection;
    _rdp_header_t parsed;
    rdp_rx_arrival_disposition_t disposition;
    uint64_t global_invalid_before;
    uint32_t connection_invalid_before;
    int connected_before;
    int assembly_unchanged;

    memset(packet, 0, sizeof(packet));
    store_network_u16(packet, RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT);
    store_network_u16(packet + 2, 1);
    store_network_u16(packet + 4, 1);
    store_network_u16(packet + 6, 1);
    store_network_u16(packet + 8, 1);
    store_network_u16(packet + 10, 2);
    memset(packet + TEST_FRAGMENT_HEADER_BYTES, 0x5A, payload_bytes);

    memset(&connection, 0, sizeof(connection));
    memset(&parsed, 0, sizeof(parsed));
    memset(&test_statistics, 0, sizeof(test_statistics));
    connection.transmit.connected = 1;
    g_rdp_stat = &test_statistics;
    global_invalid_before = test_statistics.invalid_fragment_headers;
    connection_invalid_before = connection.receive.recording.statistics.invalid_fragment_count;
    connected_before = connection.transmit.connected;
    disposition = connection_parse_and_validate_arrival(&connection, packet, (uint16_t)(TEST_FRAGMENT_HEADER_BYTES + payload_bytes), &parsed);
    assembly_unchanged = connection.receive.ownership.fragment_messages.head == NULL && connection.receive.ownership.fragment_messages.count == 0;

    printf("build=%s function=connection_parse_and_validate_arrival payload_bytes=%u disposition=%d global_invalid_delta=%llu connection_invalid_delta=%u connected_changed=%d assembly_unchanged=%d\n",
           selected_mode(), payload_bytes, disposition, (unsigned long long)(test_statistics.invalid_fragment_headers - global_invalid_before),
           connection.receive.recording.statistics.invalid_fragment_count - connection_invalid_before, connection.transmit.connected != connected_before, assembly_unchanged);
    g_rdp_stat = NULL;
#ifndef RDPLIB_SOURCE_FAITHFUL
    return disposition == RDP_RX_ABORT && test_statistics.invalid_fragment_headers == global_invalid_before + 1u &&
                   connection.receive.recording.statistics.invalid_fragment_count == connection_invalid_before + 1u && !connection.transmit.connected &&
                   connection.transmit.disconnect_reason == RDP_DISCONNECT_REASON_PROTOCOL_ERROR && assembly_unchanged
               ? 0
               : 3;
#else
    return disposition == RDP_RX_ACCEPT && test_statistics.invalid_fragment_headers == global_invalid_before &&
                   connection.receive.recording.statistics.invalid_fragment_count == connection_invalid_before && connection.transmit.connected && assembly_unchanged
               ? 0
               : 3;
#endif
}

static int run_send_stream(void)
{
    static const uint8_t payload[] = {1, 2, 3, 4};
    rdp_t *server = NULL;
    rdp_t *client = NULL;
    connection_t *public_connection = NULL;
    connection_t *connection;
    uint16_t next_message_id;
    int result;
    int process_result = 1;

    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    fast_malloc_init(1024u * 1024u);
    if (rdp_create(&server, 0, 1, RDP_CREATE_REQUIRE_IPV4) != 0 || rdp_create(&client, 0, 1, RDP_CREATE_REQUIRE_IPV4) != 0 ||
        rdp_connect(client, &public_connection, "127.0.0.1", (uint16_t)(((uint16_t)server->ipv4_address[2] << 8) | server->ipv4_address[3]), 0) != 0)
    {
        fprintf(stderr, "send-stream fixture creation failed\n");
        goto done;
    }

    connection = (connection_t *)public_connection;
    next_message_id = connection->transmit.reliable_next_message_id;
    result = connection_send(public_connection, payload, sizeof(payload), 20, RDP_SEND_RELIABLE);
    printf("build=%s function=connection_send result=%d next_id_changed=%d syn_published=%u\n", selected_mode(), result, connection->transmit.reliable_next_message_id != next_message_id,
           connection->transmit.syn_sent);
#ifndef RDPLIB_SOURCE_FAITHFUL
    process_result = result == TEST_INVALID_ARGUMENT && connection->transmit.reliable_next_message_id == next_message_id && !connection->transmit.syn_sent ? 0 : 3;
#else
    process_result = result == 0 && connection->transmit.reliable_next_message_id != next_message_id && connection->transmit.syn_sent ? 0 : 3;
#endif

done:
    if (client)
    {
        rdp_destroy(client, 1);
    }
    if (server)
    {
        rdp_destroy(server, 1);
    }
    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return process_result;
}

static int run_send_history(void)
{
    uint8_t payload[4096] = {0};
    rdp_t *server = NULL;
    rdp_t *client = NULL;
    connection_t *public_connection = NULL;
    connection_t *connection;
    uint16_t next_message_id;
    int result;
    int process_result = 1;

    memset(&test_statistics, 0, sizeof(test_statistics));
    g_rdp_stat = &test_statistics;
    fast_malloc_init(4u * 1024u * 1024u);
    if (rdp_create(&server, 0, 1, RDP_CREATE_REQUIRE_IPV4) != 0 || rdp_create(&client, 0, 1, RDP_CREATE_REQUIRE_IPV4) != 0 ||
        rdp_connect(client, &public_connection, "127.0.0.1", (uint16_t)(((uint16_t)server->ipv4_address[2] << 8) | server->ipv4_address[3]), 0) != 0)
    {
        fprintf(stderr, "send-history fixture creation failed\n");
        goto done;
    }

    connection = (connection_t *)public_connection;
    connection_set_send_buffer_size(public_connection, 4u * 1024u * 1024u);
    next_message_id = connection->transmit.reliable_next_message_id;
    connection->transmit.acknowledged_through_message_id = (uint16_t)(next_message_id - 4089u);
    result = connection_send(public_connection, payload, sizeof(payload), 0, RDP_SEND_RELIABLE);
    printf("build=%s function=connection_send result=%d next_id_delta=%u\n", selected_mode(), result, (uint16_t)(connection->transmit.reliable_next_message_id - next_message_id));
#ifndef RDPLIB_SOURCE_FAITHFUL
    process_result = result == TEST_HISTORY_FULL && connection->transmit.reliable_next_message_id == next_message_id ? 0 : 3;
#else
    process_result = result == 0 && (uint16_t)(connection->transmit.reliable_next_message_id - next_message_id) == 8 ? 0 : 3;
#endif

done:
    if (client)
    {
        rdp_destroy(client, 1);
    }
    if (server)
    {
        rdp_destroy(server, 1);
    }
    fast_malloc_destroy();
    g_rdp_stat = NULL;
    return process_result;
}

static int run_connhash_invalid(void)
{
    rdp_connhash_t hash;
    rdp_connhash_bucket_t *initial_buckets = (rdp_connhash_bucket_t *)(uintptr_t)1;
    int result;

    hash.bucket_count = UINT16_C(0xA5A5);
    hash.buckets = initial_buckets;
    result = connhash_create(&hash, 17);
    printf("build=%s function=connhash_create result=%d bucket_count_changed=%d buckets_changed=%d\n", selected_mode(), result, hash.bucket_count != UINT16_C(0xA5A5), hash.buckets != initial_buckets);

#ifndef RDPLIB_SOURCE_FAITHFUL
    return result == 1 && hash.bucket_count == UINT16_C(0xA5A5) && hash.buckets == initial_buckets ? 0 : 3;
#else
    if (hash.buckets != initial_buckets && hash.buckets)
    {
        connhash_destroy(&hash);
    }
    return 0;
#endif
}

static void print_usage(const char *program)
{
    fprintf(stderr, "usage: %s valid-wire WIRE_FILE RESULT_FILE | usend-overflow | parser-short | fragment-final-large | fragment-final-zero | send-stream | send-history | connhash-invalid\n",
            program);
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    if (argc == 4 && !strcmp(argv[1], "valid-wire"))
    {
        return run_valid_wire(argv[2], argv[3]);
    }
    if (argc == 2 && !strcmp(argv[1], "usend-overflow"))
    {
        return run_usend_overflow();
    }
    if (argc == 2 && !strcmp(argv[1], "parser-short"))
    {
        return run_parser_short();
    }
    if (argc == 2 && !strcmp(argv[1], "fragment-final-large"))
    {
        return run_fragment_final(513);
    }
    if (argc == 2 && !strcmp(argv[1], "fragment-final-zero"))
    {
        return run_fragment_final(0);
    }
    if (argc == 2 && !strcmp(argv[1], "send-stream"))
    {
        return run_send_stream();
    }
    if (argc == 2 && !strcmp(argv[1], "send-history"))
    {
        return run_send_history();
    }
    if (argc == 2 && !strcmp(argv[1], "connhash-invalid"))
    {
        return run_connhash_invalid();
    }

    print_usage(argv[0]);
    return 2;
}
