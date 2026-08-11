// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rx.h"

#include <stdlib.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "rdplib_platform.h"

rdp_rx_arrival_disposition_t rx_validate_msgid_arrival(connection_t *connection, const _rdp_header_t *header)
{
    int outside_forward_window;
    int mismatched_repeated_syn;

    if (!connection->receive.recording.message_id_receive_initialized)
    {
        return RDP_RX_ACCEPT;
    }

    outside_forward_window = (int16_t)(header->message_id - (uint16_t)(connection->receive.ack.received_through_message_id + 120u)) >= 0;
    mismatched_repeated_syn = (header->flags & RDP_FLAG_SYN) != 0 && header->message_id != connection->receive.recording.initial_message_id;
    if (outside_forward_window || mismatched_repeated_syn)
    {
        ++g_rdp_stat->invalid_message_ids;
        ++connection->receive.recording.statistics.invalid_message_id_count;
        return RDP_RX_DISCARD;
    }

    return RDP_RX_ACCEPT;
}

rdp_rx_arrival_disposition_t rx_validate_seqnum_arrival(connection_t *connection, uint16_t sequence)
{
    rdp_packet_sequence_receive_state_t *state = &connection->receive.recording.packet_sequence;
    int32_t distance;

    distance = (int16_t)(sequence - state->last_received_packet_sequence);
    if (distance == 0)
    {
        ++g_rdp_stat->duplicate_packet_sequences;
        ++connection->receive.recording.statistics.duplicate_packet_sequence_count;
        return RDP_RX_DISCARD;
    }

    if (distance >= -64 && distance < 0)
    {
        uint32_t history_bit = (uint32_t)(-distance - 1);
        if ((state->received_packet_sequence_history & (UINT64_C(1) << history_bit)) != 0)
        {
            ++g_rdp_stat->duplicate_packet_sequences;
            ++connection->receive.recording.statistics.duplicate_packet_sequence_count;
            return RDP_RX_DISCARD;
        }
    }

    if (distance < -64 || distance > 4096)
    {
        ++g_rdp_stat->invalid_packet_sequences;
        ++connection->receive.recording.statistics.invalid_packet_sequence_count;
        return RDP_RX_DISCARD;
    }

    return RDP_RX_ACCEPT;
}

rdp_rx_arrival_disposition_t rx_validate_stream_arrival(connection_t *connection, const _rdp_header_t *header)
{
    if (header->stream_id >= RDP_STREAM_COUNT)
    {
        ++g_rdp_stat->invalid_stream_ids;
        ++connection->receive.recording.statistics.invalid_stream_id_count;
        return RDP_RX_ABORT;
    }

    return RDP_RX_ACCEPT;
}

rdp_rx_arrival_disposition_t rx_validate_fragment_arrival(connection_t *connection, const _rdp_header_t *header)
{
    rdp_receive_statistics_t *statistics = &connection->receive.recording.statistics;
    rdp_list_link_t *link;
    msg_arrival_t *message;
    int final_fragment = (uint16_t)(header->fragment_index + 1u) == header->fragment_count;

    if (header->fragment_index >= header->fragment_count || header->fragment_count < 2 || (!final_fragment && header->payload_bytes != RDP_FRAGMENT_PAYLOAD_BYTES))
    {
        ++g_rdp_stat->invalid_fragment_headers;
        ++statistics->invalid_fragment_count;
        return RDP_RX_ABORT;
    }

    link = list_find_link_by_key(&connection->receive.ownership.fragment_messages, &header->fragment_id);
    message = link ? (msg_arrival_t *)link->value : NULL;
    if (message)
    {
        rdp_rx_arrival_disposition_t disposition = (rdp_rx_arrival_disposition_t)msg_arrival_validate_fragment_arrival(message, header);

        if (disposition != RDP_RX_ACCEPT)
        {
            ++g_rdp_stat->invalid_fragment_groups;
            ++statistics->invalid_fragment_group_count;
            return disposition;
        }
    }

    return RDP_RX_ACCEPT;
}

