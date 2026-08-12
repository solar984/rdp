// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "connection.h"

#include "rdp.h"
#include "rdplib_constants.h"
#include "trace.h"
#include "rdplib_wire.h"

#include <string.h>

void connection_init(connection_t *connection, rdp_t *owner, const uint8_t remote_address[16], uint32_t options)
{
    connection->owner = owner;
    tx_init(connection, owner, remote_address);
    rx_init(connection);

    connection->linger_active = 0;
    connection->linger_deadline_ms = 0;
    connection->locally_initiated = 0;
    connection->reserved_receive_gate = 0;
    memset(connection->application_storage, 0, sizeof(connection->application_storage));
    rdplib_platform_mutex_init(&connection->lock);

    connection->connection_hash_link.value = connection;
    connection->connection_hash_link.key = connection->transmit.remote_address;
    connection->event_link.value = connection;
    connection->event_link.key = &connection->event_timeout;
    connection->reference_count = 1;
    connection->event_timeout.infinite = 1;
    connection->event_timeout.deadline_ms = 0;
    connection->event_type = 0;
    connection->options = options;
#ifndef RDPLIB_SOURCE_FAITHFUL
    connection->rdplib_keepalive_interval_ms = RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS;
    connection->rdplib_packet_drop_callback = NULL;
    connection->rdplib_packet_drop_context = NULL;
#endif
}

int connection_create(connection_t *connection)
{
    int result = tx_create(connection);

    if (result == 0)
    {
        result = rx_create(connection);
    }
    return result;
}

void connection_destroy(connection_t *connection)
{
    tx_destroy(connection);
    rx_destroy(connection);
    rdplib_platform_mutex_destroy(&connection->lock);
}

int connection_connected(const connection_t *connection)
{
    return connection->transmit.connected;
}

void *connection_app_ptr(connection_t *connection)
{
    return connection;
}

uint8_t *connection_get_remote_addr(connection_t *connection)
{
    return connection->transmit.remote_address;
}

void connection_get_disconnect_info(const connection_t *connection, void *output, uint32_t output_bytes)
{
    rdp_connection_disconnect_info_t *information;

    if (output_bytes != sizeof(rdp_connection_disconnect_info_t))
    {
        return;
    }

    information = (rdp_connection_disconnect_info_t *)output;
    information->reason = connection->transmit.disconnect_reason;
    information->icmp_code = connection->receive.last_icmp_code;
    information->icmp_type = connection->receive.last_icmp_type;
    information->icmp_source_ipv4 = connection->receive.last_icmp_source;
}

void connection_get_perf_stats(connection_t *connection, rdp_connection_perf_stats_t *statistics)
{
    rdp_transmit_initialization_state_t *transmit;

    // The returned pointer is intentionally ignored. Endpoint uniqueness is
    // an unchecked invariant in the original connection owner.
    (void)connhash_lock(&connection->owner->connections, connection->transmit.remote_address);

    transmit = &connection->transmit;
    statistics->last_packet_receive_time_ms = connection->receive.last_packet_receive_time_ms;
    statistics->received_packet_sequence_history = connection->receive.recording.packet_sequence.received_packet_sequence_history;
    statistics->last_received_packet_sequence = connection->receive.recording.packet_sequence.last_received_packet_sequence;
    statistics->rtt_mean_ms = transmit->rtt_estimator.mean_ms;
    statistics->rtt_deviation_ms = transmit->rtt_estimator.deviation_ms;
    statistics->last_ping_sample_ms = transmit->last_ping_sample_ms;
    statistics->queued_reliable_bytes = transmit->sent_messages.queued_bytes + transmit->ready_messages.queued_bytes + transmit->window_blocked_messages.queued_bytes;
    statistics->transmit_stall_time_ms = tx_get_stall_time(connection);

    rdp_unlock(connection);

    // This order is source faithful and intentionally exposes the original
    // lifetime hazard described above.
    if (connection->transmit.address_family == RDP_TRANSMIT_ADDRESS_SERIAL)
    {
        statistics->transmit_stall_time_ms = rdp_get_serial_stall_time(connection->owner);
    }
}

