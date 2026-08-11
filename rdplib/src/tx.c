// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "tx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connection.h"
#include "fast.h"
#include "packet.h"
#include "rdplib_platform.h"
#include "rdplib_random.h"
#include "rdp.h"
#include "serial.h"
#include "trace.h"
#include "usend.h"

enum
{
    RDP_APPLICATION_FRAGMENT_BYTES = 512,
    RDP_APPLICATION_MAX_FRAGMENTS = 100
};

void tx_init(connection_t *connection, rdp_t *owner, const uint8_t remote_address[16])
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    uint16_t address_family;
    uint16_t initial_message_id;

#ifdef RDPLIB_SOURCE_FAITHFUL
    srand(time_get_ms());
    initial_message_id = (uint16_t)rand();
#else
    initial_message_id = (uint16_t)rdplib_random_next();
#endif

    memcpy(&address_family, remote_address, sizeof(address_family));
    memcpy(state->remote_address, remote_address, sizeof(state->remote_address));
    state->address_family = address_family;
    memset(state->trace_destination, 0, sizeof(state->trace_destination));

    if (address_family == RDP_TRANSMIT_ADDRESS_IPV4)
    {
        state->send_socket = owner->ipv4_socket;
        memcpy(state->trace_destination, remote_address, sizeof(state->trace_destination));

        // The traceroute destination uses the peer port with its high host order
        // bit set. Since sockaddr ports are network order, this is byte 2.
        state->trace_destination[2] |= UINT8_C(0x80);
    }
    else if (address_family == RDP_TRANSMIT_ADDRESS_IPX)
    {
        state->send_socket = owner->ipx_socket;
    }
    else if (address_family == RDP_TRANSMIT_ADDRESS_SERIAL)
    {
        state->send_socket = -1;
    }

    state->trace_socket = owner->icmp_probe_socket;
    state->reliable_next_message_id = initial_message_id;
    state->acknowledged_through_message_id = (uint16_t)(initial_message_id - 1u);
    state->next_packet_sequence = 0;
    state->next_fragment_id = 0;
    state->last_reliable_enqueue_time_ms = time_get_ms();
    bitarray_clear(&state->outstanding_message_ids);

    list_init(&state->sent_messages.messages);
    list_init(&state->ready_messages.messages);
    list_init(&state->window_blocked_messages.messages);

    bandwidth_init(&state->bandwidth);
    state->send_buffer_limit = 8000;
    memset(state->next_outgoing_stream_sequence, 0, sizeof(state->next_outgoing_stream_sequence));
    state->tx_initial_time_ms = time_get_ms();
    state->tx_reserved_zero = 0;
    timeout_init(&state->rtt_estimator, 500, 1);

    state->syn_sent = 0;
    state->syn_acknowledged = 0;
    state->initial_outgoing_message_id = initial_message_id;
    state->fin_sent = 0;
    state->fin_ack_seen = 0;
    state->fin_message_id = 0;
    state->connected = 1;
    state->transmit_stopped = 0;
    state->disconnect_reason = 0;
    state->disconnect_message_queued = 0;
    state->unacknowledged_message_timeout_ms = 10000;
    state->connection_inactivity_timeout_ms = 10000;
    state->delayed_ack_pending = 0;
    state->delayed_ack_deadline_ms = 0;
    state->last_ping_sample_ms = 0;
    state->close_event = NULL;
    state->close_result = NULL;
    state->trace_socket_default_ttl = owner->probe_socket_default_ttl;
}

int tx_create(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;

    state->sent_messages.queued_bytes = 0;
    list_create(&state->sent_messages.messages, 0, NULL);
    state->ready_messages.queued_bytes = 0;
    list_create(&state->ready_messages.messages, 0, NULL);
    state->window_blocked_messages.queued_bytes = 0;
    list_create(&state->window_blocked_messages.messages, 0, NULL);
    return 0;
}

void tx_flush_output_buffers(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    msg_outgoing_t *message;

    while ((message = (msg_outgoing_t *)list_remove_head(&state->window_blocked_messages.messages)) != NULL)
    {
        state->window_blocked_messages.queued_bytes -= message->serialized_bytes;
        fast_free(message);
    }
    while ((message = (msg_outgoing_t *)list_remove_head(&state->ready_messages.messages)) != NULL)
    {
        state->ready_messages.queued_bytes -= message->serialized_bytes;
        fast_free(message);
    }
    while ((message = (msg_outgoing_t *)list_remove_head(&state->sent_messages.messages)) != NULL)
    {
        state->sent_messages.queued_bytes -= message->serialized_bytes;
        fast_free(message);
    }
}

void tx_destroy(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;

    tx_flush_output_buffers(connection);

    if (state->close_result)
    {
        *state->close_result = 0;
        state->close_result = NULL;
    }

    if (state->close_event)
    {
        rdplib_platform_event_signal(state->close_event);
        state->close_event = NULL;
    }
}

void tx_received_stopped(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;

    if (state->close_result)
    {
        *state->close_result = 1;
        state->close_result = NULL;
    }

    if (state->close_event)
    {
        rdplib_platform_event_signal(state->close_event);
        state->close_event = NULL;
    }

    tx_flush_output_buffers(connection);
    state->transmit_stopped = 1;
}

void tx_abort_connection(connection_t *connection, uint32_t reason)
{
    tx_flush_output_buffers(connection);
    rx_flush_input_buffers(connection);

    if (!connection->transmit.syn_acknowledged)
    {
        ++g_rdp_stat->aborts_before_syn_acknowledgement;
    }

    connection->transmit.connected = 0;
    connection->transmit.transmit_stopped = 1;
    connection->transmit.disconnect_reason = reason;

    if (reason == RDP_DISCONNECT_REASON_PROTOCOL_ERROR)
    {
        ++g_rdp_stat->protocol_error_disconnects;
    }
    else
    {
        // A recent ICMP changes the public diagnosis. Accounting below still
        // follows the original reason passed to this function.
        if (connection->receive.icmp_count && (int32_t)(time_get_ms() - connection->receive.last_icmp_time_ms) < 10000)
        {
            connection->transmit.disconnect_reason = RDP_DISCONNECT_REASON_ICMP;
        }

        if (reason == RDP_DISCONNECT_REASON_PEER_RESET)
        {
            ++g_rdp_stat->peer_reset_disconnects;
        }
        else if (reason == RDP_DISCONNECT_REASON_ICMP)
        {
            uint8_t type = connection->receive.last_icmp_type;
            uint8_t code = connection->receive.last_icmp_code;

            if (type == 3)
            {
                if (code < 16)
                {
                    ++g_rdp_stat->disconnect_icmp_destination_unreachable_by_code[code];
                }
                else
                {
                    ++g_rdp_stat->disconnect_icmp_invalid_code;
                }
            }
            else if (type == 4)
            {
                ++g_rdp_stat->disconnect_icmp_source_quench;
            }
            else if (type == 11)
            {
                if (code < 2)
                {
                    ++g_rdp_stat->disconnect_icmp_time_exceeded_by_code[code];
                }
                else
                {
                    ++g_rdp_stat->disconnect_icmp_invalid_code;
                }
            }
            else if (type == 12)
            {
                if (code < 2)
                {
                    ++g_rdp_stat->disconnect_icmp_parameter_problem_by_code[code];
                }
                else
                {
                    ++g_rdp_stat->disconnect_icmp_invalid_code;
                }
            }
        }
        else if (reason == RDP_DISCONNECT_REASON_UNACKNOWLEDGED_MESSAGE)
        {
            ++g_rdp_stat->unacknowledged_message_disconnects;
        }
        else if (reason == RDP_DISCONNECT_REASON_CONNECTION_INACTIVITY)
        {
            ++g_rdp_stat->connection_inactivity_disconnects;
        }
    }

    if (connection->transmit.close_result)
    {
        *connection->transmit.close_result = 0;
        connection->transmit.close_result = NULL;
    }

    if (connection->transmit.close_event)
    {
        rdplib_platform_event_signal(connection->transmit.close_event);
        connection->transmit.close_event = NULL;
    }

    connection->transmit.disconnect_message_queued = 0;
}

