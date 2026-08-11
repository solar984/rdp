// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Portable receive bookkeeping recovered from the rx_* family.
//
// These are logical protocol records, not overlays for either client ABI.
// The recovered connection layouts remain in connection.h and
// connection_windows.h.
#ifndef RDP_RX_H
#define RDP_RX_H

#include <stdint.h>

#include "bitarray.h"
#include "packet.h"
#include "rdplib_constants.h"

#ifdef __cplusplus
#define RDP_RX_DEFAULT(value) = value
#else
#define RDP_RX_DEFAULT(value)
#endif

typedef enum rdp_rx_arrival_disposition_t
{
    RDP_RX_ACCEPT = 0,
    RDP_RX_DISCARD = 1,
    RDP_RX_ABORT = 2
} rdp_rx_arrival_disposition_t;

// Bit 0 represents last_received_packet_sequence - 1. The latest packet
// sequence is not stored because an equal sequence is always a duplicate.
typedef struct rdp_packet_sequence_receive_state_t
{
    uint16_t last_received_packet_sequence RDP_RX_DEFAULT(UINT16_C(0xFFFF));
    uint64_t received_packet_sequence_history RDP_RX_DEFAULT(UINT64_MAX);
} rdp_packet_sequence_receive_state_t;

// The contiguous 0x10C byte counter block cleared by rx_init. The source
// initializer owns transmit, receive, validation, and ICMP counters together
// even though most later mutations belong to tx or owner level ICMP code.
typedef struct rdp_receive_statistics_t
{
    uint32_t unreliable_packets_sent RDP_RX_DEFAULT(0);
    uint32_t unreliable_bytes_sent RDP_RX_DEFAULT(0);
    uint32_t reliable_packets_sent RDP_RX_DEFAULT(0);
    uint32_t reliable_bytes_sent RDP_RX_DEFAULT(0);
    uint32_t reliable_packets_retransmitted RDP_RX_DEFAULT(0);
    uint32_t reliable_bytes_retransmitted RDP_RX_DEFAULT(0);
    uint32_t ack_only_packets_sent RDP_RX_DEFAULT(0);
    uint32_t piggybacked_ack_packets_sent RDP_RX_DEFAULT(0);
    uint32_t unreliable_packets_received RDP_RX_DEFAULT(0);
    uint32_t unreliable_payload_bytes_received RDP_RX_DEFAULT(0);
    uint32_t reliable_packets_received RDP_RX_DEFAULT(0);
    uint32_t reliable_payload_bytes_received RDP_RX_DEFAULT(0);
    uint32_t duplicate_reliable_packets_received RDP_RX_DEFAULT(0);
    uint32_t duplicate_reliable_payload_bytes_received RDP_RX_DEFAULT(0);
    uint32_t received_header_bytes RDP_RX_DEFAULT(0);
    uint32_t ack_only_packets_received RDP_RX_DEFAULT(0);
    uint32_t piggybacked_ack_packets_received RDP_RX_DEFAULT(0);
    uint32_t ack_message_ids_processed RDP_RX_DEFAULT(0);
    uint32_t redundant_ack_packets_received RDP_RX_DEFAULT(0);
    uint32_t redundant_ack_header_bytes_received RDP_RX_DEFAULT(0);
    uint32_t future_or_unsent_ack_count RDP_RX_DEFAULT(0);
    uint32_t advancing_packet_sequences_received RDP_RX_DEFAULT(0);
    uint32_t advancing_packet_sequence_bytes_received RDP_RX_DEFAULT(0);
    uint32_t reordered_packet_sequences_received RDP_RX_DEFAULT(0);
    uint32_t reordered_packet_sequence_bytes_received RDP_RX_DEFAULT(0);
    uint32_t invalid_datagram_flags_count RDP_RX_DEFAULT(0);
    uint32_t invalid_packet_sequence_count RDP_RX_DEFAULT(0);
    uint32_t duplicate_packet_sequence_count RDP_RX_DEFAULT(0);
    uint32_t invalid_message_id_count RDP_RX_DEFAULT(0);
    uint32_t invalid_fragment_count RDP_RX_DEFAULT(0);
    uint32_t invalid_stream_id_count RDP_RX_DEFAULT(0);
    uint32_t short_packet_count RDP_RX_DEFAULT(0);
    uint32_t invalid_fragment_group_count RDP_RX_DEFAULT(0);
    uint32_t conflicting_ack_flags_count RDP_RX_DEFAULT(0);
    uint32_t empty_ack_mask_tail_count RDP_RX_DEFAULT(0);
    uint32_t ack_mask_without_base_count RDP_RX_DEFAULT(0);
    uint32_t ancient_ack_count RDP_RX_DEFAULT(0);
    uint32_t rtt_samples_accepted RDP_RX_DEFAULT(0);
    uint32_t sent_messages_acked RDP_RX_DEFAULT(0);
    uint32_t icmp_destination_unreachable_by_code[16];
    uint32_t icmp_source_quench_count RDP_RX_DEFAULT(0);
    uint32_t icmp_time_exceeded_by_code[2];
    uint32_t icmp_parameter_problem_by_code[2];
    uint32_t icmp_invalid_code_count RDP_RX_DEFAULT(0);
    uint32_t reserved[6];
} rdp_receive_statistics_t;

