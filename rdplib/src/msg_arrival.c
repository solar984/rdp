// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "packet.h"

#include <string.h>

#include "rdplib_constants.h"

void msg_arrival_init(msg_arrival_t *message, uint16_t fragment_id)
{
    message->link.value = message;
    message->link.key = &message->fragment_id;
    message->sender_connection = NULL;
    message->fragment_id = fragment_id;
    message->fragments_received = 0;
}

void msg_arrival_prepare_for_sequencer(msg_arrival_t *message)
{
    message->link.key = &message->stream_sequence;
}

void msg_arrival_prepare_for_rxq(msg_arrival_t *message)
{
    message->link.key = NULL;
}

int msg_arrival_assemble(msg_arrival_t *message, void *sender_connection, const _rdp_header_t *header, const void *payload)
{
    uint32_t payload_offset;

    if (header->fragment_index == 0)
    {
        message->flags = header->flags;
        message->sequence = header->sequence;
        message->message_id = header->message_id;
        message->fragment_count = header->fragment_count;
        message->stream_id = header->stream_id;
        message->stream_sequence = header->stream_sequence;
        message->sender_connection = sender_connection;
    }

    ++message->fragments_received;
    payload_offset = (uint32_t)header->fragment_index * RDP_FRAGMENT_PAYLOAD_BYTES;
    memcpy((uint8_t *)(message + 1) + payload_offset, payload, header->payload_bytes);

    if ((uint16_t)(header->fragment_index + 1u) == header->fragment_count)
    {
        message->payload_bytes = payload_offset + header->payload_bytes;
    }

    return message->fragments_received == header->fragment_count;
}

void msg_arrival_init_disconnect_msg(msg_arrival_t *message, void *sender_connection)
{
    memset(message, 0, sizeof(*message));
    message->link.value = message;
    message->sender_connection = sender_connection;
}

int msg_arrival_validate_fragment_arrival(const msg_arrival_t *message, const _rdp_header_t *header)
{
    uint16_t expected_message_id;

    if (!message->sender_connection)
    {
        return 0;
    }

    expected_message_id = (uint16_t)(message->message_id + header->fragment_index);
    if (message->fragment_count != header->fragment_count || expected_message_id != header->message_id)
    {
        return 2;
    }

    return 0;
}

uint32_t msg_arrival_get_size(const msg_arrival_t *message)
{
    return message->payload_bytes;
}

uint8_t *msg_arrival_get_data(msg_arrival_t *message)
{
    return msg_arrival_data(message);
}

void *msg_arrival_get_sender(const msg_arrival_t *message)
{
    return message->sender_connection;
}

int msg_arrival_has_fin(const msg_arrival_t *message)
{
    return (message->flags & RDP_FLAG_FIN) != 0;
}