int connection_linger_expired(const connection_t *connection)
{
    return connection->linger_active && (int32_t)(connection->linger_deadline_ms - time_get_ms()) <= 10;
}

void connection_set_send_buffer_size(connection_t *connection, uint32_t bytes)
{
    connection->transmit.send_buffer_limit = bytes;
}

int connection_close(connection_t *connection, uint32_t timeout_ms, int *result, rdplib_platform_event_t *completion_event)
{
    rdp_application_receive_t *application;
    rdp_transmit_initialization_state_t *transmit;
    int completion_pending = 1;
    int producer_was_nonempty;
    int producer_is_empty;

    if (!connection)
    {
        return RDP_CONNECTION_SEND_INVALID_ARGUMENT;
    }

    // Endpoint uniqueness is an unchecked source invariant.
    (void)connhash_lock(&connection->owner->connections, connection->transmit.remote_address);
    application = &connection->owner->application_receive;
    rdplib_platform_mutex_lock(&application->producer_lock);
    producer_was_nonempty = application->producer_queue.messages.head && application->producer_queue.messages.head->value;
    rxq_flush_all_messages(&application->producer_queue, connection);
    producer_is_empty = !application->producer_queue.messages.head || !application->producer_queue.messages.head->value;
    if (producer_was_nonempty && producer_is_empty)
    {
        (void)rdplib_platform_semaphore_wait(&application->arrival_semaphore, 0);
    }
    rdplib_platform_mutex_unlock(&application->producer_lock);

    rdplib_platform_mutex_lock(&application->consumer_lock);
    rxq_flush_all_messages(&application->consumer_queue, connection);
    rdplib_platform_mutex_unlock(&application->consumer_lock);
    transmit = &connection->transmit;

    if ((transmit->connected && transmit->transmit_stopped) || (transmit->fin_ack_seen && transmit->fin_message_id == transmit->acknowledged_through_message_id))
    {
        if (result)
        {
            *result = 1;
        }
        completion_pending = 0;
    }
    else if (!transmit->connected)
    {
        if (result)
        {
            *result = 0;
        }
        completion_pending = 0;
    }

    if (timeout_ms == 0)
    {
        if (completion_pending)
        {
            if (result)
            {
                *result = 0;
            }
            completion_pending = 0;
        }
        (void)rdp_connection_mark_for_delete(connection->owner, connection);
    }
    else
    {
        rdplib_platform_event_t *installed_event = NULL;
        int *installed_result = NULL;

        if (completion_pending)
        {
            installed_event = completion_event;
            installed_result = result;
        }

        rx_flush_input_buffers(connection);
        connection->linger_active = 1;
        connection->linger_deadline_ms = time_get_ms() + timeout_ms;
        transmit->close_event = installed_event;
        transmit->close_result = installed_result;
        (void)tx_send_fin(connection);
        rdp_resort(connection, 1);
    }

    if (!completion_pending && completion_event)
    {
        rdplib_platform_event_signal(completion_event);
    }

    rdp_unlock(connection);
    return RDP_CONNECTION_SEND_OK;
}