void tx_send_virgin(connection_t *connection, msg_outgoing_t *message)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    rdp_receive_statistics_t *statistics = &connection->receive.recording.statistics;
    uint32_t now_ms = time_get_ms();

    message->first_sent_time_ms = now_ms;
    message->last_sent_time_ms = now_ms;
    ++message->transmission_count;

    tx_send_packet(connection, msg_outgoing_data(message), message->serialized_bytes, message->flags);

    if ((message->flags & RDP_FLAG_MSGID) != 0)
    {
        ++g_rdp_stat->reliable_packets_sent;
        g_rdp_stat->reliable_bytes_sent += message->serialized_bytes;
        ++statistics->reliable_packets_sent;
        statistics->reliable_bytes_sent += message->serialized_bytes;
        state->sent_messages.queued_bytes += message->serialized_bytes;
        list_add_tail(&state->sent_messages.messages, &message->link);
    }
    else
    {
        ++g_rdp_stat->unreliable_packets_sent;
        g_rdp_stat->unreliable_bytes_sent += message->serialized_bytes;
        ++statistics->unreliable_packets_sent;
        statistics->unreliable_bytes_sent += message->serialized_bytes;
        fast_free(message);
    }
}

void tx_enqueue_outgoing(connection_t *connection, msg_outgoing_t *message)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    int can_send_immediately;

    if ((message->flags & RDP_FLAG_MSGID) != 0)
    {
        if (!state->syn_sent)
        {
            message->flags |= RDP_FLAG_SYN;
            state->syn_sent = 1;
            tx_send_virgin(connection, message);
            return;
        }

        if ((int16_t)(message->message_id - state->acknowledged_through_message_id - 120u) >= 0)
        {
            state->window_blocked_messages.queued_bytes += message->serialized_bytes;
            list_add_tail(&state->window_blocked_messages.messages, &message->link);
            return;
        }
    }

    if (!state->syn_acknowledged)
    {
        state->ready_messages.queued_bytes += message->serialized_bytes;
        list_add_tail(&state->ready_messages.messages, &message->link);
        return;
    }

    can_send_immediately = bandwidth_get_queue_size(&state->bandwidth) < (state->bandwidth.bytes_per_second >> 3);
    if (state->address_family == RDP_TRANSMIT_ADDRESS_SERIAL)
    {
        can_send_immediately = can_send_immediately && rdp_serial_tx_ready(connection->owner);
    }

    if (!can_send_immediately)
    {
        state->ready_messages.queued_bytes += message->serialized_bytes;
        list_add_tail(&state->ready_messages.messages, &message->link);
        return;
    }

    if (state->ready_messages.messages.head && state->ready_messages.messages.head->value)
    {
        state->ready_messages.queued_bytes += message->serialized_bytes;
        list_add_tail(&state->ready_messages.messages, &message->link);
        message = (msg_outgoing_t *)list_remove_head(&state->ready_messages.messages);
        state->ready_messages.queued_bytes -= message->serialized_bytes;
    }

    tx_send_virgin(connection, message);
}

int tx_send_fin(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    msg_outgoing_t *message;
    uint16_t message_id;

    if (state->fin_sent || !state->connected || state->transmit_stopped)
    {
        return 0;
    }

#ifndef RDPLIB_SOURCE_FAITHFUL
    if ((uint16_t)(state->reliable_next_message_id - state->acknowledged_through_message_id - 1u) >= RDP_BITARRAY_BITS)
    {
        return RDP_CONNECTION_SEND_HISTORY_FULL;
    }
#endif

    message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(msg_outgoing_t) + 2u);
    if (!message)
    {
        return 2;
    }

    message_id = state->reliable_next_message_id;
    // The source trusts the public send/history gate. Calling this helper with
    // more than 4096 outstanding IDs writes beyond the bit array.
    bitarray_setbit(&state->outstanding_message_ids, (uint16_t)(message_id - state->acknowledged_through_message_id - 1u));
    state->last_reliable_enqueue_time_ms = time_get_ms();
    state->fin_message_id = message_id;
    state->reliable_next_message_id = (uint16_t)(message_id + 1u);

    message->flags = RDP_FLAG_FIN | RDP_FLAG_MSGID;
    message->message_id = state->fin_message_id;
    msg_outgoing_init(message);
    tx_enqueue_outgoing(connection, message);
    state->fin_sent = 1;
    return 0;
}

int tx_send_alive(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    msg_outgoing_t *message;
    uint16_t message_id;

    if (state->fin_sent || !state->connected || state->transmit_stopped)
    {
        return 0;
    }

#ifndef RDPLIB_SOURCE_FAITHFUL
    // Keep the final history position available for FIN.
    if ((uint16_t)(state->reliable_next_message_id - state->acknowledged_through_message_id - 1u) >= RDP_BITARRAY_BITS - 1u)
    {
        return RDP_CONNECTION_SEND_HISTORY_FULL;
    }
#endif

    message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(msg_outgoing_t) + 2u);
    if (!message)
    {
        return 2;
    }

    message->flags = RDP_FLAG_SYSTEM | RDP_FLAG_MSGID;
    message_id = state->reliable_next_message_id;
    // As in tx_send_fin, the owning caller must have preserved history capacity.
    bitarray_setbit(&state->outstanding_message_ids, (uint16_t)(message_id - state->acknowledged_through_message_id - 1u));
    state->last_reliable_enqueue_time_ms = time_get_ms();
    state->reliable_next_message_id = (uint16_t)(message_id + 1u);
    message->message_id = message_id;
    msg_outgoing_init(message);
    tx_enqueue_outgoing(connection, message);
    return 0;
}

int tx_send_packet(connection_t *connection, const uint8_t *data, uint32_t bytes, uint16_t flags)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    rdp_receive_statistics_t *statistics = &connection->receive.recording.statistics;
    uint16_t header_words[(RDP_WIRE_HEADER_MAX_BYTES + 1u) / 2u];
    uint8_t *header = (uint8_t *)header_words;
    uint32_t header_bytes;
    int result;
#ifndef RDPLIB_SOURCE_FAITHFUL
    uint16_t saved_unreported_message_count = connection->receive.ack.unreported_message_count;
    uint16_t saved_unreported_min_message_id = connection->receive.ack.unreported_min_message_id;
    uint16_t saved_unreported_max_message_id = connection->receive.ack.unreported_max_message_id;
    uint32_t saved_delayed_ack_pending = state->delayed_ack_pending;
    uint32_t saved_delayed_ack_deadline_ms = state->delayed_ack_deadline_ms;