void rx_record_seqnum_arrival(connection_t *connection, const _rdp_header_t *header)
{
    rdp_receive_recording_state_t *state = &connection->receive.recording;
    int32_t distance;
    uint32_t datagram_bytes;
    uint32_t stream_index;

    distance = (int16_t)(header->sequence - state->packet_sequence.last_received_packet_sequence);
    datagram_bytes = (uint32_t)header->header_bytes + header->payload_bytes;

    if (distance < 0)
    {
        uint32_t history_bit = (uint32_t)(-distance - 1);
        state->packet_sequence.received_packet_sequence_history |= UINT64_C(1) << history_bit;
        ++g_rdp_stat->reordered_packet_sequences_received;
        g_rdp_stat->reordered_packet_sequence_bytes_received += datagram_bytes;
        ++state->statistics.reordered_packet_sequences_received;
        state->statistics.reordered_packet_sequence_bytes_received += datagram_bytes;
    }
    else
    {
        ++g_rdp_stat->advancing_packet_sequences_received;
        g_rdp_stat->advancing_packet_sequence_bytes_received += datagram_bytes;

        if (distance > 0)
        {
            ++state->statistics.advancing_packet_sequences_received;
            state->statistics.advancing_packet_sequence_bytes_received += datagram_bytes;

            if (distance > 64)
            {
                state->packet_sequence.received_packet_sequence_history = 0;
            }
            else
            {
                // The clients form the new history bit before applying the
                // remaining distance. Keeping the 2 shifts separate is
                // significant at distance 64: no operation shifts by the width
                // of the uint64_t value.
                state->packet_sequence.received_packet_sequence_history = ((state->packet_sequence.received_packet_sequence_history << 1) | UINT64_C(1)) << (distance - 1);
            }

            state->packet_sequence.last_received_packet_sequence = header->sequence;
        }
    }

    if ((int16_t)(header->sequence - state->stream_sequence_reset_reference) > 16000)
    {
        state->stream_sequence_reset_reference = (uint16_t)(header->sequence - 1u);
        for (stream_index = 0; stream_index < RDP_STREAM_COUNT; ++stream_index)
        {
            state->next_unreliable_sequence[stream_index] = state->stream_sequence_reset_reference;
        }
    }
}

int rx_record_msgid_arrival(connection_t *connection, uint16_t message_id)
{
    rdp_receive_recording_state_t *state = &connection->receive.recording;
    rdp_receive_ack_state_t *ack_state = &connection->receive.ack;
    uint16_t history_bit;
    uint16_t contiguous_bits;
    int duplicate;

    state->last_reliable_receive_time_ms = time_get_ms();

    if (!state->message_id_receive_initialized)
    {
        state->message_id_receive_initialized = 1;
        state->initial_message_id = message_id;
        ack_state->received_through_message_id = (uint16_t)(message_id - 1u);
        state->highest_received_message_id = ack_state->received_through_message_id;
    }

    if (!ack_state->unreported_message_count)
    {
        ack_state->unreported_min_message_id = message_id;
        ack_state->unreported_max_message_id = message_id;
    }
    else if (message_id < ack_state->unreported_min_message_id)
    {
        // The clients use normal unsigned comparisons for this short lived report range.
        ack_state->unreported_min_message_id = message_id;
    }
    else if (message_id > ack_state->unreported_max_message_id)
    {
        ack_state->unreported_max_message_id = message_id;
    }
    ++ack_state->unreported_message_count;

    if ((int16_t)(message_id - ack_state->received_through_message_id) <= 0)
    {
        return 1;
    }

    if ((int16_t)(message_id - state->highest_received_message_id) > 0)
    {
        state->highest_received_message_id = message_id;
    }

    history_bit = (uint16_t)(message_id - ack_state->received_through_message_id - 1u);
    duplicate = bitarray_setbit(&ack_state->received_message_ids, history_bit) != 0;

    contiguous_bits = 0;
    while (getbit(ack_state->received_message_ids.bytes, contiguous_bits))
    {
        ++contiguous_bits;
    }

    if (contiguous_bits)
    {
        bitarray_copy(&ack_state->received_message_ids, ack_state->received_message_ids.bytes, contiguous_bits, RDP_BITARRAY_BYTES);
        ack_state->received_through_message_id = (uint16_t)(ack_state->received_through_message_id + contiguous_bits);
    }

    return duplicate;
}

int rx_in_sequence(connection_t *connection, msg_arrival_t *message)
{
    uint16_t *next_sequence = &connection->receive.recording.next_unreliable_sequence[message->stream_id];

    if ((int16_t)(message->sequence - *next_sequence) < 0)
    {
        return 0;
    }

    *next_sequence = (uint16_t)(message->sequence + 1u);
    return 1;
}

