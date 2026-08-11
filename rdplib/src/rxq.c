// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "queue.h"

#include "fast.h"

uint32_t rxq_flush_all_messages(rdp_rxq_t *queue, struct connection_t *connection)
{
    uint32_t remaining = queue->messages.count;
    uint32_t released = 0;

    while (remaining--)
    {
        msg_arrival_t *message = (msg_arrival_t *)list_remove_head(&queue->messages);

        if (message->sender_connection == connection)
        {
            ++released;
            fast_free(message);
        }
        else
        {
            list_add_tail(&queue->messages, &message->link);
        }
    }

    return released;
}