#endif

    // RESET eclipses STOP in all 3 clients.
    if (!state->connected)
    {
        flags |= RDP_FLAG_RESET;
    }
    else if (connection->linger_active)
    {
        flags |= RDP_FLAG_STOP;
    }

    header_bytes = RDP_WIRE_HEADER_BASE_BYTES + rx_append_ack(connection, header_words + 2, &flags);
    state->delayed_ack_pending = 0;
    header_words[0] = htons(flags);
    header_words[1] = htons(state->next_packet_sequence);

    if (header_bytes > RDP_WIRE_HEADER_BASE_BYTES)
    {
        if (data)
        {
            ++g_rdp_stat->piggybacked_ack_packets_sent;
            ++statistics->piggybacked_ack_packets_sent;
        }
        else
        {
            ++g_rdp_stat->ack_only_packets_sent;
            ++statistics->ack_only_packets_sent;
        }
    }

    if (state->address_family == RDP_TRANSMIT_ADDRESS_SERIAL)
    {
        rdp_serial_buffer_t buffers[2] = {{header, header_bytes}, {data, bytes}};
#ifdef _WIN32
        result = serial_send_windows(&connection->owner->serial, buffers, data ? 2u : 1u, state->remote_address);
#else
        result = serial_send_mac(&connection->owner->serial, buffers, data ? 2u : 1u, state->remote_address);
#endif
    }
    else
    {
        rdp_buffer_t buffers[2] = {{header, header_bytes}, {data, bytes}};
#ifdef RDPLIB_SOURCE_FAITHFUL
        result = usend(state->send_socket, buffers, data ? 2u : 1u, state->remote_address, connection->owner->use_encryption, connection->owner->use_crc);
#else
        result = rdplib_usend(connection, state->send_socket, buffers, data ? 2u : 1u, state->remote_address, connection->owner->use_encryption, connection->owner->use_crc);
#endif
#ifdef RDPLIB_SOURCE_FAITHFUL
        // This source clock sample is unused after usend and absent from the
        // serial branch in all 3 clients.
        (void)time_get_ms();
#endif
    }

    ++g_rdp_stat->backend_send_attempts;
    if (result)
    {
        if (g_rdp_stat->previous_backend_send_failed)
        {
            ++g_rdp_stat->repeated_backend_send_failures;
        }
        g_rdp_stat->previous_backend_send_failed = 1;
        ++g_rdp_stat->backend_send_failures;
    }
    else
    {
        g_rdp_stat->previous_backend_send_failed = 0;
    }

    if (result == 0)
    {
        bandwidth_enqueue_bytes(&state->bandwidth, header_bytes + bytes + 28u);
        ++state->next_packet_sequence;
    }
    else if (result == 1)
    {
        tx_abort_connection(connection, RDP_DISCONNECT_REASON_SEND_ERROR);
    }
    else if (result == 5)
    {
        bandwidth_enqueue_bytes(&state->bandwidth, state->bandwidth.bytes_per_second / 10u);
#ifndef RDPLIB_SOURCE_FAITHFUL
        connection->receive.ack.unreported_message_count = saved_unreported_message_count;
        connection->receive.ack.unreported_min_message_id = saved_unreported_min_message_id;
        connection->receive.ack.unreported_max_message_id = saved_unreported_max_message_id;
        state->delayed_ack_pending = saved_delayed_ack_pending;
        state->delayed_ack_deadline_ms = saved_delayed_ack_deadline_ms;
#endif
    }

    return result;
}

void tx_tx(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    rdp_receive_statistics_t *statistics = &connection->receive.recording.statistics;
    msg_outgoing_t *message = state->sent_messages.messages.head ? (msg_outgoing_t *)state->sent_messages.messages.head->value : NULL;
    uint32_t now_ms = time_get_ms();
    uint32_t backend_available_time_ms = bandwidth_get_time_empty(&state->bandwidth);

    if (state->address_family == RDP_TRANSMIT_ADDRESS_SERIAL)
    {
        backend_available_time_ms = rdp_serial_get_time_empty(connection->owner);
    }

    ++g_rdp_stat->transmit_scheduler_passes;
    if ((int32_t)(now_ms + 10u - backend_available_time_ms) < 0)
    {
        ++g_rdp_stat->transmit_backend_not_ready;
        return;
    }

    if (state->syn_acknowledged && tx_send_ready_virgins(connection))
    {
        msg_outgoing_t *ready_message = (msg_outgoing_t *)list_remove_head(&state->ready_messages.messages);

        state->ready_messages.queued_bytes -= ready_message->serialized_bytes;
        tx_send_virgin(connection, ready_message);
        return;
    }

    if (message)
    {
        uint32_t retransmission_delay_ms = state->rtt_estimator.mean_ms + 2u * state->rtt_estimator.deviation_ms;

        if (retransmission_delay_ms <= 50u)
        {
            retransmission_delay_ms = 50u;
        }
        else if (retransmission_delay_ms >= 65535u)
        {
            retransmission_delay_ms = 65535u;
        }

        if ((int32_t)(message->last_sent_time_ms + retransmission_delay_ms - 10u - now_ms) <= 0)
        {
            uint32_t message_age_ms = now_ms - message->first_sent_time_ms;
            uint32_t receive_idle_ms = now_ms - connection->receive.last_packet_receive_time_ms;
            uint32_t unacknowledged_timeout_ms = state->unacknowledged_message_timeout_ms;
            uint32_t inactivity_timeout_ms = state->connection_inactivity_timeout_ms;

            if ((message_age_ms >= unacknowledged_timeout_ms / 2u || (message_age_ms >= inactivity_timeout_ms / 2u && receive_idle_ms >= inactivity_timeout_ms)) &&
                (connection->options & RDP_CONNECTION_FEATURE_TRACEROUTE))
            {
                (void)trace_start(connection);
            }

            if (message_age_ms >= unacknowledged_timeout_ms || (message_age_ms >= inactivity_timeout_ms && receive_idle_ms >= inactivity_timeout_ms))
            {
                if (state->connected)
                {
                    tx_abort_connection(connection, message_age_ms >= unacknowledged_timeout_ms ? RDP_DISCONNECT_REASON_UNACKNOWLEDGED_MESSAGE : RDP_DISCONNECT_REASON_CONNECTION_INACTIVITY);
                }
                return;
            }

            message = (msg_outgoing_t *)list_remove_head(&state->sent_messages.messages);
            state->sent_messages.queued_bytes -= message->serialized_bytes;
            message->last_sent_time_ms = now_ms;
            ++message->transmission_count;

            if ((int16_t)(message->message_id - state->acknowledged_through_message_id - 120u) < 0)
            {
                tx_send_packet(connection, msg_outgoing_data(message), message->serialized_bytes, message->flags);
                ++g_rdp_stat->reliable_packets_retransmitted;
                g_rdp_stat->reliable_bytes_retransmitted += message->serialized_bytes;
                ++statistics->reliable_packets_retransmitted;
                statistics->reliable_bytes_retransmitted += message->serialized_bytes;
            }

            // The wire backend may have aborted and flushed the connection
            // while this record was temporarily outside every queue. The
            // clients still publish counters above and reattach it here.
            state->sent_messages.queued_bytes += message->serialized_bytes;
            list_add_tail(&state->sent_messages.messages, &message->link);
            return;
        }
    }

    if (state->delayed_ack_pending)
    {
        tx_send_packet(connection, NULL, 0, 0);
        return;
    }

    ++g_rdp_stat->transmit_scheduler_no_work;
}