// ACK construction owns the contiguous reliable base and its 4096 bit
// receive history. Receive recording and transmit serialization
// share this record so the 2 paths cannot develop competing copies.
typedef struct rdp_receive_ack_state_t
{
    uint16_t received_through_message_id RDP_RX_DEFAULT(0);
    _bitarray_t received_message_ids;
    uint16_t unreported_message_count RDP_RX_DEFAULT(0);
    uint16_t unreported_min_message_id RDP_RX_DEFAULT(0);
    uint16_t unreported_max_message_id RDP_RX_DEFAULT(0);
} rdp_receive_ack_state_t;

typedef struct rdp_receive_recording_state_t
{
    int message_id_receive_initialized RDP_RX_DEFAULT(0);
    uint16_t initial_message_id RDP_RX_DEFAULT(0);
    uint16_t highest_received_message_id RDP_RX_DEFAULT(0);
    uint32_t last_reliable_receive_time_ms RDP_RX_DEFAULT(0);
    rdp_packet_sequence_receive_state_t packet_sequence;
    uint16_t stream_sequence_reset_reference RDP_RX_DEFAULT(0);
    uint16_t next_unreliable_sequence[RDP_STREAM_COUNT];
    rdp_receive_statistics_t statistics;
} rdp_receive_recording_state_t;

// Native pointer ownership fields consumed by the receive list, saved FIN,
// and teardown functions. This is a logical subset of connection_t, not an
// overlay for either 32 bit client ABI.
typedef struct rdp_receive_ownership_state_t
{
    rdp_list_t fragment_messages;
    uint8_t next_ordered_stream_sequence[RDP_STREAM_COUNT];
    rdp_list_t receive_streams[RDP_STREAM_COUNT];
    uint16_t fin_arrival_pending;
    uint16_t fin_message_id;
    msg_arrival_t *saved_fin_arrival;
    void *trace_samples;
} rdp_receive_ownership_state_t;

// Logical fields initialized by rx_init. Guarded fields that the
// client body leaves untouched are retained here so tests can
// distinguish source initialization from convenient 0 construction.
typedef struct rdp_receive_initialization_state_t
{
    rdp_receive_recording_state_t recording;
    rdp_receive_ack_state_t ack;
    rdp_receive_ownership_state_t ownership;
    uint32_t last_packet_receive_time_ms;
    uint8_t last_icmp_type;
    uint8_t last_icmp_code;
    uint16_t _icmp_padding;
    uint32_t last_icmp_time_ms;
    uint32_t icmp_count;
    uint32_t last_icmp_source;
    void *completed_trace_samples;
    uint32_t completed_trace_sample_count;
    uint32_t completed_trace_time;
    uint32_t trace_in_flight;
    uint32_t trace_last_send_time_ms;
    uint32_t trace_started_time_ms;
    uint32_t trace_started_wall_time;
    uint32_t trace_ttl;
    uint32_t trace_ttl_limit;
    uint32_t trace_sweep_count;
    uint32_t trace_sample_index;
} rdp_receive_initialization_state_t;

struct connection_t;

enum
{
    RDP_ACK_MAX_BYTES = 17
};