void connection_recalc_event_timeout(connection_t *connection, rdp_timeout_data_t *timeout)
{
    rdp_transmit_initialization_state_t *transmit = &connection->transmit;
    rdp_receive_initialization_state_t *receive = &connection->receive;

    timeout->infinite = 1;
    timeout->deadline_ms = 0;
    connection->event_type = RDP_CONNECTION_EVENT_NONE;

    if (transmit->connected && !transmit->transmit_stopped)
    {
        tx_get_event_time(connection, timeout);
        if (!timeout->infinite)
        {
            connection->event_type = RDP_CONNECTION_EVENT_TRANSMIT;
        }
    }
    else if (transmit->connected && transmit->delayed_ack_pending)
    {
        connection->event_type = RDP_CONNECTION_EVENT_TRANSMIT;
        timeout->infinite = 0;
        timeout->deadline_ms = transmit->delayed_ack_deadline_ms;
    }

    if ((connection->options & RDP_CONNECTION_FEATURE_KEEPALIVE) && transmit->syn_sent && !transmit->fin_sent && transmit->connected && !transmit->transmit_stopped
#ifndef RDPLIB_SOURCE_FAITHFUL
        && (uint16_t)(transmit->reliable_next_message_id - transmit->acknowledged_through_message_id - 1u) < RDP_BITARRAY_BITS - 1u
#endif
    )
    {
#ifdef RDPLIB_SOURCE_FAITHFUL
        uint32_t keepalive_deadline_ms = transmit->last_reliable_enqueue_time_ms + 10000u;
#else
        uint32_t keepalive_deadline_ms = transmit->last_reliable_enqueue_time_ms + connection->rdplib_keepalive_interval_ms;
#endif

        if (timeout->infinite || (int32_t)(keepalive_deadline_ms - timeout->deadline_ms) < 0)
        {
            connection->event_type = RDP_CONNECTION_EVENT_KEEPALIVE;
            timeout->infinite = 0;
            timeout->deadline_ms = keepalive_deadline_ms;
        }
    }

    if ((connection->options & RDP_CONNECTION_FEATURE_TRACEROUTE) && receive->trace_sweep_count < RDP_TRACE_SWEEP_LIMIT)
    {
        uint32_t trace_deadline_ms = receive->trace_last_send_time_ms;

        if (receive->trace_in_flight)
        {
            uint32_t retransmission_delay_ms = (uint32_t)transmit->rtt_estimator.mean_ms + 2u * transmit->rtt_estimator.deviation_ms;

            if (retransmission_delay_ms <= 50u)
            {
                retransmission_delay_ms = 50u;
            }
            else if (retransmission_delay_ms >= 65535u)
            {
                retransmission_delay_ms = 65535u;
            }
            trace_deadline_ms += retransmission_delay_ms;
        }
        if (timeout->infinite || (int32_t)(trace_deadline_ms - timeout->deadline_ms) < 0)
        {
            connection->event_type = RDP_CONNECTION_EVENT_TRACEROUTE;
            timeout->infinite = 0;
            timeout->deadline_ms = trace_deadline_ms;
        }
    }

    if (connection->linger_active)
    {
        if (timeout->infinite || (int32_t)(connection->linger_deadline_ms - timeout->deadline_ms) < 0)
        {
            connection->event_type = RDP_CONNECTION_EVENT_LINGER;
            timeout->infinite = 0;
            timeout->deadline_ms = connection->linger_deadline_ms;
        }
    }
}

void connection_event_process(connection_t *connection, uint32_t now_ms, rdp_timeout_data_t *timeout)
{
    for (;;)
    {
        switch (connection->event_type)
        {
        case RDP_CONNECTION_EVENT_LINGER:
            return;

        case RDP_CONNECTION_EVENT_TRANSMIT:
            tx_tx(connection);
            break;

        case RDP_CONNECTION_EVENT_KEEPALIVE:
            (void)tx_send_alive(connection);
            break;

        case RDP_CONNECTION_EVENT_TRACEROUTE:
            (void)trace_send(connection);
            break;

        default:
            break;
        }

        connection_recalc_event_timeout(connection, timeout);
        if (timeout->infinite || (int32_t)(now_ms - timeout->deadline_ms) < 0)
        {
            return;
        }
    }
}