void tx_handle_ack(connection_t *connection, uint16_t message_id)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    rdp_receive_statistics_t *statistics = &connection->receive.recording.statistics;
    msg_outgoing_t *message = txq_remove_msgid(&state->sent_messages, message_id);

    ++g_rdp_stat->acknowledgement_message_ids_processed;
    ++statistics->ack_message_ids_processed;

    if (message)
    {
        uint32_t elapsed_ms = time_get_ms() - message->first_sent_time_ms;

        ++g_rdp_stat->sent_messages_acknowledged;
        ++statistics->sent_messages_acked;

        if (message->transmission_count == 1)
        {
            ++g_rdp_stat->rtt_samples_accepted;
            ++statistics->rtt_samples_accepted;
            timeout_add_sample(&state->rtt_estimator, elapsed_ms, message->transmission_count);
            state->last_ping_sample_ms = (uint16_t)elapsed_ms;
        }

        fast_free(message);
    }

    // The source uses the first newly accepted reliable ACK as the SYN
    // transition. It does not require the acknowledged ID to be the SYN ID.
    if (state->syn_sent && !state->syn_acknowledged)
    {
        state->syn_acknowledged = 1;
        ++g_rdp_stat->syn_acknowledgements;
        state->unacknowledged_message_timeout_ms = 120000;
        state->connection_inactivity_timeout_ms = 30000;
    }

    if (state->fin_sent && !state->fin_ack_seen && message_id == state->fin_message_id)
    {
        state->fin_ack_seen = 1;
    }

    // tx_record_ack_arrival calls this before publishing an advancing ACKTHRU
    // base. Preserve that order: FIN recognition and waiter completion can be
    // separated across calls.
    if (state->fin_sent && state->acknowledged_through_message_id == state->fin_message_id)
    {
        if (state->close_result)
        {
            *state->close_result = 1;
            state->close_result = NULL;
        }

        if (state->close_event)
        {
            rdplib_platform_event_signal(state->close_event);
            state->close_event = NULL;
        }
    }
}

#ifndef RDPLIB_SOURCE_FAITHFUL
static int rdplib_tx_sent_messages_contains(const connection_t *connection, uint16_t message_id)
{
    const rdp_list_link_t *link = connection->transmit.sent_messages.messages.head;

    while (link)
    {
        const msg_outgoing_t *message = (const msg_outgoing_t *)link->value;

        if (message->message_id == message_id)
        {
            return 1;
        }
        link = link->next;
    }
    return 0;
}

static int rdplib_tx_acknowledged_id_is_retired_or_sent(const connection_t *connection, uint16_t message_id)
{
    const rdp_transmit_initialization_state_t *state = &connection->transmit;
    int32_t offset = (int16_t)(message_id - state->acknowledged_through_message_id);

    if (offset <= 0)
    {
        return 1;
    }
    if ((uint32_t)offset > RDP_BITARRAY_BITS)
    {
        return 0;
    }

    // A clear history bit was already retired.  A newly claimed bit must name a record in the sent queue.
    if (!getbit(state->outstanding_message_ids.bytes, (uint32_t)offset - 1u))
    {
        return 1;
    }
    return rdplib_tx_sent_messages_contains(connection, message_id);
}

static int rdplib_tx_ack_claims_are_retired_or_sent(const connection_t *connection, const _rdp_header_t *header)
{
    const rdp_transmit_initialization_state_t *state = &connection->transmit;
    const uint8_t *mask;
    uint16_t base_message_id;
    uint32_t mask_bits;
    uint32_t index;

    if ((header->flags & (RDP_FLAG_ACKTHRU | RDP_FLAG_MASKOFFSET)) == 0)
    {
        return 1;
    }

    base_message_id = ntohs(*(const uint16_t *)header->ack_data);
    mask = header->ack_data + 2;
    mask_bits = ((header->flags & RDP_FLAG_ACK_MASK_LENGTH) >> 4) * 8u;

    if ((header->flags & RDP_FLAG_ACKTHRU) != 0)
    {
        int32_t advance = (int16_t)(base_message_id - state->acknowledged_through_message_id);

        for (index = 0; advance > 0 && index < (uint32_t)advance; ++index)
        {
            if (!rdplib_tx_acknowledged_id_is_retired_or_sent(connection, (uint16_t)(state->acknowledged_through_message_id + index + 1u)))
            {
                return 0;
            }
        }
    }
    else if (!rdplib_tx_acknowledged_id_is_retired_or_sent(connection, base_message_id))
    {
        return 0;
    }

    for (index = 0; index < mask_bits; ++index)
    {
        if (getbit(mask, index) && !rdplib_tx_acknowledged_id_is_retired_or_sent(connection, (uint16_t)(base_message_id + index + 1u)))
        {
            return 0;
        }
    }
    return 1;
}
#endif

rdp_rx_arrival_disposition_t tx_validate_ack_arrival(connection_t *connection, const _rdp_header_t *header, uint32_t *field_bytes)
{
    rdp_receive_statistics_t *statistics = &connection->receive.recording.statistics;
    uint16_t flags = header->flags;
    uint32_t mask_bytes = (flags & RDP_FLAG_ACK_MASK_LENGTH) >> 4;
    uint16_t base_message_id;
    uint16_t highest_message_id;
    uint32_t highest_set_bit;
    uint32_t bit;
    int32_t distance_from_next_id;

    *field_bytes = 0;
    if ((flags & RDP_FLAG_ACKTHRU) != 0 && (flags & RDP_FLAG_MASKOFFSET) != 0)
    {
        ++g_rdp_stat->conflicting_ack_base_flags;
        ++statistics->conflicting_ack_flags_count;
        return RDP_RX_ABORT;
    }

    if ((flags & (RDP_FLAG_ACKTHRU | RDP_FLAG_MASKOFFSET)) == 0)
    {
        if (mask_bytes)
        {
            ++g_rdp_stat->ack_masks_without_base;
            ++statistics->ack_mask_without_base_count;
            return RDP_RX_ABORT;
        }
        return RDP_RX_ACCEPT;
    }

    base_message_id = ntohs(*(const uint16_t *)header->ack_data);
    highest_message_id = base_message_id;
    *field_bytes = 2u + mask_bytes;
    if (mask_bytes)
    {
        const uint8_t *mask = header->ack_data + 2;

        // The source scans only the last encoded byte. A 0 final byte is
        // structurally invalid even when earlier bytes contain acknowledgements.
        highest_set_bit = mask_bytes * 8u;
        for (bit = (mask_bytes - 1u) * 8u; bit < mask_bytes * 8u; ++bit)
        {
            if (getbit(mask, bit))
            {
                highest_set_bit = bit;
            }
        }

        if (highest_set_bit == mask_bytes * 8u)
        {
            ++g_rdp_stat->empty_ack_mask_tails;
            ++statistics->empty_ack_mask_tail_count;
            return RDP_RX_ABORT;
        }

        highest_message_id = (uint16_t)(base_message_id + highest_set_bit + 1u);
    }

    distance_from_next_id = (int16_t)(highest_message_id - connection->transmit.reliable_next_message_id);
    if (distance_from_next_id >= 0)
    {
        ++g_rdp_stat->future_or_unsent_acknowledgements;
        ++statistics->future_or_unsent_ack_count;
        return RDP_RX_DISCARD;
    }
    if (distance_from_next_id < -4096)
    {
        ++g_rdp_stat->ancient_acknowledgements;
        ++statistics->ancient_ack_count;
        return RDP_RX_DISCARD;
    }
#ifndef RDPLIB_SOURCE_FAITHFUL
    if (!rdplib_tx_ack_claims_are_retired_or_sent(connection, header))
    {
        ++g_rdp_stat->future_or_unsent_acknowledgements;
        ++statistics->future_or_unsent_ack_count;
        return RDP_RX_DISCARD;
    }
#endif

    return RDP_RX_ACCEPT;
}