#ifdef __cplusplus
extern "C"
{
#endif

// Initializes all receive bookkeeping and intrusive list heads for a
// connection. EQMac PPC and TAKP Windows seed every bit of the 64 bit packet
// history to 1. EQMac Intel alone clears its upper word;
void rx_init(struct connection_t *connection);

// Configures the fragment and ordered stream lists created for a connection.
// The lists must already have been initialized by rx_ownership_state_init.
// Like the clients, this always returns 0.
int rx_create(struct connection_t *connection);

// Releases receive owned messages, destroys the intrusive lists, and frees
// the optional active traceroute sample array. Exclusive connection access is
// required; the original function performs no locking of its own.
void rx_destroy(struct connection_t *connection);

// Releases all saved, fragment, and ordered stream arrivals without resetting
// sequence cursors or the separate FIN latch. Exclusive access is required.
void rx_flush_input_buffers(struct connection_t *connection);

// Saves an ordered FIN until the cumulative reliable receive base reaches its
// message ID. An existing saved pointer is overwritten without being freed.
void rx_save_fin_arrival(struct connection_t *connection, msg_arrival_t *message);

// Transfers the saved FIN pointer to the caller. The separate pending latch
// and message ID remain unchanged.
msg_arrival_t *rx_load_fin_arrival(struct connection_t *connection);

// Inserts a reliable sequenced message into its stream's stable wrap aware
// queue. stream_id must be in range, the message must not already be linked,
// and reliable ID deduplication must have excluded an equal live sequence.
void rx_sort_into_sequence(struct connection_t *connection, msg_arrival_t *message);

// Removes the stream head only when its 8 bit sequence is exactly the next
// expected value, advancing the cursor with natural byte rollover. A repeated
// live sequence would block this queue; the 120 ID reliable window and earlier
// duplicate filter are the source level precondition that prevents it.
msg_arrival_t *rx_get_next_in_sequence(struct connection_t *connection, uint8_t stream_id);

// Allocates or locates an arrival, copies an accepted payload fragment, and
// returns ownership only when the logical message is complete. The caller must
// first apply the recovered fragment validator and reliable duplicate filter.
// sender_connection is borrowed into the completed message.
msg_arrival_t *rx_assemble(struct connection_t *connection, const _rdp_header_t *header, const void *payload);

// Refreshes receive activity with the source routine's clock sample.
void rx_record_packet_arrival(struct connection_t *connection);

// Applies the 120 message forward window and repeated SYN identity rule.
// Old and duplicate IDs remain valid here; rx_record_msgid_arrival classifies
// them after the packet passes structural validation.
rdp_rx_arrival_disposition_t rx_validate_msgid_arrival(struct connection_t *connection, const _rdp_header_t *header);

// Applies the 64 packet duplicate history and the 4096 packet forward limit.
rdp_rx_arrival_disposition_t rx_validate_seqnum_arrival(struct connection_t *connection, uint16_t sequence);

// Stream IDs form a fixed 0..19 namespace. An invalid stream aborts the
// connection instead of merely discarding the datagram.
rdp_rx_arrival_disposition_t rx_validate_stream_arrival(struct connection_t *connection, const _rdp_header_t *header);

// Validates the wire fragment shape and, when initialized is nonzero, its
// relationship to an existing fragment group. The clients permit a final
// payload longer than 512 bytes; allocation safety remains the caller's job.
rdp_rx_arrival_disposition_t rx_validate_fragment_arrival(struct connection_t *connection, const _rdp_header_t *header);

// Records a packet sequence that already passed rx_validate_seqnum_arrival.
// Equality and a history bit collision are therefore unreachable here.
void rx_record_seqnum_arrival(struct connection_t *connection, const _rdp_header_t *header);

// Records a reliable message ID and returns nonzero if it was already present.
// Every call updates the pending ACK range, including old and duplicate IDs.
int rx_record_msgid_arrival(struct connection_t *connection, uint16_t message_id);

// Applies the sequenced unreliable 16 bit packet sequence receive floor.
// Missing sequences are never waited for: an accepted arrival advances the
// floor directly to packet sequence + 1. The 8 bit reliable stream sequence
// is a separate field and is not consulted here.
int rx_in_sequence(struct connection_t *connection, msg_arrival_t *message);

// Appends ACKTHRU or MASKOFFSET to output and consumes the unreported count.
// output must provide RDP_ACK_MAX_BYTES. flags remains in host byte order.
uint32_t rx_append_ack(struct connection_t *connection, uint16_t *output, uint16_t *flags);

#ifdef __cplusplus
}
#endif

#undef RDP_RX_DEFAULT

#endif /* RDP_RX_H */