void connection_handle_icmp(connection_t *connection, uint8_t type, uint8_t code, uint8_t trace_response, uint8_t trace_sample_index, const uint8_t source_address[16])
{
    rdp_receive_initialization_state_t *receive = &connection->receive;
    rdp_receive_statistics_t *statistics = &receive->recording.statistics;
    uint32_t now_ms = time_get_ms();
    uint32_t source_ipv4;

    if (trace_response)
    {
        trace_sample_t *samples;
        trace_sample_t *sample;

        if (trace_sample_index >= receive->trace_sample_index || !receive->ownership.trace_samples)
        {
            return;
        }

        samples = (trace_sample_t *)receive->ownership.trace_samples;
        sample = &samples[trace_sample_index];
        if (type == 3 && code == 3 && receive->trace_ttl_limit > sample->ttl)
        {
            receive->trace_ttl_limit = sample->ttl;
            if (receive->trace_ttl > sample->ttl)
            {
                ++receive->trace_sweep_count;
                receive->trace_ttl = 1;
            }
        }

        sample->round_trip_time_ms = now_ms - sample->sent_time_ms;
        memcpy(&source_ipv4, source_address + 4, sizeof(source_ipv4));
        sample->responder_ipv4 = source_ipv4;
        sample->icmp_type = type;
        sample->icmp_code = code;
        receive->trace_in_flight = 0;
        receive->trace_last_send_time_ms = now_ms;
        return;
    }

    switch (type)
    {
    case 3:
        if (code < 16)
        {
            ++g_rdp_stat->icmp_destination_unreachable_by_code[code];
            ++statistics->icmp_destination_unreachable_by_code[code];
        }
        else
        {
            ++g_rdp_stat->icmp_invalid_code;
            ++statistics->icmp_invalid_code_count;
        }
        break;

    case 4:
        ++g_rdp_stat->icmp_source_quench;
        ++statistics->icmp_source_quench_count;
        break;

    case 11:
        if (code < 2)
        {
            ++g_rdp_stat->icmp_time_exceeded_by_code[code];
            ++statistics->icmp_time_exceeded_by_code[code];
        }
        else
        {
            ++g_rdp_stat->icmp_invalid_code;
            ++statistics->icmp_invalid_code_count;
        }
        break;

    case 12:
        if (code < 2)
        {
            ++g_rdp_stat->icmp_parameter_problem_by_code[code];
            ++statistics->icmp_parameter_problem_by_code[code];
        }
        else
        {
            ++g_rdp_stat->icmp_invalid_code;
            ++statistics->icmp_invalid_code_count;
        }
        break;

    default:
        return;
    }

    ++receive->icmp_count;
    receive->last_icmp_type = type;
    receive->last_icmp_code = code;
    receive->last_icmp_time_ms = time_get_ms();
    memcpy(&source_ipv4, source_address + 4, sizeof(source_ipv4));
    receive->last_icmp_source = source_ipv4;
    if (type == 3 && code == 3)
    {
        tx_abort_connection(connection, RDP_DISCONNECT_REASON_ICMP);
    }
}

