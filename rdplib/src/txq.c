// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "queue.h"

#include "packet.h"

uint32_t txq_get_oldest_time_sent(rdp_txq_t *queue)
{
    uint32_t remaining = queue->messages.count;
    msg_outgoing_t *message = (msg_outgoing_t *)queue->messages.head->value;
    uint32_t oldest = message->first_sent_time_ms;

    while (remaining--)
    {
        message = (msg_outgoing_t *)list_remove_head(&queue->messages);

        // Signed subtraction preserves the client's ordering across the
        // 32 bit millisecond clock rollover.
        if ((int32_t)(oldest - message->first_sent_time_ms) > 0)
        {
            oldest = message->first_sent_time_ms;
        }

        list_add_tail(&queue->messages, &message->link);
    }

    return oldest;
}

msg_outgoing_t *txq_remove_msgid(rdp_txq_t *queue, uint16_t message_id)
{
    uint32_t remaining = queue->messages.count;
    msg_outgoing_t *removed = 0;

    while (remaining--)
    {
        msg_outgoing_t *message = (msg_outgoing_t *)list_remove_head(&queue->messages);

        if (message->message_id == message_id)
        {
            queue->queued_bytes -= message->serialized_bytes;
            removed = message;
        }
        else
        {
            list_add_tail(&queue->messages, &message->link);
        }
    }

    return removed;
}
