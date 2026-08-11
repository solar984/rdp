// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Portable packet header construction recovered from tx_send_packet.
//
// These are logical protocol records, not overlays for either client ABI.
// The original connection layouts remain in connection.h and
// connection_windows.h.
#ifndef RDP_TX_H
#define RDP_TX_H

#include <stdint.h>

#include "bandwidth.h"
#include "event.h"
#include "queue.h"
#include "rdplib_constants.h"
#include "rx.h"
#include "timeout.h"

typedef struct rdplib_platform_event_t rdplib_platform_event_t;
struct connection_t;
struct rdp_t;

enum rdp_transmit_address_family
{
    RDP_TRANSMIT_ADDRESS_IPV4 = 2,
    RDP_TRANSMIT_ADDRESS_IPX = 6,
    RDP_TRANSMIT_ADDRESS_SERIAL = 69
};

// Logical fields initialized and owned by the tx_* functions. Fields with
// unknown names retain their evidence offsets until a reader is recovered.
typedef struct rdp_transmit_initialization_state_t
{
    intptr_t send_socket;
    uint8_t remote_address[16];
    uint16_t address_family; // Source level replacement for reading sockaddr family bytes directly.
    uint16_t acknowledged_through_message_id;
    uint16_t next_packet_sequence;
    uint16_t reliable_next_message_id;
    uint16_t next_fragment_id;
    uint32_t last_reliable_enqueue_time_ms;
    _bitarray_t outstanding_message_ids;
    rdp_txq_t sent_messages;
    rdp_txq_t ready_messages;
    rdp_txq_t window_blocked_messages;
    _bandwidth_t bandwidth;
    uint32_t send_buffer_limit;
    uint32_t tx_initial_time_ms; // Written once by tx_init; no transport reader found.
    uint32_t tx_reserved_zero;   // Adjacent 0 with no transport reader found.
    _timeout_t rtt_estimator;
    uint8_t next_outgoing_stream_sequence[RDP_STREAM_COUNT];
    uint32_t syn_sent;
    uint32_t syn_acknowledged;
    uint16_t initial_outgoing_message_id;
    uint32_t fin_sent;
    uint32_t fin_ack_seen;
    uint16_t fin_message_id;
    uint32_t connected;
    uint32_t transmit_stopped;
    uint32_t disconnect_reason;
    uint32_t disconnect_message_queued;
    uint32_t unacknowledged_message_timeout_ms;
    uint32_t connection_inactivity_timeout_ms;
    uint32_t delayed_ack_pending;
    uint32_t delayed_ack_deadline_ms;
    uint16_t last_ping_sample_ms;
    rdplib_platform_event_t *close_event;
    int *close_result;
    uint8_t trace_destination[16];
    intptr_t trace_socket;
    uint32_t trace_socket_default_ttl;
} rdp_transmit_initialization_state_t;

enum
{
    RDP_WIRE_HEADER_BASE_BYTES = 4,
    RDP_WIRE_HEADER_MAX_BYTES = RDP_WIRE_HEADER_BASE_BYTES + RDP_ACK_MAX_BYTES
};

