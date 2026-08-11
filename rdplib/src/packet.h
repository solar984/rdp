// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Decoded RDP header and transport message records.
//
// These are source level structures with host pointers, not packed wire or
// 32 bit client ABI declarations. Arrival and outgoing allocations append
// their variable payload bytes immediately after the structure.
#ifndef RDP_PACKET_H
#define RDP_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "container.h"

typedef struct _rdp_header_t
{
    uint16_t flags;
    uint16_t sequence;
    const uint8_t *ack_data; // Points into the datagram being parsed.
    uint16_t message_id;
    uint16_t fragment_id;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint8_t stream_id;
    uint8_t stream_sequence;
    uint16_t header_bytes;
    uint16_t payload_bytes;
} _rdp_header_t;

typedef struct msg_arrival_t
{
    rdp_list_link_t link; // Its key is repurposed as the message moves between receive lists.
    void *sender_connection;
    uint16_t flags;
    uint16_t sequence;
    uint16_t message_id;
    uint16_t fragment_id;
    uint16_t fragment_count;
    uint8_t stream_id;
    uint8_t stream_sequence;
    uint16_t fragments_received;
    uint32_t payload_bytes;
    uint32_t _f0028;
    uint8_t sender_address[16]; // Used when no connection object owns the arrival.
} msg_arrival_t;

typedef struct msg_outgoing_t
{
    rdp_list_link_t link;
    uint32_t first_sent_time_ms; // Lifetime origin; retransmission never resets it.
    uint32_t last_sent_time_ms;  // RTO origin for the most recent transmission.
    uint16_t flags;
    uint16_t message_id;
    uint16_t fragment_id;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint8_t stream_id;
    uint8_t stream_sequence;
    uint16_t transmission_count; // RTT accepts only a message transmitted exactly once.
    uint32_t serialized_bytes;
    uint32_t _f002C;
} msg_outgoing_t;

static inline uint8_t *msg_arrival_data(msg_arrival_t *message)
{
    return message->payload_bytes ? (uint8_t *)(message + 1) : NULL;
}

static inline uint8_t *msg_outgoing_data(msg_outgoing_t *message)
{
    return (uint8_t *)(message + 1);
}

void msg_outgoing_init(msg_outgoing_t *message);
void msg_arrival_init(msg_arrival_t *message, uint16_t fragment_id);
void msg_arrival_prepare_for_sequencer(msg_arrival_t *message);
void msg_arrival_prepare_for_rxq(msg_arrival_t *message);
int msg_arrival_assemble(msg_arrival_t *message, void *sender_connection, const _rdp_header_t *header, const void *payload);
void msg_arrival_init_disconnect_msg(msg_arrival_t *message, void *sender_connection);
int msg_arrival_validate_fragment_arrival(const msg_arrival_t *message, const _rdp_header_t *header);
uint32_t msg_arrival_get_size(const msg_arrival_t *message);
uint8_t *msg_arrival_get_data(msg_arrival_t *message);
void *msg_arrival_get_sender(const msg_arrival_t *message);
int msg_arrival_has_fin(const msg_arrival_t *message);

#endif /* RDP_PACKET_H */