uint32_t rx_append_ack(connection_t *connection, uint16_t *output_words, uint16_t *flags)
{
    rdp_receive_ack_state_t *state = &connection->receive.ack;
    uint16_t mask_range;
    uint32_t mask_bytes;
    uint32_t first_mask_bit;

    if (!output_words || !flags || !state->unreported_message_count)
    {
        return 0;
    }

    // This is intentionally not the usual signed modular subtraction. All
    // 3 clients sign extend the pending minimum, zero extend the cumulative
    // base, and only then subtract. High half minimum IDs therefore tend to use
    // ACKTHRU even when their forward modular distance is greater than 8.
    if ((int16_t)state->unreported_min_message_id - (int32_t)state->received_through_message_id <= 8)
    {
        *output_words = htons(state->received_through_message_id);
        *flags |= RDP_FLAG_ACKTHRU;
        state->unreported_min_message_id = state->received_through_message_id;

        if ((int16_t)(state->received_through_message_id - state->unreported_max_message_id) > 0)
        {
            state->unreported_max_message_id = state->received_through_message_id;
        }
    }
    else
    {
        *output_words = htons(state->unreported_min_message_id);
        *flags |= RDP_FLAG_MASKOFFSET;
    }

    mask_range = (uint16_t)(state->unreported_max_message_id - state->unreported_min_message_id);
    mask_bytes = ((uint32_t)mask_range + 7u) >> 3;
    if (mask_bytes > 15u)
    {
        mask_bytes = 15u;
    }

    if (mask_bytes)
    {
        *flags |= (uint16_t)(mask_bytes << 4);
        first_mask_bit = (uint16_t)(state->unreported_min_message_id - state->received_through_message_id);
        bitarray_copy(&state->received_message_ids, (uint8_t *)(output_words + 1), first_mask_bit, mask_bytes);
    }

    state->unreported_message_count = 0;
    return 2u + mask_bytes;
}

void rx_init(connection_t *connection)
{
    rdp_receive_initialization_state_t *state = &connection->receive;
    uint32_t stream_id;

    memset(&state->recording.statistics, 0, sizeof(state->recording.statistics));
    state->ack.received_through_message_id = 0;
    state->recording.highest_received_message_id = 0;
    bitarray_clear(&state->ack.received_message_ids);
    state->ack.unreported_message_count = 0;
    state->last_packet_receive_time_ms = time_get_ms();
    state->recording.last_reliable_receive_time_ms = time_get_ms();

    list_init(&state->ownership.fragment_messages);
    state->recording.stream_sequence_reset_reference = 0;
    memset(state->recording.next_unreliable_sequence, 0, sizeof(state->recording.next_unreliable_sequence));
    memset(state->ownership.next_ordered_stream_sequence, 0, sizeof(state->ownership.next_ordered_stream_sequence));
    for (stream_id = 0; stream_id < RDP_STREAM_COUNT; ++stream_id)
    {
        list_init(&state->ownership.receive_streams[stream_id]);
    }

    state->recording.message_id_receive_initialized = 0;
    state->ownership.fin_arrival_pending = 0;
    state->ownership.saved_fin_arrival = NULL;
    state->recording.packet_sequence.last_received_packet_sequence = UINT16_C(0xFFFF);
    state->recording.packet_sequence.received_packet_sequence_history = UINT64_MAX;

    state->completed_trace_samples = NULL;
    state->completed_trace_sample_count = 0;
    state->ownership.trace_samples = NULL;
    state->trace_in_flight = 0;
    state->trace_last_send_time_ms = time_get_ms();
    state->trace_ttl = 1;
    state->trace_ttl_limit = 30;
    state->trace_sweep_count = 0;
    state->trace_sample_index = 0;
    state->icmp_count = 0;
}

void rx_record_packet_arrival(connection_t *connection)
{
    connection->receive.last_packet_receive_time_ms = time_get_ms();
}

int rx_create(connection_t *connection)
{
    rdp_receive_ownership_state_t *state = &connection->receive.ownership;
    uint32_t stream_id;

    list_create(&state->fragment_messages, 1, uint16_cmp);
    for (stream_id = 0; stream_id < RDP_STREAM_COUNT; ++stream_id)
    {
        list_create(&state->receive_streams[stream_id], 1, uint8_cmp);
    }

    return 0;
}