void tx_record_ack_arrival(connection_t *connection, const _rdp_header_t *header)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    rdp_receive_statistics_t *statistics = &connection->receive.recording.statistics;
    const uint8_t *mask = header->ack_data;
    uint16_t flags = header->flags;
    uint16_t base_message_id = 0;
    uint32_t newly_acknowledged_ids = 0;
    uint32_t mask_bytes = (flags & RDP_FLAG_ACK_MASK_LENGTH) >> 4;
    uint32_t mask_bits = mask_bytes * 8u;
    uint32_t skipped_mask_bits = 0;
    int32_t base_offset = 0;
    int32_t applicable_mask_bits;
    int32_t index;
    uint32_t nominal_header_bytes;
#ifdef RDPLIB_SOURCE_FAITHFUL
    char ack_text[273];
    char *ack_text_cursor = ack_text;
#endif

    if ((flags & RDP_FLAG_ACKTHRU) != 0)
    {
        uint16_t old_acknowledged_through = state->acknowledged_through_message_id;
        int32_t advance;

        base_message_id = ntohs(*(const uint16_t *)mask);
        mask += 2;
#ifdef RDPLIB_SOURCE_FAITHFUL
        ack_text_cursor += sprintf(ack_text_cursor, "ackthru [%u", base_message_id);
#endif
        advance = (int16_t)(base_message_id - old_acknowledged_through);
        if (advance > 0)
        {
            for (index = 0; index < advance; ++index)
            {
                if (getbit(state->outstanding_message_ids.bytes, (uint32_t)index))
                {
                    tx_handle_ack(connection, (uint16_t)(old_acknowledged_through + index + 1));
                    ++newly_acknowledged_ids;
                }
            }

            bitarray_copy(&state->outstanding_message_ids, state->outstanding_message_ids.bytes, (uint32_t)advance, RDP_BITARRAY_BYTES);
            state->acknowledged_through_message_id = base_message_id;

            for (;;)
            {
                msg_outgoing_t *message = state->window_blocked_messages.messages.head ? (msg_outgoing_t *)state->window_blocked_messages.messages.head->value : NULL;

                if (!message || (int16_t)(message->message_id - state->acknowledged_through_message_id - 120u) >= 0)
                {
                    break;
                }

                message = (msg_outgoing_t *)list_remove_head(&state->window_blocked_messages.messages);
                state->window_blocked_messages.queued_bytes -= message->serialized_bytes;
                tx_enqueue_outgoing(connection, message);
            }
        }
        base_offset = (int16_t)(base_message_id - state->acknowledged_through_message_id);
    }
    else if ((flags & RDP_FLAG_MASKOFFSET) != 0)
    {
        base_message_id = ntohs(*(const uint16_t *)mask);
        mask += 2;
#ifdef RDPLIB_SOURCE_FAITHFUL
        ack_text_cursor += sprintf(ack_text_cursor, "maskoffset [%u", base_message_id);
#endif
        base_offset = (int16_t)(base_message_id - state->acknowledged_through_message_id);
        if (base_offset > 0 && bitarray_clearbit(&state->outstanding_message_ids, (uint32_t)(base_offset - 1)))
        {
            tx_handle_ack(connection, base_message_id);
            ++newly_acknowledged_ids;
        }
    }

    if ((flags & (RDP_FLAG_ACKTHRU | RDP_FLAG_MASKOFFSET)) != 0)
    {
        if (header->payload_bytes)
        {
            ++g_rdp_stat->piggybacked_ack_packets_received;
            ++statistics->piggybacked_ack_packets_received;
        }
        else
        {
            ++g_rdp_stat->ack_only_packets_received;
            ++statistics->ack_only_packets_received;
        }

#ifdef RDPLIB_SOURCE_FAITHFUL
        if (mask_bits)
        {
            *ack_text_cursor++ = ':';
            for (index = 0; index < (int32_t)mask_bits; ++index)
            {
                *ack_text_cursor++ = (char)(getbit(mask, (uint32_t)index) + '0');
            }
        }
        *ack_text_cursor++ = ']';
        *ack_text_cursor++ = '\n';
        *ack_text_cursor = '\0';
#endif
    }

    applicable_mask_bits = (int32_t)mask_bits;
    if (base_offset < 0)
    {
        skipped_mask_bits = (uint32_t)-base_offset;
        applicable_mask_bits += base_offset;
        base_offset = 0;
    }

    for (index = 0; index < applicable_mask_bits; ++index)
    {
        uint32_t outstanding_index;
        uint16_t message_id;

        if (!getbit(mask, skipped_mask_bits + (uint32_t)index))
        {
            continue;
        }

        outstanding_index = (uint32_t)(base_offset + index);
        if (!bitarray_clearbit(&state->outstanding_message_ids, outstanding_index))
        {
            continue;
        }

        message_id = (uint16_t)(state->acknowledged_through_message_id + outstanding_index + 1u);
        tx_handle_ack(connection, message_id);
        ++newly_acknowledged_ids;
    }

    if (!newly_acknowledged_ids)
    {
        nominal_header_bytes = 2u + mask_bytes;
        ++g_rdp_stat->packets_without_new_acknowledgements;
        g_rdp_stat->nominal_ack_header_bytes_without_new_acknowledgements += nominal_header_bytes;
        ++statistics->redundant_ack_packets_received;
        statistics->redundant_ack_header_bytes_received += nominal_header_bytes;
    }
}

void tx_set_delayed_ack(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;

    if (state->delayed_ack_pending)
    {
        return;
    }

    state->delayed_ack_pending = 1;
    state->delayed_ack_deadline_ms = time_get_ms() + 50u;
}

void tx_get_event_time(connection_t *connection, rdp_timeout_data_t *timeout)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    msg_outgoing_t *ready_head = state->ready_messages.messages.head ? (msg_outgoing_t *)state->ready_messages.messages.head->value : NULL;
    msg_outgoing_t *sent_head = state->sent_messages.messages.head ? (msg_outgoing_t *)state->sent_messages.messages.head->value : NULL;
    uint32_t backend_available_time_ms = bandwidth_get_time_empty(&state->bandwidth);
    uint32_t transport_deadline_ms;

    timeout->infinite = 1;
    timeout->deadline_ms = 0;
    if (!ready_head && !sent_head && !state->delayed_ack_pending)
    {
        return;
    }

#ifndef RDPLIB_SOURCE_FAITHFUL
    // An unreliable message cannot establish SYN.  Leave it queued until a reliable send or keepalive gives this direction an initial reliable ID.
    if (ready_head && !state->syn_acknowledged && !sent_head && !state->delayed_ack_pending)
    {
        return;
    }
