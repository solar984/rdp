// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>
#include <string.h>

#include "rdp.h"

typedef struct send_capture_t
{
    uint32_t calls;
    intptr_t endpoint;
    uint8_t packet[8];
    uint32_t packet_bytes;
    uint8_t destination[16];
    uint32_t destination_bytes;
} send_capture_t;

static send_capture_t capture;
static int32_t send_result;
static uint32_t semaphore_wait_calls;
static uint32_t semaphore_wait_timeout;

static int32_t capture_send(intptr_t endpoint, const uint8_t *packet, uint32_t packet_bytes, const void *destination, uint32_t destination_bytes)
{
    ++capture.calls;
    capture.endpoint = endpoint;
    capture.packet_bytes = packet_bytes;
    capture.destination_bytes = destination_bytes;
    assert(packet_bytes <= sizeof(capture.packet));
    assert(destination_bytes <= sizeof(capture.destination));
    memcpy(capture.packet, packet, packet_bytes);
    memcpy(capture.destination, destination, destination_bytes);
    return send_result;
}

int32_t rdplib_platform_send_datagram_to(intptr_t endpoint, const uint8_t *packet, uint32_t packet_bytes, const void *destination, uint32_t destination_bytes)
{
    return capture_send(endpoint, packet, packet_bytes, destination, destination_bytes);
}

uint32_t rdp_wake_test_usemaphore_decrement(usemaphore_t *semaphore, uint32_t timeout_ms)
{
    (void)semaphore;
    ++semaphore_wait_calls;
    semaphore_wait_timeout = timeout_ms;
    return 0;
}

static void reset_capture(int32_t result)
{
    memset(&capture, 0, sizeof(capture));
    send_result = result;
}

static void test_udp_preference_and_result(void)
{
    rdp_t owner;
    struct sockaddr_in sent_address;
    uint32_t token = UINT32_C(0x12345678);

    memset(&owner, 0, sizeof(owner));
    owner.udp_socket = 11;
    owner.ipx_socket = 22;
    owner.local_udp_addr.sin_family = RDP_TRANSMIT_ADDRESS_IPV4;
    owner.local_udp_addr.sin_port = htons(8123);
    owner.local_udp_addr.sin_addr.s_addr = htonl(UINT32_C(0x01020304));

    reset_capture((int32_t)sizeof(token));
    assert(rdp_wake(&owner, token) == 0);
    assert(capture.calls == 1 && capture.endpoint == owner.udp_socket);
    assert(capture.packet_bytes == sizeof(token) && memcmp(capture.packet, &token, sizeof(token)) == 0); // The recovered token is native endian.
    assert(capture.destination_bytes == sizeof(struct sockaddr_in));
    memcpy(&sent_address, capture.destination, sizeof(sent_address));
    assert(sent_address.sin_family == owner.local_udp_addr.sin_family);
    assert(sent_address.sin_port == owner.local_udp_addr.sin_port);
    assert(sent_address.sin_addr.s_addr == htonl(UINT32_C(0x7f000001)));
    assert(owner.local_udp_addr.sin_addr.s_addr == htonl(UINT32_C(0x01020304))); // Selection uses a copy.

    reset_capture((int32_t)sizeof(token) - 1);
    assert(rdp_wake(&owner, token) == 1);
    assert(capture.calls == 1 && capture.endpoint == owner.udp_socket);
}

static void test_ipx_fallback(void)
{
    rdp_t owner;
    uint32_t token = UINT32_C(0xa1b2c3d4);
    uint8_t *address;
    uint32_t index;

    memset(&owner, 0, sizeof(owner));
    owner.udp_socket = -1;
    owner.ipx_socket = 33;
    address = (uint8_t *)&owner.local_ipx_addr;
    for (index = 0; index < sizeof(owner.local_ipx_addr); ++index)
    {
        address[index] = (uint8_t)(index * 13u + 5u);
    }

    reset_capture((int32_t)sizeof(token));
    assert(rdp_wake(&owner, token) == 0);
    assert(capture.calls == 1 && capture.endpoint == owner.ipx_socket);
    assert(capture.packet_bytes == sizeof(token) && memcmp(capture.packet, &token, sizeof(token)) == 0);
    assert(capture.destination_bytes == 14 && memcmp(capture.destination, &owner.local_ipx_addr, 14) == 0);

    reset_capture(-1);
    assert(rdp_wake(&owner, token) == 1);
    assert(capture.calls == 1 && capture.endpoint == owner.ipx_socket);
}

static void test_no_transport(void)
{
    rdp_t owner;

    memset(&owner, 0, sizeof(owner));
    owner.udp_socket = -1;
    owner.ipx_socket = -1;
    reset_capture(-1);
    assert(rdp_wake(&owner, 7) == 0); // The source initializes char_sent to sizeof(msg) when no send is possible.
    assert(capture.calls == 0);
}

static void test_receive_preserves_high_bit_timeout(void)
{
    rdp_t owner;

    memset(&owner, 0, sizeof(owner));
    rdp_init(&owner);
    semaphore_wait_calls = 0;
    semaphore_wait_timeout = 0;
    assert(rdp_receive(&owner, UINT32_C(0x80000000)) == NULL);
    assert(semaphore_wait_calls == 1);
    assert(semaphore_wait_timeout == UINT32_C(0x80000000));
    rxq_destroy(&owner.message_rxq);
    rxq_destroy(&owner.external_rxq);
    umutex_destroy(&owner.message_rxq_mutex);
    umutex_destroy(&owner.external_rxq_mutex);
}

int main(void)
{
    test_udp_preference_and_result();
    test_ipx_fallback();
    test_no_transport();
    test_receive_preserves_high_bit_timeout();
    return 0;
}