void rx_save_fin_arrival(connection_t *connection, msg_arrival_t *message)
{
    rdp_receive_ownership_state_t *state = &connection->receive.ownership;

    state->saved_fin_arrival = message;
    state->fin_arrival_pending = 1;
    state->fin_message_id = message->message_id;
}

msg_arrival_t *rx_load_fin_arrival(connection_t *connection)
{
    rdp_receive_ownership_state_t *state = &connection->receive.ownership;
    msg_arrival_t *message = state->saved_fin_arrival;

    state->saved_fin_arrival = NULL;
    return message;
}

void rx_sort_into_sequence(connection_t *connection, msg_arrival_t *message)
{
    msg_arrival_prepare_for_sequencer(message);
    list_insert(&connection->receive.ownership.receive_streams[message->stream_id], &message->link);
}

msg_arrival_t *rx_get_next_in_sequence(connection_t *connection, uint8_t stream_id)
{
    rdp_receive_ownership_state_t *state = &connection->receive.ownership;
    rdp_list_t *stream = &state->receive_streams[stream_id];
    msg_arrival_t *message = stream->head ? (msg_arrival_t *)stream->head->value : NULL;

    if (!message || state->next_ordered_stream_sequence[stream_id] != message->stream_sequence)
    {
        return NULL;
    }

    ++state->next_ordered_stream_sequence[stream_id];
    return (msg_arrival_t *)list_remove_head(stream);
}

void rx_flush_input_buffers(connection_t *connection)
{
    rdp_receive_ownership_state_t *state = &connection->receive.ownership;
    uint32_t stream_id;
    msg_arrival_t *message;

    if (state->saved_fin_arrival)
    {
        fast_free(state->saved_fin_arrival);
        state->saved_fin_arrival = NULL;
    }

    while ((message = (msg_arrival_t *)list_remove_head(&state->fragment_messages)) != NULL)
    {
        fast_free(message);
    }

    for (stream_id = 0; stream_id < RDP_STREAM_COUNT; ++stream_id)
    {
        while ((message = (msg_arrival_t *)list_remove_head(&state->receive_streams[stream_id])) != NULL)
        {
            fast_free(message);
        }
    }
}

void rx_destroy(connection_t *connection)
{
    rdp_receive_ownership_state_t *state = &connection->receive.ownership;
    uint32_t stream_id;

    rx_flush_input_buffers(connection);
    list_destroy(&state->fragment_messages);
    for (stream_id = 0; stream_id < RDP_STREAM_COUNT; ++stream_id)
    {
        list_destroy(&state->receive_streams[stream_id]);
    }

    if (state->trace_samples)
    {
        free(state->trace_samples);
        state->trace_samples = NULL;
    }
}

msg_arrival_t *rx_assemble(connection_t *connection, const _rdp_header_t *header, const void *payload)
{
    rdp_receive_ownership_state_t *state = &connection->receive.ownership;
    msg_arrival_t *message = NULL;
    uint32_t allocation_payload_bytes = header->payload_bytes;
    int new_message;

    if ((header->flags & RDP_FLAG_FRAGMENT) != 0)
    {
        rdp_list_link_t *link = list_find_link_by_key(&state->fragment_messages, &header->fragment_id);
        message = link ? (msg_arrival_t *)link->value : NULL;
        allocation_payload_bytes = (uint32_t)header->fragment_count * RDP_FRAGMENT_PAYLOAD_BYTES;
    }

    if (!message)
    {
        message = (msg_arrival_t *)fast_malloc((uint32_t)sizeof(*message) + allocation_payload_bytes);

        // Allocation failure is not checked before initialization in any of
        // the clients. The resulting null write is an original source hazard.
        msg_arrival_init(message, header->fragment_id);
        new_message = 1;
    }
    else
    {
        new_message = 0;
    }

    if (!message)
    {
        return NULL;
    }

    if (msg_arrival_assemble(message, connection, header, payload))
    {
        if (!new_message)
        {
            message = (msg_arrival_t *)list_remove_by_link(&state->fragment_messages, &message->link);
        }

        return message;
    }

    if (new_message)
    {
        list_insert(&state->fragment_messages, &message->link);
    }

    return NULL;
}
