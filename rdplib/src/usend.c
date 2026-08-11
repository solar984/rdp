// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "usend.h"

#include <stdlib.h>
#include <string.h>

#ifndef RDPLIB_SOURCE_FAITHFUL
#include "connection.h"
#endif
#include "framing.h"
#include "rdplib_platform.h"

enum
{
    RDP_USEND_STACK_BYTES = 32768,
    RDP_USEND_WOULD_BLOCK = 10035,
    RDP_USEND_INVALID_ARGUMENT = 6,
    RDP_USEND_CAPACITY_EXCEEDED = 18
};

#ifdef RDPLIB_SOURCE_FAITHFUL
int usend(intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc)
#else
static int rdplib_usend_internal(struct connection_t *connection, intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc)
#endif
{
    uint8_t packet[RDP_USEND_STACK_BYTES];
    uint32_t network_crc;
    uint32_t packet_bytes = 0;
    uint32_t unpadded_bytes;
    uint32_t padded_bytes;
    uint32_t index;
    int32_t sent_bytes;

#ifndef RDPLIB_SOURCE_FAITHFUL
    {
        uint32_t validated_bytes = 0;
        uint32_t framed_bytes;

        if ((!buffers && buffer_count != 0) || !destination)
        {
            return RDP_USEND_INVALID_ARGUMENT;
        }
        for (index = 0; index < buffer_count; ++index)
        {
            if ((!buffers[index].data && buffers[index].bytes != 0) || buffers[index].bytes > RDP_USEND_STACK_BYTES - validated_bytes)
            {
                return buffers[index].data || buffers[index].bytes == 0 ? RDP_USEND_CAPACITY_EXCEEDED : RDP_USEND_INVALID_ARGUMENT;
            }
            validated_bytes += buffers[index].bytes;
        }

        framed_bytes = validated_bytes;
        if (use_encryption || use_crc)
        {
            if (framed_bytes > RDP_USEND_STACK_BYTES - sizeof(uint32_t))
            {
                return RDP_USEND_CAPACITY_EXCEEDED;
            }
            framed_bytes += sizeof(uint32_t);
        }
        if (use_encryption && (framed_bytes > RDP_USEND_STACK_BYTES - 8u || ((framed_bytes + 8u) & ~UINT32_C(7)) > RDP_USEND_STACK_BYTES))
        {
            return RDP_USEND_CAPACITY_EXCEEDED;
        }
    }
#endif

    for (index = 0; index < buffer_count; ++index)
    {
        memcpy(packet + packet_bytes, buffers[index].data, buffers[index].bytes);
        packet_bytes += buffers[index].bytes;
    }

#ifndef RDPLIB_SOURCE_FAITHFUL
    if (connection && connection->rdplib_packet_drop_callback &&
        connection->rdplib_packet_drop_callback(connection->rdplib_packet_drop_context, RDPLIB_PACKET_DROP_OUTBOUND, packet, packet_bytes))
    {
        return 0;
    }
#endif

    if (use_encryption || use_crc)
    {
        network_crc = htonl(rdp_crc(0, packet, packet_bytes));
        memcpy(packet + packet_bytes, &network_crc, sizeof(network_crc));
        packet_bytes += sizeof(network_crc);
    }

    if (use_encryption)
    {
        unpadded_bytes = packet_bytes;
        padded_bytes = (packet_bytes + 8u) & ~UINT32_C(7);
        while (packet_bytes < padded_bytes)
        {
            packet[packet_bytes++] = (uint8_t)rand();
        }
        packet[padded_bytes - 1u] = (uint8_t)((packet[padded_bytes - 1u] & 0xF0u) | (padded_bytes - unpadded_bytes));
        rdp_encode(packet, (int)(padded_bytes / 8u));
        packet_bytes = padded_bytes;
    }

    sent_bytes = rdplib_platform_send_datagram(endpoint, packet, packet_bytes, destination);
    if (sent_bytes != (int32_t)packet_bytes)
    {
        return rdplib_platform_last_socket_error() == RDP_USEND_WOULD_BLOCK ? 5 : 1;
    }
    return 0;
}

#ifndef RDPLIB_SOURCE_FAITHFUL
int usend(intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc)
{
    return rdplib_usend_internal(NULL, endpoint, buffers, buffer_count, destination, use_encryption, use_crc);
}

int rdplib_usend(struct connection_t *connection, intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc)
{
    return rdplib_usend_internal(connection, endpoint, buffers, buffer_count, destination, use_encryption, use_crc);
}
#endif