#ifdef __cplusplus
extern "C"
{
#endif

// Reconstructs the complete logical tx_init store sequence from the owning
// RDP object and the peer sockaddr bytes.
//
// The 3 queues are initialized but their byte totals are
// untouched; connection_create calls tx_create afterward to set those totals.
// An unsupported address family also leaves send_socket untouched, matching
// the original unchecked precondition. The source faithful build produces the
// initial message ID with the recovered time_get_ms, srand, rand sequence;
// the default build uses the isolated host random service.
void tx_init(struct connection_t *connection, struct rdp_t *owner, const uint8_t remote_address[16]);

// Creates the connection's 3 unsorted outbound lists and clears their byte totals.
// tx_init must have initialized their list storage first. Like the clients,
// this operation cannot fail and returns 0.
int tx_create(struct connection_t *connection);

// Releases every transport owned outgoing message in blocked, ready, sent
// order. Exclusive connection access and fast allocator ownership of every
// queued message are required.
void tx_flush_output_buffers(struct connection_t *connection);

// Flushes outbound ownership and fails and wakes an installed close waiter.
// The waiter owns and destroys its synchronization object after it wakes.
void tx_destroy(struct connection_t *connection);

// Completes a waiting close successfully, flushes every outbound queue, and
// then latches peer STOP. The completion before flush order is intentional.
void tx_received_stopped(struct connection_t *connection);

// Aborts a connection, releases all transport owned input and output,
// records process wide reason statistics, and fails an installed close waiter.
// Exclusive connection access and a valid g_rdp_stat are required.
void tx_abort_connection(struct connection_t *connection, uint32_t reason);

// Sends a never transmitted record and publishes its final ownership.
//
// The selected build time backend consumes the borrowed vectors synchronously. An
// unreliable allocation is released immediately after the send returns.
// A reliable allocation is queued afterward even if the backend aborted the
// connection, preserving the original send error ordering.
void tx_send_virgin(struct connection_t *connection, msg_outgoing_t *message);

// Applies SYN, the 120 message window, bandwidth/serial readiness, and ready FIFO rules.
// The caller transfers message ownership unconditionally. Serial connections use the owner's
// build selected serial backend directly.
void tx_enqueue_outgoing(struct connection_t *connection, msg_outgoing_t *message);

// Queues the connection's reliable FIN.  The default build also reports a full reliable history.
int tx_send_fin(struct connection_t *connection);

// Queues a reliable SYSTEM keepalive.  The default build preserves the final history position for FIN.
int tx_send_alive(struct connection_t *connection);

// Builds and sends an RDP datagram through UDP or serial.
//
// The caller must hold exclusive connection access. The backend consumes
// both borrowed vectors synchronously and returns the original usend/serial_send
// status.  Only result 0 advances the packet sequence; result 1 aborts and
// result 5 charges 1/10 of the configured byte rate to the bandwidth
// model.  The source faithful build consumes ACK state before the backend result
// is known.  The default build restores it when the backend would block.
int tx_send_packet(struct connection_t *connection, const uint8_t *data, uint32_t bytes, uint16_t flags);

// Performs at most 1 transmit work item for a connection.
//
// The caller must hold exclusive connection access. Serial connections require
// serial_time_empty, and trace_start is required
// when the traceroute option is enabled. Like the clients, backend send
// failure can leave the current reliable message queued on an aborted connection.
void tx_tx(struct connection_t *connection);

// Retires an acknowledged reliable ID and applies its connection level effects.
//
// The caller must hold exclusive connection access. A missing sent record still
// counts as a processed ACK and can establish SYN acknowledgement or complete FIN
// state. Close wait completion compares the already published cumulative ACK base,
// which may intentionally lag the message_id argument during ACKTHRU processing.
void tx_handle_ack(struct connection_t *connection, uint16_t message_id);

// Validates a packet's ACK fields and reports their encoded byte count.
//
// Structural errors abort the connection.  A structurally valid claim at or
// beyond the next unsent message ID, or more than 4096 IDs behind it, is
// discarded.  The default build also requires each newly claimed outstanding
// ID to have entered the sent queue.  This is the recovered source level entry
// point.
rdp_rx_arrival_disposition_t tx_validate_ack_arrival(struct connection_t *connection, const _rdp_header_t *header, uint32_t *field_bytes);

// Applies a decoded packet's ACK fields to the connection's transmit state.
//
// This retires sent records, re admits messages entering the 120 message
// window, and preserves the clients' counter bug: a packet retiring no
// reliable IDs increments redundant ACK counters even with no ACK field.
void tx_record_ack_arrival(struct connection_t *connection, const _rdp_header_t *header);

// Arms the non extending 50 ms delayed ACK timer.
void tx_set_delayed_ack(struct connection_t *connection);

// Selects the next transmit deadline after the backend availability prediction.
void tx_get_event_time(struct connection_t *connection, rdp_timeout_data_t *timeout);

// Reports whether a ready message should precede retransmission of the sent head.
int tx_send_ready_virgins(struct connection_t *connection);

// Returns the sent head age beyond a retransmission delay, or 0.
uint32_t tx_get_stall_time(struct connection_t *connection);

#ifdef __cplusplus
}
#endif

#endif /* RDP_TX_H */
