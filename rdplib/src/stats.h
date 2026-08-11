// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Public performance snapshot filled by connection_get_perf_stats.
//
// The fields and their meaning are the same in all 3 TAKP game clients.
// Their ABI layout is not. The Mac compilers place the 64 bit receive history
// value at +0x04, while MSVC aligns it to +0x08 and leaves +0x04 unused.
// Store the history as 2 client layout words so these records retain their
// exact 32 bit layouts when compiled on a 64 bit host.
#ifndef RDP_STATS_H
#define RDP_STATS_H

#include <stdint.h>
#include "layout.h"

// Recovered fields in the process wide statistics block referenced by
// g_rdp_stat. Named fields and offsets are verified through +0x868; the
// original block's total size is unknown. Transport callers require a
// nonnull pointer to zeroed storage. Updates from separate connections
// are not synchronized.
typedef struct rdp_global_statistics_t
{
    /* 0x000 */ uint64_t unreliable_packets_sent;
    /* 0x008 */ uint64_t unreliable_bytes_sent;
    /* 0x010 */ uint64_t reliable_packets_sent;
    /* 0x018 */ uint64_t reliable_bytes_sent;
    /* 0x020 */ uint64_t reliable_packets_retransmitted;
    /* 0x028 */ uint64_t reliable_bytes_retransmitted;
    /* 0x030 */ uint64_t ack_only_packets_sent;
    /* 0x038 */ uint64_t piggybacked_ack_packets_sent;
    /* 0x040 */ uint64_t unreliable_packets_received;
    /* 0x048 */ uint64_t unreliable_payload_bytes_received;
    /* 0x050 */ uint64_t reliable_packets_received;
    /* 0x058 */ uint64_t reliable_payload_bytes_received;
    /* 0x060 */ uint64_t duplicate_reliable_packets_received;
    /* 0x068 */ uint64_t duplicate_reliable_payload_bytes_received;
    /* 0x070 */ uint64_t received_header_bytes;
    /* 0x078 */ uint64_t ack_only_packets_received;
    /* 0x080 */ uint64_t piggybacked_ack_packets_received;
    /* 0x088 */ uint64_t acknowledgement_message_ids_processed;
    /* 0x090 */ uint64_t packets_without_new_acknowledgements;
    /* 0x098 */ uint64_t nominal_ack_header_bytes_without_new_acknowledgements;
    /* 0x0A0 */ uint64_t future_or_unsent_acknowledgements;
    /* 0x0A8 */ uint64_t advancing_packet_sequences_received;
    /* 0x0B0 */ uint64_t advancing_packet_sequence_bytes_received;
    /* 0x0B8 */ uint64_t reordered_packet_sequences_received;
    /* 0x0C0 */ uint64_t reordered_packet_sequence_bytes_received;
    /* 0x0C8 */ uint64_t application_arrivals_discarded_during_linger;
    /* 0x0D0 */ uint64_t application_arrivals_discarded_after_disconnect;
    /* 0x0D8 */ uint64_t invalid_datagram_flags;
    /* 0x0E0 */ uint64_t invalid_packet_sequences;
    /* 0x0E8 */ uint64_t duplicate_packet_sequences;
    /* 0x0F0 */ uint64_t invalid_message_ids;
    /* 0x0F8 */ uint64_t invalid_fragment_headers;
    /* 0x100 */ uint64_t invalid_stream_ids;
    /* 0x108 */ uint64_t short_datagrams;
    /* 0x110 */ uint64_t invalid_fragment_groups;
    /* 0x118 */ uint64_t conflicting_ack_base_flags;
    /* 0x120 */ uint64_t empty_ack_mask_tails;
    /* 0x128 */ uint64_t ancient_acknowledgements;
    /* 0x130 */ uint64_t ack_masks_without_base;
    /* 0x138 */ uint64_t invalid_local_wakeup_datagrams;
    /* 0x140 */ uint64_t invalid_encrypted_datagram_sizes;
    /* 0x148 */ uint64_t illegal_framed_datagrams;
    /* 0x150 */ uint64_t outgoing_connection_attempts;
    /* 0x158 */ uint64_t incoming_connection_attempts;
    /* 0x160 */ uint64_t rejected_incoming_syn_datagrams;
    /* 0x168 */ uint64_t unknown_endpoint_datagrams;
    /* 0x170 */ uint64_t syn_acknowledgements;
    /* 0x178 */ uint64_t _unrecovered_before_abort[2];
    /* 0x188 */ uint64_t aborts_before_syn_acknowledgement;
    /* 0x190 */ uint64_t rtt_samples_accepted;
    /* 0x198 */ uint64_t sent_messages_acknowledged;
    /* 0x1A0 */ uint64_t io_wait_nonpositive_returns;
    /* 0x1A8 */ uint64_t protocol_error_disconnects;
    /* 0x1B0 */ uint64_t peer_reset_disconnects;
    /* 0x1B8 */ uint64_t connection_inactivity_disconnects;
    /* 0x1C0 */ uint64_t unacknowledged_message_disconnects;
    /* 0x1C8 */ uint64_t disconnect_icmp_destination_unreachable_by_code[16];
    /* 0x248 */ uint64_t disconnect_icmp_source_quench;
    /* 0x250 */ uint64_t disconnect_icmp_time_exceeded_by_code[2];
    /* 0x260 */ uint64_t disconnect_icmp_parameter_problem_by_code[2];
    /* 0x270 */ uint64_t disconnect_icmp_invalid_code;
    /* 0x278 */ uint64_t _unrecovered_before_icmp_arrivals[2];
    /* 0x288 */ uint64_t icmp_destination_unreachable_by_code[16];
    /* 0x308 */ uint64_t icmp_source_quench;
    /* 0x310 */ uint64_t icmp_time_exceeded_by_code[2];
    /* 0x320 */ uint64_t icmp_parameter_problem_by_code[2];
    /* 0x330 */ uint64_t icmp_invalid_code;
    /* 0x338 */ uint64_t backend_send_attempts;
    /* 0x340 */ uint64_t backend_send_failures;
    /* 0x348 */ uint64_t repeated_backend_send_failures;
    /* 0x350 */ uint64_t previous_backend_send_failed;
    /* 0x358 */ uint64_t _unrecovered_before_transmit_scheduler[160];
    /* 0x858 */ uint64_t transmit_scheduler_passes;
    /* 0x860 */ uint64_t transmit_scheduler_no_work;
    /* 0x868 */ uint64_t transmit_backend_not_ready;
} rdp_global_statistics_t;

