// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crc.h"
#include "cypher.h"
#include "iov.h"
#include "rdplib_platform.h"
#include "usend.h"

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
#include "connection.h"
#endif

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

enum
{
    TEST_NETWORK_VERSION = 0x0202,
    TEST_AF_INET = 2,
    TEST_SOCKET_DATAGRAM = 2,
    TEST_PROTOCOL_UDP = 17,
    TEST_INVALID_ARGUMENT = 6,
    TEST_CAPACITY_EXCEEDED = 18,
    TEST_USEND_CAPACITY = 32768
};

_Static_assert(offsetof(iov_t, data) == 0, "iov_t::data moved");
_Static_assert(offsetof(iov_t, size) == sizeof(void *), "iov_t::size moved");
_Static_assert(sizeof(iov_t) == (sizeof(void *) == 4 ? 8 : 16), "iov_t native layout changed");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(iov_t) == 8, "iov_t no longer matches the recovered x86 layout");
#endif

typedef struct loopback_pair_t
{
    intptr_t sender;
    intptr_t receiver;
    uint8_t destination[16];
} loopback_pair_t;

static uint32_t load_network_u32(const uint8_t *source)
{
    return (uint32_t)source[0] << 24 | (uint32_t)source[1] << 16 | (uint32_t)source[2] << 8 | source[3];
}

