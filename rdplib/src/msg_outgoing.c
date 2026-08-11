// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "packet.h"

#include <string.h>

#include "rdplib_constants.h"
#include "rdplib_platform.h"

void msg_outgoing_init(msg_outgoing_t *message)
{
    uint8_t *data = msg_outgoing_data(message);
    uint16_t network_value;

    message->link.value = message;
    message->link.key = NULL;
    message->serialized_bytes = 0;
    message->transmission_count = 0;

    if (message->flags & RDP_FLAG_MSGID)
    {
        network_value = htons(message->message_id);
        memcpy(data + message->serialized_bytes, &network_value, sizeof(network_value));
        message->serialized_bytes += sizeof(network_value);
    }

    if (message->flags & RDP_FLAG_FRAGMENT)
    {
        network_value = htons(message->fragment_id);
        memcpy(data + message->serialized_bytes, &network_value, sizeof(network_value));
        message->serialized_bytes += sizeof(network_value);
        network_value = htons(message->fragment_index);
        memcpy(data + message->serialized_bytes, &network_value, sizeof(network_value));
        message->serialized_bytes += sizeof(network_value);
        network_value = htons(message->fragment_count);
        memcpy(data + message->serialized_bytes, &network_value, sizeof(network_value));
        message->serialized_bytes += sizeof(network_value);
    }

    if (message->flags & RDP_FLAG_SEQUENCED)
    {
        data[message->serialized_bytes++] = message->stream_id;
        if (message->flags & RDP_FLAG_MSGID)
        {
            data[message->serialized_bytes++] = message->stream_sequence;
        }
    }

    message->_f002C = 0;
}