#endif

    if (state->address_family == RDP_TRANSMIT_ADDRESS_SERIAL)
    {
        backend_available_time_ms = rdp_serial_get_time_empty(connection->owner);
    }

    timeout->infinite = 0;
    timeout->deadline_ms = backend_available_time_ms;
    if (ready_head && state->syn_acknowledged)
    {
        return;
    }

    transport_deadline_ms = state->delayed_ack_deadline_ms;
    if (sent_head)
    {
        uint32_t retransmission_delay_ms = (uint32_t)state->rtt_estimator.mean_ms + 2u * state->rtt_estimator.deviation_ms;
        uint32_t retransmission_deadline_ms;

        if (retransmission_delay_ms <= 50u)
        {
            retransmission_delay_ms = 50u;
        }
        else if (retransmission_delay_ms >= 65535u)
        {
            retransmission_delay_ms = 65535u;
        }
        retransmission_deadline_ms = sent_head->last_sent_time_ms + retransmission_delay_ms;
        if (!state->delayed_ack_pending || (int32_t)(transport_deadline_ms - retransmission_deadline_ms) > 0)
        {
            transport_deadline_ms = retransmission_deadline_ms;
        }
    }

    if ((int32_t)(transport_deadline_ms - timeout->deadline_ms) > 0)
    {
        timeout->deadline_ms = transport_deadline_ms;
    }
}

int tx_send_ready_virgins(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    msg_outgoing_t *sent_head = state->sent_messages.messages.head ? (msg_outgoing_t *)state->sent_messages.messages.head->value : NULL;
    uint32_t now_ms = time_get_ms();
    uint32_t oldest_sent_age_ms;
    uint32_t admission_delay_ms;

    if (!state->ready_messages.messages.head)
    {
        return 0;
    }

    oldest_sent_age_ms = sent_head ? now_ms - sent_head->last_sent_time_ms : 0;
    if (state->address_family == RDP_TRANSMIT_ADDRESS_SERIAL)
    {
        admission_delay_ms = 10000u;
    }
    else
    {
        admission_delay_ms = (uint32_t)state->rtt_estimator.mean_ms + 3u * state->rtt_estimator.deviation_ms;
        if (admission_delay_ms <= 50u)
        {
            admission_delay_ms = 50u;
        }
        else if (admission_delay_ms >= 65535u)
        {
            admission_delay_ms = 65535u;
        }
    }
    return oldest_sent_age_ms <= admission_delay_ms;
}

uint32_t tx_get_stall_time(connection_t *connection)
{
    rdp_transmit_initialization_state_t *state = &connection->transmit;
    uint32_t oldest_sent_age_ms = 0;
    uint32_t retransmission_delay_ms = (uint32_t)state->rtt_estimator.mean_ms + 2u * state->rtt_estimator.deviation_ms;

    if (state->sent_messages.messages.head && state->sent_messages.messages.count)
    {
        oldest_sent_age_ms = time_get_ms() - txq_get_oldest_time_sent(&state->sent_messages);
    }

    if (retransmission_delay_ms <= 50u)
    {
        retransmission_delay_ms = 50u;
    }
    else if (retransmission_delay_ms >= 65535u)
    {
        retransmission_delay_ms = 65535u;
    }

    return oldest_sent_age_ms > retransmission_delay_ms ? oldest_sent_age_ms - retransmission_delay_ms : 0;
}

int trace_start(connection_t *connection)
{
    rdp_receive_initialization_state_t *receive;
    trace_sample_t *active_samples;
    uint32_t now_ms;
    int result = 0;

    if (!connection)
    {
        return 6;
    }
    if (connection->owner->icmp_probe_socket == -1)
    {
        return 8;
    }

    (void)connhash_lock(&connection->owner->connections, connection->transmit.remote_address);
    receive = &connection->receive;
    if (connection->options & RDP_CONNECTION_FEATURE_TRACEROUTE)
    {
        now_ms = rdplib_platform_current_time_ms();
        if (now_ms - receive->trace_started_time_ms < connection->transmit.connection_inactivity_timeout_ms)
        {
            result = 9;
            goto unlock;
        }
    }

    if (connection->transmit.trace_socket == -1)
    {
        result = 8;
        goto unlock;
    }

    active_samples = (trace_sample_t *)receive->ownership.trace_samples;
    if (active_samples)
    {
        if (!receive->completed_trace_samples)
        {
            size_t completed_bytes = (size_t)receive->trace_sample_index * sizeof(*active_samples);

            receive->completed_trace_samples = rdplib_platform_malloc(completed_bytes);
            if (!receive->completed_trace_samples)
            {
                result = 2;
                goto unlock;
            }
            memcpy(receive->completed_trace_samples, active_samples, completed_bytes);
            receive->completed_trace_sample_count = receive->trace_sample_index;
            receive->completed_trace_time = receive->trace_started_wall_time;
        }
        else
        {
            rdplib_platform_free(active_samples);
            receive->ownership.trace_samples = NULL;
        }
    }

    if (!receive->ownership.trace_samples)
    {
        receive->ownership.trace_samples = rdplib_platform_malloc(sizeof(trace_sample_t) * RDP_TRACE_SAMPLE_CAPACITY);
        if (!receive->ownership.trace_samples)
        {
            result = 2;
            goto unlock;
        }
    }

    memset(receive->ownership.trace_samples, 0, sizeof(trace_sample_t) * RDP_TRACE_SAMPLE_CAPACITY);
    receive->trace_in_flight = 0;
    now_ms = rdplib_platform_current_time_ms();
    receive->trace_started_time_ms = now_ms;
    receive->trace_last_send_time_ms = now_ms;
    receive->trace_started_wall_time = rdplib_platform_wall_time_seconds();
    receive->trace_ttl = 1;
    receive->trace_ttl_limit = RDP_TRACE_MAX_TTL;
    receive->trace_sweep_count = 0;
    receive->trace_sample_index = 0;
    connection->options |= RDP_CONNECTION_FEATURE_TRACEROUTE;

unlock:
    rdp_unlock(connection);
    return result;
}

int trace_send(connection_t *connection)
{
    rdp_receive_initialization_state_t *receive = &connection->receive;
    trace_sample_t *samples = (trace_sample_t *)receive->ownership.trace_samples;
    trace_sample_t *sample = &samples[receive->trace_sample_index];
    int result;

    sample->sent_time_ms = rdplib_platform_current_time_ms();
    sample->ttl = (uint8_t)receive->trace_ttl;

    result = rdplib_platform_socket_set_option(connection->transmit.trace_socket, 0, 7, &receive->trace_ttl, sizeof(receive->trace_ttl));
    if (result != 0)
    {
        receive->trace_sweep_count = RDP_TRACE_SWEEP_LIMIT;
        return result;
    }

    {
        uint8_t sample_index = (uint8_t)receive->trace_sample_index;

        if (rdplib_platform_send_datagram(connection->transmit.trace_socket, &sample_index, 1, connection->transmit.trace_destination) == 1)
        {
            ++receive->trace_ttl;
            if (receive->trace_ttl > receive->trace_ttl_limit)
            {
                ++receive->trace_sweep_count;
                receive->trace_ttl = 1;
            }
            ++receive->trace_sample_index;
            receive->trace_in_flight = 1;
            receive->trace_last_send_time_ms = rdplib_platform_current_time_ms();
        }
        else
        {
            memset(sample, 0, sizeof(*sample));
        }
    }

    result = rdplib_platform_socket_set_option(connection->transmit.trace_socket, 0, 7, &connection->transmit.trace_socket_default_ttl, sizeof(connection->transmit.trace_socket_default_ttl));
    if (result != 0)
    {
        receive->trace_sweep_count = RDP_TRACE_SWEEP_LIMIT;
    }
    return result;
}