static void store_native_u16(uint8_t *destination, uint16_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static struct sockaddr *as_sockaddr(uint8_t address[16])
{
    return (struct sockaddr *)(void *)address;
}

static void verify_signatures(void)
{
    int32_t (*append_crc)(char *, int32_t) = rdp_append_crc;
    int32_t (*encode_data)(char *, int32_t) = rdp_encode_data;
    uint32_t (*send_vector)(intptr_t, iov_t *, uint32_t, struct sockaddr *, uint32_t, uint32_t) = usend;
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    uint32_t (*framed_size)(uint32_t, uint32_t, uint32_t) = rdplib_usend_framed_size;
#endif

    (void)append_crc;
    (void)encode_data;
    (void)send_vector;
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    (void)framed_size;
#endif
}

static void loopback_pair_create(loopback_pair_t *pair)
{
    uint32_t address_bytes = sizeof(pair->destination);

    memset(pair, 0, sizeof(*pair));
    pair->sender = rdplib_platform_socket_create(TEST_AF_INET, TEST_SOCKET_DATAGRAM, TEST_PROTOCOL_UDP);
    pair->receiver = rdplib_platform_socket_create(TEST_AF_INET, TEST_SOCKET_DATAGRAM, TEST_PROTOCOL_UDP);
    assert(pair->sender != -1);
    assert(pair->receiver != -1);

    store_native_u16(pair->destination, TEST_AF_INET);
    pair->destination[4] = 127;
    pair->destination[5] = 0;
    pair->destination[6] = 0;
    pair->destination[7] = 1;
    assert(rdplib_platform_socket_bind(pair->receiver, pair->destination, sizeof(pair->destination)) == 0);
    assert(rdplib_platform_socket_get_name(pair->receiver, pair->destination, &address_bytes) == 0);
    assert(address_bytes == sizeof(pair->destination));
}

static void loopback_pair_destroy(loopback_pair_t *pair)
{
    rdplib_platform_socket_close(pair->receiver);
    rdplib_platform_socket_close(pair->sender);
    pair->receiver = -1;
    pair->sender = -1;
}

static int32_t receive_packet(loopback_pair_t *pair, uint8_t *packet, uint32_t capacity)
{
    uint8_t source_address[16];

    return rdplib_platform_receive_datagram(pair->receiver, packet, capacity, source_address);
}

static void test_append_crc(void)
{
    static const uint8_t payload[] = {'a', 'b', 'c', 'D', 'E'};
    uint8_t storage[1 + sizeof(payload) + sizeof(uint32_t)];
    uint8_t *buffer = storage + 1;
    int32_t size;

    memcpy(buffer, payload, sizeof(payload));
    size = rdp_append_crc((char *)buffer, (int32_t)sizeof(payload));
    assert(size == (int32_t)(sizeof(payload) + sizeof(uint32_t)));
    assert(memcmp(buffer, payload, sizeof(payload)) == 0);
    assert(load_network_u32(buffer + sizeof(payload)) == rdp_crc(0, (const char *)payload, (uint32_t)sizeof(payload)));
}

static void test_encode_padding_counts(void)
{
    uint8_t original[16];
    uint8_t buffer[24];
    uint32_t pad_size;

    for (pad_size = 1; pad_size <= 8; ++pad_size)
    {
        uint32_t original_size = pad_size == 8 ? 8 : 8 - pad_size;
        uint32_t expected_size = original_size + pad_size;
        int32_t encoded_size;
        uint32_t index;

        for (index = 0; index < original_size; ++index)
        {
            original[index] = (uint8_t)(0x31u + index + pad_size);
        }
        memset(buffer, 0, sizeof(buffer));
        memcpy(buffer, original, original_size);
        srand((unsigned int)(0x4100u + pad_size));
        encoded_size = rdp_encode_data((char *)buffer, (int32_t)original_size);
        assert(encoded_size == (int32_t)expected_size);

        rdp_decode(buffer, encoded_size / 8);
        assert(memcmp(buffer, original, original_size) == 0);
        assert((buffer[encoded_size - 1] & 0x0Fu) == pad_size);
    }
}

static void test_scatter_gather_wire(void)
{
    static const uint8_t first[] = {'a', 'b', 'c'};
    static const uint8_t second[] = {'D', 'E'};
    static const uint8_t joined[] = {'a', 'b', 'c', 'D', 'E'};
    loopback_pair_t pair;
    iov_t iov[2];
    uint8_t encrypted_packet[32];
    uint8_t packet[32];
    int32_t encrypted_bytes;
    int32_t packet_bytes;
    uint32_t padding;
    uint32_t unpadded_bytes;

    loopback_pair_create(&pair);
    iov[0].data = (void *)first;
    iov[0].size = sizeof(first);
    iov[1].data = (void *)second;
    iov[1].size = sizeof(second);

    assert(usend(pair.sender, iov, 2, as_sockaddr(pair.destination), 0, 0) == 0);
    packet_bytes = receive_packet(&pair, packet, sizeof(packet));
    assert(packet_bytes == (int32_t)sizeof(joined));
    assert(memcmp(packet, joined, sizeof(joined)) == 0);

    assert(usend(pair.sender, iov, 2, as_sockaddr(pair.destination), 0, 1) == 0);
    packet_bytes = receive_packet(&pair, packet, sizeof(packet));
    assert(packet_bytes == (int32_t)(sizeof(joined) + sizeof(uint32_t)));
    assert(memcmp(packet, joined, sizeof(joined)) == 0);
    assert(load_network_u32(packet + sizeof(joined)) == rdp_crc(0, (const char *)joined, (uint32_t)sizeof(joined)));

    srand(0x51A7);
    assert(usend(pair.sender, iov, 2, as_sockaddr(pair.destination), 1, 0) == 0);
    packet_bytes = receive_packet(&pair, packet, sizeof(packet));
    assert(packet_bytes > 0 && (packet_bytes & 7) == 0);
    encrypted_bytes = packet_bytes;
    memcpy(encrypted_packet, packet, (size_t)packet_bytes);
    rdp_decode(packet, packet_bytes / 8);
    padding = packet[packet_bytes - 1] & 0x0Fu;
    assert(padding >= 1 && padding <= 8);
    unpadded_bytes = (uint32_t)packet_bytes - padding;
    assert(unpadded_bytes == sizeof(joined) + sizeof(uint32_t));
    assert(memcmp(packet, joined, sizeof(joined)) == 0);
    assert(load_network_u32(packet + sizeof(joined)) == rdp_crc(0, (const char *)joined, (uint32_t)sizeof(joined)));

    srand(0x51A7);
    assert(usend(pair.sender, iov, 2, as_sockaddr(pair.destination), 1, 1) == 0);
    packet_bytes = receive_packet(&pair, packet, sizeof(packet));
    assert(packet_bytes == encrypted_bytes);
    assert(memcmp(packet, encrypted_packet, (size_t)packet_bytes) == 0);

    srand(0x51A7);
    assert(usend(pair.sender, iov, 2, as_sockaddr(pair.destination), 7, 9) == 0);
    packet_bytes = receive_packet(&pair, packet, sizeof(packet));
    assert(packet_bytes == encrypted_bytes);
    assert(memcmp(packet, encrypted_packet, (size_t)packet_bytes) == 0);

    assert(usend(-1, iov, 2, as_sockaddr(pair.destination), 0, 0) == 1);
    loopback_pair_destroy(&pair);
}

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
static void test_framed_sizes(void)
{
    assert(rdplib_usend_framed_size(543, 0, 0) == 543);
    assert(rdplib_usend_framed_size(543, 0, 1) == 547);
    assert(rdplib_usend_framed_size(543, 1, 0) == 552);
    assert(rdplib_usend_framed_size(543, 1, 1) == 552);
    assert(rdplib_usend_framed_size(532, 0, 1) == 536);
    assert(rdplib_usend_framed_size(533, 0, 1) == 537);
}

typedef struct drop_capture_t
{
    const uint8_t *expected;
    uint32_t expected_bytes;
    uint32_t calls;
} drop_capture_t;

static int capture_and_drop(void *context, rdplib_packet_drop_direction_t direction, const uint8_t *packet, uint32_t packet_bytes)
{
    drop_capture_t *capture = (drop_capture_t *)context;

    assert(direction == RDPLIB_PACKET_DROP_OUTBOUND);
    assert(packet_bytes == capture->expected_bytes);
    assert(memcmp(packet, capture->expected, packet_bytes) == 0);
    ++capture->calls;
    return 1;
}

static void test_checked_drop_adapter_sees_joined_plaintext(void)
{
    static const uint8_t first[] = {'a', 'b', 'c'};
    static const uint8_t second[] = {'D', 'E'};
    static const uint8_t joined[] = {'a', 'b', 'c', 'D', 'E'};
    connection_t connection;
    drop_capture_t capture;
    iov_t iov[2];
    uint8_t remote_addr[16] = {0};

    memset(&connection, 0, sizeof(connection));
    capture.expected = joined;
    capture.expected_bytes = sizeof(joined);
    capture.calls = 0;
    connection.rdplib_packet_drop_callback = capture_and_drop;
    connection.rdplib_packet_drop_context = &capture;
    iov[0].data = (void *)first;
    iov[0].size = sizeof(first);
    iov[1].data = (void *)second;
    iov[1].size = sizeof(second);

    // The invalid socket is never reached: the callback drops the joined plaintext before CRC, padding, or encryption.
    assert(rdplib_usend(&connection, -1, iov, 2, as_sockaddr(remote_addr), 1, 1) == 0);
    assert(capture.calls == 1);
}

typedef struct snapshot_capture_t
{
    const uint8_t *expected;
    uint32_t expected_bytes;
    uint8_t *first;
    uint32_t first_bytes;
    uint8_t *second;
    uint32_t second_bytes;
    uint32_t calls;
} snapshot_capture_t;

static int capture_mutate_sources_and_allow(void *context, rdplib_packet_drop_direction_t direction, const uint8_t *packet, uint32_t packet_bytes)
{
    snapshot_capture_t *capture = (snapshot_capture_t *)context;

    assert(direction == RDPLIB_PACKET_DROP_OUTBOUND);
    assert(packet_bytes == capture->expected_bytes);
    assert(memcmp(packet, capture->expected, packet_bytes) == 0);
    memset(capture->first, 0xE1, capture->first_bytes);
    memset(capture->second, 0xE2, capture->second_bytes);
    ++capture->calls;
    return 0;
}

static void test_checked_allow_adapter_sends_joined_snapshot(void)
{
    static const uint8_t expected[] = {'a', 'b', 'c', 'D', 'E'};
    loopback_pair_t pair;
    connection_t connection;
    snapshot_capture_t capture;
    iov_t iov[2];
    uint8_t first[] = {'a', 'b', 'c'};
    uint8_t second[] = {'D', 'E'};
    uint8_t packet[sizeof(expected)];

    loopback_pair_create(&pair);
    memset(&connection, 0, sizeof(connection));
    capture.expected = expected;
    capture.expected_bytes = sizeof(expected);
    capture.first = first;
    capture.first_bytes = sizeof(first);
    capture.second = second;
    capture.second_bytes = sizeof(second);
    capture.calls = 0;
    connection.rdplib_packet_drop_callback = capture_mutate_sources_and_allow;
    connection.rdplib_packet_drop_context = &capture;
    iov[0].data = first;
    iov[0].size = sizeof(first);
    iov[1].data = second;
    iov[1].size = sizeof(second);

    assert(rdplib_usend(&connection, pair.sender, iov, 2, as_sockaddr(pair.destination), 0, 0) == 0);
    assert(capture.calls == 1);
    assert(first[0] == UINT8_C(0xE1));
    assert(second[0] == UINT8_C(0xE2));
    assert(receive_packet(&pair, packet, sizeof(packet)) == (int32_t)sizeof(expected));
    assert(memcmp(packet, expected, sizeof(expected)) == 0);
    loopback_pair_destroy(&pair);
}

static void test_checked_arguments_and_capacity(void)
{
    static const uint32_t encrypted_sizes[] = {32756, 32757, 32763};
    loopback_pair_t pair;
    iov_t iov;
    uint8_t *payload;
    uint8_t *packet;
    uint32_t index;

    loopback_pair_create(&pair);
    payload = (uint8_t *)malloc(TEST_USEND_CAPACITY);
    packet = (uint8_t *)malloc(TEST_USEND_CAPACITY);
    assert(payload != NULL);
    assert(packet != NULL);
    for (index = 0; index < TEST_USEND_CAPACITY; ++index)
    {
        payload[index] = (uint8_t)(index * 37u + 11u);
    }

    iov.data = payload;
    iov.size = TEST_USEND_CAPACITY;
    assert(usend(pair.sender, &iov, 1, as_sockaddr(pair.destination), 0, 0) == 0);
    assert(receive_packet(&pair, packet, TEST_USEND_CAPACITY) == TEST_USEND_CAPACITY);
    assert(memcmp(packet, payload, TEST_USEND_CAPACITY) == 0);

    iov.size = TEST_USEND_CAPACITY - sizeof(uint32_t);
    assert(usend(pair.sender, &iov, 1, as_sockaddr(pair.destination), 0, 1) == 0);
    assert(receive_packet(&pair, packet, TEST_USEND_CAPACITY) == TEST_USEND_CAPACITY);
    assert(memcmp(packet, payload, iov.size) == 0);
    assert(load_network_u32(packet + iov.size) == rdp_crc(0, (const char *)payload, iov.size));

    for (index = 0; index < sizeof(encrypted_sizes) / sizeof(encrypted_sizes[0]); ++index)
    {
        iov.size = encrypted_sizes[index];
        srand((unsigned int)(0x6200u + index));
        assert(usend(pair.sender, &iov, 1, as_sockaddr(pair.destination), 1, 0) == 0);
        assert(receive_packet(&pair, packet, TEST_USEND_CAPACITY) == TEST_USEND_CAPACITY);
    }

    iov.size = 32764;
    assert(usend(pair.sender, &iov, 1, as_sockaddr(pair.destination), 1, 0) == TEST_CAPACITY_EXCEEDED);
    iov.size = TEST_USEND_CAPACITY + 1u;
    assert(usend(pair.sender, &iov, 1, as_sockaddr(pair.destination), 0, 0) == TEST_CAPACITY_EXCEEDED);
    iov.size = TEST_USEND_CAPACITY - sizeof(uint32_t) + 1u;
    assert(usend(pair.sender, &iov, 1, as_sockaddr(pair.destination), 0, 1) == TEST_CAPACITY_EXCEEDED);
    assert(usend(pair.sender, NULL, 1, as_sockaddr(pair.destination), 0, 0) == TEST_INVALID_ARGUMENT);
    assert(usend(pair.sender, &iov, 1, NULL, 0, 0) == TEST_INVALID_ARGUMENT);
    iov.data = NULL;
    iov.size = 1;
    assert(usend(pair.sender, &iov, 1, as_sockaddr(pair.destination), 0, 0) == TEST_INVALID_ARGUMENT);
    iov.size = 0;
    assert(usend(pair.sender, &iov, 1, as_sockaddr(pair.destination), 0, 0) == 0);
    assert(receive_packet(&pair, packet, TEST_USEND_CAPACITY) == 0);
    assert(usend(pair.sender, NULL, 0, as_sockaddr(pair.destination), 0, 0) == 0);
    assert(receive_packet(&pair, packet, TEST_USEND_CAPACITY) == 0);

    free(packet);
    free(payload);
    loopback_pair_destroy(&pair);
}
#endif

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    assert(rdplib_platform_network_startup(TEST_NETWORK_VERSION) == 0);
    verify_signatures();
    test_append_crc();
    test_encode_padding_counts();
    test_scatter_gather_wire();
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    test_framed_sizes();
    test_checked_drop_adapter_sees_joined_plaintext();
    test_checked_allow_adapter_sends_joined_snapshot();
    test_checked_arguments_and_capacity();
#endif
    rdplib_platform_network_cleanup();
    return 0;
}