// The original transport reads this process global pointer without a null
// check. The embedding application must publish valid zeroed storage before
// any connection can update statistics.
extern rdp_global_statistics_t *g_rdp_stat;

// Public 12 byte snapshot filled by connection_get_disconnect_info.
typedef struct rdp_connection_disconnect_info_t
{
    /* 0x00 */ uint32_t reason;
    /* 0x04 */ uint8_t icmp_type;
    /* 0x05 */ uint8_t icmp_code;
    /* 0x06 */ uint8_t _padding[2];
    /* 0x08 */ uint32_t icmp_source_ipv4;
} rdp_connection_disconnect_info_t;

#ifndef RDPLIB_CONNECTION_PERF_STATS_T_DEFINED
#define RDPLIB_CONNECTION_PERF_STATS_T_DEFINED
typedef struct rdp_connection_perf_stats_t
{
    uint32_t last_packet_receive_time_ms;
    uint64_t received_packet_sequence_history;
    uint16_t last_received_packet_sequence;
    uint32_t rtt_mean_ms;
    uint32_t rtt_deviation_ms;
    uint32_t last_ping_sample_ms;
    uint32_t queued_reliable_bytes;
    uint32_t transmit_stall_time_ms;
} rdp_connection_perf_stats_t;
#endif

RDP_ASSERT_OFFSET(rdp_connection_disconnect_info_t, icmp_type, 0x04);
RDP_ASSERT_OFFSET(rdp_connection_disconnect_info_t, icmp_source_ipv4, 0x08);
RDP_STATIC_ASSERT(sizeof(rdp_connection_disconnect_info_t) == 0x0C, "Connection disconnect info must be 0x0C bytes");
RDP_ASSERT_OFFSET(rdp_global_statistics_t, aborts_before_syn_acknowledgement, 0x188);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, unreliable_packets_sent, 0x000);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, reliable_packets_sent, 0x010);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, reliable_packets_retransmitted, 0x020);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, ack_only_packets_sent, 0x030);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, piggybacked_ack_packets_sent, 0x038);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, unreliable_packets_received, 0x040);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, reliable_packets_received, 0x050);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, duplicate_reliable_packets_received, 0x060);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, received_header_bytes, 0x070);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, ack_only_packets_received, 0x078);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, piggybacked_ack_packets_received, 0x080);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, acknowledgement_message_ids_processed, 0x088);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, packets_without_new_acknowledgements, 0x090);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, nominal_ack_header_bytes_without_new_acknowledgements, 0x098);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, future_or_unsent_acknowledgements, 0x0A0);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, advancing_packet_sequences_received, 0x0A8);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, reordered_packet_sequences_received, 0x0B8);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, invalid_datagram_flags, 0x0D8);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, invalid_packet_sequences, 0x0E0);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, duplicate_packet_sequences, 0x0E8);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, invalid_message_ids, 0x0F0);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, invalid_fragment_headers, 0x0F8);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, invalid_stream_ids, 0x100);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, short_datagrams, 0x108);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, invalid_fragment_groups, 0x110);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, conflicting_ack_base_flags, 0x118);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, empty_ack_mask_tails, 0x120);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, ancient_acknowledgements, 0x128);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, ack_masks_without_base, 0x130);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, outgoing_connection_attempts, 0x150);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, syn_acknowledgements, 0x170);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, rtt_samples_accepted, 0x190);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, sent_messages_acknowledged, 0x198);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, io_wait_nonpositive_returns, 0x1A0);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, protocol_error_disconnects, 0x1A8);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, peer_reset_disconnects, 0x1B0);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, disconnect_icmp_destination_unreachable_by_code, 0x1C8);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, disconnect_icmp_source_quench, 0x248);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, disconnect_icmp_invalid_code, 0x270);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, icmp_destination_unreachable_by_code, 0x288);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, icmp_source_quench, 0x308);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, icmp_time_exceeded_by_code, 0x310);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, icmp_parameter_problem_by_code, 0x320);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, icmp_invalid_code, 0x330);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, backend_send_attempts, 0x338);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, backend_send_failures, 0x340);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, repeated_backend_send_failures, 0x348);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, previous_backend_send_failed, 0x350);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, transmit_scheduler_passes, 0x858);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, transmit_scheduler_no_work, 0x860);
RDP_ASSERT_OFFSET(rdp_global_statistics_t, transmit_backend_not_ready, 0x868);
RDP_STATIC_ASSERT(sizeof(rdp_global_statistics_t) == 0x870, "Recovered global statistics extent must be 0x870 bytes");

#endif /* RDP_STATS_H */