uint32_t connection_set_max_data_rate(connection_t *connection, uint32_t bytes_per_second)
{
    uint32_t previous = connection->transmit.bandwidth.bytes_per_second;

    connection->transmit.bandwidth.bytes_per_second = bytes_per_second;
    return previous;
}

int connection_send(connection_t *connection, const void *data, uint32_t bytes, uint32_t stream, uint32_t flags)
{
    rdp_buffer_t buffer;

    buffer.data = data;
    buffer.bytes = bytes;
    return connection_sendv(connection, &buffer, 1, stream, flags);
}

int connection_sendv(connection_t *connection, const rdp_buffer_t *buffers, uint32_t buffer_count, uint32_t stream, uint32_t flags)
{
#ifndef RDPLIB_SOURCE_FAITHFUL
    rdp_transmit_initialization_state_t *transmit;
    msg_outgoing_t *message;
    msg_outgoing_t *fragments[RDP_APPLICATION_MAX_FRAGMENTS] = {0};
    uint32_t total_bytes = 0;
    uint32_t buffer_index;
    uint32_t buffer_offset;
    uint32_t queued_bytes;
    uint32_t reliable;
    uint32_t index;
    uint16_t fragment_count;
    int result = RDP_CONNECTION_SEND_OK;

    if (!connection || (!buffers && buffer_count != 0))
    {
        return RDP_CONNECTION_SEND_INVALID_ARGUMENT;
    }

    for (index = 0; index < buffer_count; ++index)
    {
        if ((!buffers[index].data && buffers[index].bytes != 0) || buffers[index].bytes > UINT32_MAX - total_bytes)
        {
            return RDP_CONNECTION_SEND_INVALID_ARGUMENT;
        }
        total_bytes += buffers[index].bytes;
    }

    reliable = flags & RDP_SEND_RELIABLE;
    if (total_bytes > 51200u || (!reliable && total_bytes > RDP_APPLICATION_FRAGMENT_BYTES))
    {
        return RDP_CONNECTION_SEND_PAYLOAD_TOO_LARGE;
    }
    if (stream >= RDP_STREAM_COUNT || (flags & ~(uint32_t)RDP_SEND_RELIABLE) != 0)
    {
        return RDP_CONNECTION_SEND_INVALID_ARGUMENT;
    }
    fragment_count = reliable ? (uint16_t)((total_bytes + RDP_APPLICATION_FRAGMENT_BYTES - 1u) / RDP_APPLICATION_FRAGMENT_BYTES) : 0;

    (void)connhash_lock(&connection->owner->connections, connection->transmit.remote_address);
    transmit = &connection->transmit;

    if (!transmit->connected)
    {
        result = RDP_CONNECTION_SEND_NOT_CONNECTED;
        goto done;
    }
    if (transmit->transmit_stopped)
    {
        result = RDP_CONNECTION_SEND_PEER_STOPPED;
        goto done;
    }
    if (transmit->fin_sent)
    {
        result = RDP_CONNECTION_SEND_FIN_SENT;
        goto done;
    }
    {
        uint32_t outstanding_ids = (uint16_t)(transmit->reliable_next_message_id - transmit->acknowledged_through_message_id - 1u);

        if (outstanding_ids + 1u >= 4096u || (reliable && outstanding_ids + fragment_count >= 4096u))
        {
            result = RDP_CONNECTION_SEND_HISTORY_FULL;
            goto done;
        }
    }

    if (transmit->sent_messages.queued_bytes > UINT32_MAX - transmit->ready_messages.queued_bytes ||
        transmit->sent_messages.queued_bytes + transmit->ready_messages.queued_bytes > UINT32_MAX - transmit->window_blocked_messages.queued_bytes)
    {
        result = RDP_CONNECTION_SEND_INVALID_ARGUMENT;
        goto done;
    }
    queued_bytes = transmit->sent_messages.queued_bytes + transmit->ready_messages.queued_bytes + transmit->window_blocked_messages.queued_bytes;
    if (queued_bytes > transmit->send_buffer_limit)
    {
        result = RDP_CONNECTION_SEND_BUFFER_FULL;
        goto done;
    }

    if (reliable)
    {
        uint32_t remaining_bytes = total_bytes;
        uint16_t fragment_index;

        for (fragment_index = 0; fragment_index < fragment_count; ++fragment_index)
        {
            uint32_t fragment_bytes = remaining_bytes < RDP_APPLICATION_FRAGMENT_BYTES ? remaining_bytes : RDP_APPLICATION_FRAGMENT_BYTES;

            fragments[fragment_index] = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(*message) + fragment_bytes + 10u);
            if (!fragments[fragment_index])
            {
                uint16_t release_index;

                for (release_index = 0; release_index < fragment_index; ++release_index)
                {
                    fast_free(fragments[release_index]);
                }
                result = RDP_CONNECTION_SEND_ALLOCATION_FAILED;
                goto done;
            }
            remaining_bytes -= fragment_bytes;
        }

        buffer_index = 0;
        buffer_offset = 0;
        remaining_bytes = total_bytes;
        for (fragment_index = 0; fragment_index < fragment_count; ++fragment_index)
        {
            uint32_t fragment_bytes = remaining_bytes < RDP_APPLICATION_FRAGMENT_BYTES ? remaining_bytes : RDP_APPLICATION_FRAGMENT_BYTES;
            uint32_t copy_remaining = fragment_bytes;
            uint16_t message_flags = fragment_count > 1 ? RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT : RDP_FLAG_MSGID;
            uint8_t *destination;

            message = fragments[fragment_index];
            if (stream != 0 && fragment_index == 0)
            {
                message_flags |= RDP_FLAG_SEQUENCED;
            }

            message->flags = message_flags;
            bitarray_setbit(&transmit->outstanding_message_ids, (uint16_t)(transmit->reliable_next_message_id - transmit->acknowledged_through_message_id - 1u));
            transmit->last_reliable_enqueue_time_ms = rdplib_platform_current_time_ms();
            message->message_id = transmit->reliable_next_message_id++;
            message->fragment_id = transmit->next_fragment_id;
            message->fragment_index = fragment_index;
            message->fragment_count = fragment_count;
            message->stream_id = (uint8_t)stream;
            message->stream_sequence = transmit->next_outgoing_stream_sequence[stream];
            msg_outgoing_init(message);

            destination = msg_outgoing_data(message) + message->serialized_bytes;
            while (copy_remaining != 0)
            {
                uint32_t available = buffers[buffer_index].bytes - buffer_offset;
                uint32_t copy_bytes = copy_remaining < available ? copy_remaining : available;

                memcpy(destination, (const uint8_t *)buffers[buffer_index].data + buffer_offset, copy_bytes);
                destination += copy_bytes;
                message->serialized_bytes += copy_bytes;
                buffer_offset += copy_bytes;
                copy_remaining -= copy_bytes;
                if (buffer_offset == buffers[buffer_index].bytes)
                {
                    buffer_offset = 0;
                    ++buffer_index;
                }
            }

            if ((message->flags & RDP_FLAG_SEQUENCED) != 0)
            {
                ++transmit->next_outgoing_stream_sequence[stream];
            }
            tx_enqueue_outgoing(connection, message);
            remaining_bytes -= fragment_bytes;
        }

        if (fragment_count > 1)
        {
            ++transmit->next_fragment_id;
        }
    }
    else
    {
        uint8_t *destination;

        message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(*message) + total_bytes + 4u);
        if (!message)
        {
            result = RDP_CONNECTION_SEND_ALLOCATION_FAILED;
            goto done;
        }
        message->flags = stream != 0 ? RDP_FLAG_SEQUENCED : 0;
        message->stream_id = (uint8_t)stream;
        msg_outgoing_init(message);

        destination = msg_outgoing_data(message) + message->serialized_bytes;
        for (index = 0; index < buffer_count; ++index)
        {
            memcpy(destination, buffers[index].data, buffers[index].bytes);
            destination += buffers[index].bytes;
            message->serialized_bytes += buffers[index].bytes;
        }
        tx_enqueue_outgoing(connection, message);
    }

    rdp_resort(connection, 1);