void connection_record_arrival(connection_t *connection, const _rdp_header_t *header, uint32_t *duplicate_reliable)
{
    rdp_receive_recording_state_t *recording = &connection->receive.recording;
    rdp_receive_statistics_t *statistics = &recording->statistics;
    *duplicate_reliable = 0;
    rx_record_packet_arrival(connection);

    if ((header->flags & RDP_FLAG_RESET) != 0 && connection->transmit.connected)
    {
        tx_abort_connection(connection, RDP_DISCONNECT_REASON_PEER_RESET);
        return;
    }

    if ((header->flags & RDP_FLAG_STOP) != 0 && !connection->transmit.transmit_stopped)
    {
        tx_received_stopped(connection);
    }

    rx_record_seqnum_arrival(connection, header);

    tx_record_ack_arrival(connection, header);

    if ((header->flags & RDP_FLAG_MSGID) != 0)
    {
        *duplicate_reliable = (uint32_t)rx_record_msgid_arrival(connection, header->message_id);

        if (*duplicate_reliable)
        {
            ++g_rdp_stat->duplicate_reliable_packets_received;
            g_rdp_stat->duplicate_reliable_payload_bytes_received += header->payload_bytes;
            ++statistics->duplicate_reliable_packets_received;
            statistics->duplicate_reliable_payload_bytes_received += header->payload_bytes;
        }
        else
        {
            ++g_rdp_stat->reliable_packets_received;
            g_rdp_stat->reliable_payload_bytes_received += header->payload_bytes;
            ++statistics->reliable_packets_received;
            statistics->reliable_payload_bytes_received += header->payload_bytes;
        }
    }
    else if (header->payload_bytes)
    {
        ++g_rdp_stat->unreliable_packets_received;
        g_rdp_stat->unreliable_payload_bytes_received += header->payload_bytes;
        ++statistics->unreliable_packets_received;
        statistics->unreliable_payload_bytes_received += header->payload_bytes;
    }

    g_rdp_stat->received_header_bytes += header->header_bytes;
    statistics->received_header_bytes += header->header_bytes;

    if ((header->flags & RDP_FLAG_MSGID) != 0)
    {
        tx_set_delayed_ack(connection);
    }
}

#ifndef RDPLIB_SOURCE_FAITHFUL
static rdp_rx_arrival_disposition_t rdplib_connection_abort_short_datagram(connection_t *connection)
{
    ++g_rdp_stat->short_datagrams;
    ++connection->receive.recording.statistics.short_packet_count;
    if (connection->transmit.connected)
    {
        tx_abort_connection(connection, RDP_DISCONNECT_REASON_PROTOCOL_ERROR);
    }
    return RDP_RX_ABORT;
}

static rdp_rx_arrival_disposition_t rdplib_connection_abort_invalid_fragment(connection_t *connection)
{
    ++g_rdp_stat->invalid_fragment_headers;
    ++connection->receive.recording.statistics.invalid_fragment_count;
    if (connection->transmit.connected)
    {
        tx_abort_connection(connection, RDP_DISCONNECT_REASON_PROTOCOL_ERROR);
    }
    return RDP_RX_ABORT;
}
#endif

