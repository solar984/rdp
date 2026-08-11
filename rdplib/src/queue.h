// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Receive and transmit queue types.
//
// PPC mangling proves the distinct _rxq_t name. It remains list compatible;
// the transmit queue additionally tracks serialized bytes.
#ifndef RDP_QUEUE_H
#define RDP_QUEUE_H

#include <stdint.h>

#include "container.h"
#include "packet.h"

typedef struct rdp_txq_t
{
    rdp_list_t messages;
    uint32_t queued_bytes;
} rdp_txq_t;

typedef struct _rxq_t
{
    rdp_list_t messages;
} rdp_rxq_t;

struct connection_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Rotates a nonempty queue and returns the wrap aware oldest first send time.
uint32_t txq_get_oldest_time_sent(rdp_txq_t *queue);

// Removes every duplicate ID if the live transport's uniqueness precondition was violated.
msg_outgoing_t *txq_remove_msgid(rdp_txq_t *queue, uint16_t message_id);

// Releases arrivals belonging to connection while preserving all other order.
uint32_t rxq_flush_all_messages(rdp_rxq_t *queue, struct connection_t *connection);

#ifdef __cplusplus
}
#endif

#endif /* RDP_QUEUE_H */