done:
    rdp_unlock(connection);
    return result;
#else
    rdp_transmit_initialization_state_t *transmit;
    msg_outgoing_t *message;
    uint32_t total_bytes = 0;
    uint32_t buffer_index;
    uint32_t buffer_offset;
    uint32_t queued_bytes;
    uint32_t reliable;
    uint32_t index;
    int result = RDP_CONNECTION_SEND_OK;

    if (!connection)
    {
        return RDP_CONNECTION_SEND_INVALID_ARGUMENT;
    }

    for (index = 0; index < buffer_count; ++index)
    {
        total_bytes += buffers[index].bytes;
    }

    reliable = flags & RDP_SEND_RELIABLE;
    if (total_bytes > 51200u || (!reliable && total_bytes > RDP_APPLICATION_FRAGMENT_BYTES))
    {
        return RDP_CONNECTION_SEND_PAYLOAD_TOO_LARGE;
    }
    if ((stream & ~UINT32_C(0xFF)) != 0 || (flags & ~(uint32_t)RDP_SEND_RELIABLE) != 0)
    {
        return RDP_CONNECTION_SEND_INVALID_ARGUMENT;
    }

    // Endpoint uniqueness is an unchecked source invariant.
    (void)connhash_lock(&connection->owner->connections, connection->transmit.remote_address);
    transmit = &connection->transmit;

    if (!transmit->connected)
    {
        result = RDP_CONNECTION_SEND_NOT_CONNECTED;
        goto done;
    }
    if (transmit->transmit_stopped)
    {
        result = RDP_CONNECTION_SEND_PEER_STOPPED;
        goto done;
    }
    if (transmit->fin_sent)
    {
        result = RDP_CONNECTION_SEND_FIN_SENT;
        goto done;
    }
    if ((uint16_t)((uint16_t)(transmit->reliable_next_message_id - 1u) - transmit->acknowledged_through_message_id) + 1u >= 4096u)
    {
        result = RDP_CONNECTION_SEND_HISTORY_FULL;
        goto done;
    }

    queued_bytes = transmit->sent_messages.queued_bytes + transmit->ready_messages.queued_bytes + transmit->window_blocked_messages.queued_bytes;
    if (queued_bytes > transmit->send_buffer_limit)
    {
        result = RDP_CONNECTION_SEND_BUFFER_FULL;
        goto done;
    }

    if (reliable)
    {
        uint16_t fragment_count = (uint16_t)((total_bytes + RDP_APPLICATION_FRAGMENT_BYTES - 1u) / RDP_APPLICATION_FRAGMENT_BYTES);
        uint16_t message_flags = RDP_FLAG_MSGID;
        uint16_t fragment_index = 0;
        uint32_t remaining_bytes = total_bytes;

        if (fragment_count > 1)
        {
            message_flags |= RDP_FLAG_FRAGMENT;
        }

        buffer_index = 0;
        buffer_offset = 0;
        while (fragment_index < fragment_count)
        {
            uint32_t fragment_bytes = remaining_bytes < RDP_APPLICATION_FRAGMENT_BYTES ? remaining_bytes : RDP_APPLICATION_FRAGMENT_BYTES;
            uint32_t copy_remaining = fragment_bytes;
            uint16_t this_flags = (uint16_t)(message_flags & ~RDP_FLAG_SEQUENCED);
            uint8_t *destination;

            message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(*message) + fragment_bytes + 10u);
            if (!message)
            {
                result = RDP_CONNECTION_SEND_ALLOCATION_FAILED;
                goto done;
            }

            if (stream != 0 && fragment_index == 0)
            {
                this_flags |= RDP_FLAG_SEQUENCED;
            }

            message->flags = this_flags;
            bitarray_setbit(&transmit->outstanding_message_ids, (uint16_t)(transmit->reliable_next_message_id - transmit->acknowledged_through_message_id - 1u));
            transmit->last_reliable_enqueue_time_ms = rdplib_platform_current_time_ms();
            message->message_id = transmit->reliable_next_message_id++;
            message->fragment_id = transmit->next_fragment_id;
            message->fragment_index = fragment_index;
            message->fragment_count = fragment_count;
            message->stream_id = (uint8_t)stream;
            message->stream_sequence = transmit->next_outgoing_stream_sequence[stream];
            msg_outgoing_init(message);

            destination = msg_outgoing_data(message) + message->serialized_bytes;
            while (copy_remaining != 0)
            {
                uint32_t available = buffers[buffer_index].bytes - buffer_offset;
                uint32_t copy_bytes = copy_remaining < available ? copy_remaining : available;

                memcpy(destination, (const uint8_t *)buffers[buffer_index].data + buffer_offset, copy_bytes);
                destination += copy_bytes;
                message->serialized_bytes += copy_bytes;
                buffer_offset += copy_bytes;
                copy_remaining -= copy_bytes;
                if (buffer_offset == buffers[buffer_index].bytes)
                {
                    buffer_offset = 0;
                    ++buffer_index;
                }
            }

            if ((message->flags & RDP_FLAG_SEQUENCED) != 0)
            {
                ++transmit->next_outgoing_stream_sequence[stream];
            }
            tx_enqueue_outgoing(connection, message);
            remaining_bytes -= fragment_bytes;
            ++fragment_index;
        }

        if (fragment_count > 1)
        {
            ++transmit->next_fragment_id;
        }
    }
    else
    {
        uint8_t *destination;

        message = (msg_outgoing_t *)fast_malloc((uint32_t)sizeof(*message) + total_bytes + 4u);
        message->flags = stream != 0 ? RDP_FLAG_SEQUENCED : 0;
        message->stream_id = (uint8_t)stream;
        msg_outgoing_init(message);

        destination = msg_outgoing_data(message) + message->serialized_bytes;
        for (index = 0; index < buffer_count; ++index)
        {
            memcpy(destination, buffers[index].data, buffers[index].bytes);
            destination += buffers[index].bytes;
            message->serialized_bytes += buffers[index].bytes;
        }
        tx_enqueue_outgoing(connection, message);
    }

    rdp_resort(connection, 1);

done:
    rdp_unlock(connection);
    return result;
#endif
}