rdp_rx_arrival_disposition_t connection_parse_and_validate_arrival(connection_t *connection, const uint8_t *packet, uint16_t packet_bytes, _rdp_header_t *header)
{
#ifndef RDPLIB_SOURCE_FAITHFUL
    uint32_t required_bytes;
    uint32_t fragment_offset = 0;
    uint16_t flags;
    uint16_t acknowledgement_base_flags;

    if (!connection || !packet || !header || packet_bytes < sizeof(uint16_t))
    {
        return RDP_RX_ABORT;
    }

    flags = rdplib_load_network_u16(packet);
    if (((flags & (RDP_FLAG_SYN | RDP_FLAG_FIN | RDP_FLAG_FRAGMENT)) == 0 || (flags & RDP_FLAG_MSGID) != 0) && (flags & RDP_FLAG_MULTI) == 0)
    {
        required_bytes = 4;
        acknowledgement_base_flags = flags & (RDP_FLAG_ACKTHRU | RDP_FLAG_MASKOFFSET);
        if (acknowledgement_base_flags == RDP_FLAG_ACKTHRU || acknowledgement_base_flags == RDP_FLAG_MASKOFFSET)
        {
            required_bytes += 2u + ((flags & RDP_FLAG_ACK_MASK_LENGTH) >> 4);
        }
        if ((flags & RDP_FLAG_MSGID) != 0)
        {
            required_bytes += 2;
        }
        if ((flags & RDP_FLAG_FRAGMENT) != 0)
        {
            fragment_offset = required_bytes;
            required_bytes += 6;
        }
        if ((flags & RDP_FLAG_SEQUENCED) != 0)
        {
            ++required_bytes;
            if ((flags & RDP_FLAG_MSGID) != 0)
            {
                ++required_bytes;
            }
        }

        if (required_bytes > packet_bytes)
        {
            return rdplib_connection_abort_short_datagram(connection);
        }
        if (fragment_offset)
        {
            uint32_t payload_bytes = packet_bytes - required_bytes;

            if (rdplib_load_network_u16(packet + fragment_offset + 4u) > 100u || payload_bytes < 1u || payload_bytes > RDP_FRAGMENT_PAYLOAD_BYTES)
            {
                return rdplib_connection_abort_invalid_fragment(connection);
            }
        }
    }
#endif

    rdp_receive_recording_state_t *recording = &connection->receive.recording;
    rdp_receive_statistics_t *statistics = &recording->statistics;
    const uint8_t *cursor = packet;
    rdp_rx_arrival_disposition_t disposition = RDP_RX_ACCEPT;
    uint32_t acknowledgement_bytes;
    uint16_t required_message_id_flags;

    header->flags = rdplib_load_network_u16(cursor);
    cursor += 2;
    required_message_id_flags = header->flags & (RDP_FLAG_SYN | RDP_FLAG_FIN | RDP_FLAG_FRAGMENT);
    if ((required_message_id_flags && (header->flags & RDP_FLAG_MSGID) == 0) || (header->flags & RDP_FLAG_MULTI) != 0)
    {
        ++g_rdp_stat->invalid_datagram_flags;
        ++statistics->invalid_datagram_flags_count;
        disposition = RDP_RX_ABORT;
        goto finished;
    }

    header->sequence = rdplib_load_network_u16(cursor);
    cursor += 2;
    disposition = rx_validate_seqnum_arrival(connection, header->sequence);
    if (disposition != RDP_RX_ACCEPT)
    {
        goto finished;
    }

    header->ack_data = cursor;
    disposition = tx_validate_ack_arrival(connection, header, &acknowledgement_bytes);
    if (disposition != RDP_RX_ACCEPT)
    {
        goto finished;
    }
    cursor += acknowledgement_bytes;

    if ((header->flags & RDP_FLAG_MSGID) != 0)
    {
        header->message_id = rdplib_load_network_u16(cursor);
        cursor += 2;
        disposition = rx_validate_msgid_arrival(connection, header);
        if (disposition != RDP_RX_ACCEPT)
        {
            goto finished;
        }
    }

    header->fragment_id = 0;
    header->fragment_index = 0;
    header->fragment_count = 1;
    if ((header->flags & RDP_FLAG_FRAGMENT) != 0)
    {
        header->fragment_id = rdplib_load_network_u16(cursor);
        header->fragment_index = rdplib_load_network_u16(cursor + 2);
        header->fragment_count = rdplib_load_network_u16(cursor + 4);
        cursor += 6;
    }

    if ((header->flags & RDP_FLAG_SEQUENCED) != 0)
    {
        header->stream_id = *cursor++;
        if ((header->flags & RDP_FLAG_MSGID) != 0)
        {
            header->stream_sequence = *cursor++;
        }

        disposition = rx_validate_stream_arrival(connection, header);
        if (disposition != RDP_RX_ACCEPT)
        {
            goto finished;
        }
    }

    header->header_bytes = (uint16_t)(cursor - packet);
    if (packet_bytes < header->header_bytes)
    {
        ++g_rdp_stat->short_datagrams;
        ++statistics->short_packet_count;
        disposition = RDP_RX_ABORT;
        goto finished;
    }

    header->payload_bytes = (uint16_t)(packet_bytes - header->header_bytes);
    if ((header->flags & RDP_FLAG_FRAGMENT) != 0)
    {
        disposition = rx_validate_fragment_arrival(connection, header);
    }

finished:
    if (disposition == RDP_RX_ABORT && connection->transmit.connected)
    {
        tx_abort_connection(connection, RDP_DISCONNECT_REASON_PROTOCOL_ERROR);
    }
    return disposition;
}
