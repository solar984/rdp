// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "serial.h"

#include <string.h>

#include "fast.h"

// TAKP Windows writes this diagnostic timestamp at serial send, receive, fill,
// and destruction entry. No instruction in the executable reads it. The Mac
// builds do not contain the store.
uint32_t g_rdp_serial_activity_time_ms;

uint32_t serial_calculate_transmit_time_ms(uint32_t byte_count)
{
    // The clients multiply in a 32 bit register before dividing. Keeping the
    // intermediate unsigned preserves that overflow behavior.
    uint32_t microseconds = byte_count * 9000u;
    return microseconds / 1000000u;
}

uint32_t serial_calculate_stall_time_ms(uint32_t current_time_ms, uint32_t write_start_time_ms, uint32_t write_bytes)
{
    uint32_t expected_time_ms = serial_calculate_transmit_time_ms(write_bytes);
    uint32_t elapsed_time_ms = current_time_ms - write_start_time_ms;
    uint32_t stall_threshold_ms = expected_time_ms * 2u;

    if (elapsed_time_ms > stall_threshold_ms)
    {
        return elapsed_time_ms - stall_threshold_ms;
    }

    return 0;
}

uint32_t serial_calculate_time_empty_ms(uint32_t current_time_ms, uint32_t queued_send_bytes, uint32_t active_write_bytes)
{
    uint32_t waiting_bytes = queued_send_bytes;

    if (waiting_bytes)
    {
        // serial_get_time_empty excludes the write already at
        // the head of the asynchronous queue. Its callers must keep the byte
        // total and head record consistent; the client does not check this.
        waiting_bytes -= active_write_bytes;
    }

    return current_time_ms + serial_calculate_transmit_time_ms(waiting_bytes);
}

int serial_pending_send_count_is_ready(uint32_t pending_send_count)
{
    return pending_send_count < 2u;
}

uint16_t serial_calculate_frame_checksum(const void *data, uint16_t byte_count)
{
    const uint16_t *word = (const uint16_t *)data;
    uint32_t sum = 0;

    while (byte_count > 1)
    {
        sum += *word++;
        byte_count = (uint16_t)(byte_count - 2);
    }

    if (byte_count)
    {
        sum += *(const uint8_t *)word;
    }

    sum = (sum & 0xFFFFu) + (sum >> 16);
    sum += sum >> 16;
    return (uint16_t)~sum;
}

void serial_init(serial_t *serial)
{
    // The standalone platform record carries only the creation latch needed
    // by the client's common partial construction unwind.
    rdplib_platform_mutex_prepare(&serial->lock);
    serial->endpoint = -1;
    serial->receive_sync_state = 0;
    list_init(&serial->pending_sends);
    serial->queued_send_bytes = 0;
    serial_rx_init(&serial->receive_buffer);
    memset(&serial->statistics, 0, sizeof(serial->statistics));
}

int serial_create(serial_t *serial, uint16_t local_port)
{
    int result;

    list_create(&serial->pending_sends, 0, NULL);
    rdplib_platform_mutex_init(&serial->lock);
    serial->local_port = local_port;
    result = serial_rx_create(&serial->receive_buffer, RDP_SERIAL_RECEIVE_CAPACITY);
    if (result != 0)
    {
        // The Windows destroy path normally drains asynchronous writes, but
        // this create failure path cannot own an asynchronous write yet. All 3 clients
        // therefore reduce to this same cleanup sequence here.
        rdplib_platform_mutex_lock(&serial->lock);
        serial_rx_destroy(&serial->receive_buffer);
        list_destroy(&serial->pending_sends);
        rdplib_platform_mutex_unlock(&serial->lock);
        rdplib_platform_mutex_destroy(&serial->lock);
    }
    return result;
}

int serial_tx_write(serial_t *serial, serial_tx_buf_t *buffer)
{
    uint32_t bytes_written = 0;
    uint32_t error_code = 0;
    int result = 0;

    if (rdplib_platform_serial_write(serial->endpoint, &buffer->platform_async_state, serial_tx_data(buffer), buffer->write_bytes, &bytes_written, &error_code))
    {
        if (bytes_written == buffer->write_bytes)
        {
            ++serial->statistics.immediate_complete_write_count;
        }
        else
        {
            ++serial->statistics.immediate_short_write_count;
        }
    }
    else if (error_code == RDPLIB_PLATFORM_ERROR_IO_PENDING)
    {
        buffer->link.value = buffer;
        buffer->write_start_time_ms = rdplib_platform_current_time_ms();
        serial->queued_send_bytes += buffer->write_bytes;
        list_add_tail(&serial->pending_sends, &buffer->link);
        return 0;
    }
    else if (error_code == RDPLIB_PLATFORM_ERROR_INVALID_USER_BUFFER || error_code == RDPLIB_PLATFORM_ERROR_NOT_ENOUGH_MEMORY)
    {
        ++serial->statistics.write_resource_error_count;
        result = 5;
    }
    else
    {
        ++serial->statistics.write_error_count;
        serial->statistics.last_write_error = error_code;
        serial->endpoint = -1;
        result = 1;
    }

    rdplib_platform_serial_close_event(rdplib_platform_serial_async_get_event(&buffer->platform_async_state));
    fast_free(buffer);
    return result;
}

void serial_retire_pending_writes_windows_locked(serial_t *serial, int wait)
{
    while (serial->pending_sends.head)
    {
        serial_tx_buf_t *buffer = (serial_tx_buf_t *)serial->pending_sends.head->value;
        uint32_t bytes_written = 0;
        uint32_t error_code = 0;

        if (!buffer)
        {
            break;
        }

        if (rdplib_platform_serial_get_write_result(serial->endpoint, &buffer->platform_async_state, wait, &bytes_written, &error_code))
        {
            if (bytes_written == buffer->write_bytes)
            {
                ++serial->statistics.async_complete_write_count;
            }
            else
            {
                ++serial->statistics.async_short_write_count;
            }
        }
        else
        {
            if (error_code == RDPLIB_PLATFORM_ERROR_IO_INCOMPLETE)
            {
                break;
            }
            ++serial->statistics.write_error_count;
        }

        buffer = (serial_tx_buf_t *)list_remove_head(&serial->pending_sends);
        serial->queued_send_bytes -= buffer->write_bytes;
        rdplib_platform_serial_close_event(rdplib_platform_serial_async_get_event(&buffer->platform_async_state));
        fast_free(buffer);
    }
}

static void serial_destroy_common_locked(serial_t *serial)
{
    serial_rx_destroy(&serial->receive_buffer);
    list_destroy(&serial->pending_sends);
}

void serial_destroy_mac(serial_t *serial)
{
    rdplib_platform_mutex_lock(&serial->lock);
    serial_destroy_common_locked(serial);
    rdplib_platform_mutex_unlock(&serial->lock);
    rdplib_platform_mutex_destroy(&serial->lock);
}

void serial_destroy_windows(serial_t *serial)
{
    rdplib_platform_mutex_lock(&serial->lock);
    g_rdp_serial_activity_time_ms = rdplib_platform_current_time_ms();
    serial_retire_pending_writes_windows_locked(serial, 1);
    serial_destroy_common_locked(serial);
    rdplib_platform_mutex_unlock(&serial->lock);
    rdplib_platform_mutex_destroy(&serial->lock);
}

static int serial_send_locked(serial_t *serial, const rdp_serial_buffer_t *buffers, uint32_t buffer_count, const uint8_t remote_address[16])
{
    static const char magic[18] = "\n\rwho's yo daddy?";
    serial_tx_buf_t *buffer;
    serial_frame_header_t *frame;
    uint8_t *payload;
    uint32_t payload_bytes = 0;
    uint32_t payload_offset = 0;
    uint32_t index;

    for (index = 0; index < buffer_count; ++index)
    {
        payload_bytes += buffers[index].bytes;
    }

    buffer = (serial_tx_buf_t *)fast_malloc((uint32_t)sizeof(*buffer) + (uint32_t)sizeof(*frame) + payload_bytes);
    if (!buffer)
    {
        return 2;
    }

    memset(&buffer->platform_async_state, 0, sizeof(buffer->platform_async_state));
    rdplib_platform_serial_async_set_event(&buffer->platform_async_state, rdplib_platform_serial_create_event());
    if (!rdplib_platform_serial_async_get_event(&buffer->platform_async_state))
    {
        fast_free(buffer);
        return 3;
    }

    frame = (serial_frame_header_t *)serial_tx_data(buffer);
    memcpy(frame->magic, magic, sizeof(magic));
    frame->local_port = serial->local_port;
    memcpy(&frame->remote_port, remote_address + 2, sizeof(frame->remote_port));
    frame->payload_bytes = (uint16_t)payload_bytes;
    frame->checksum = 0;
    payload = serial_frame_payload(frame);
    for (index = 0; index < buffer_count; ++index)
    {
        memcpy(payload + payload_offset, buffers[index].data, buffers[index].bytes);
        payload_offset += buffers[index].bytes;
    }

    frame->checksum = serial_calculate_frame_checksum(&frame->local_port, (uint16_t)(payload_bytes + 8u));
    buffer->write_bytes = payload_bytes + (uint32_t)sizeof(*frame);
    return serial_tx_write(serial, buffer);
}

int serial_send_mac(serial_t *serial, const rdp_serial_buffer_t *buffers, uint32_t buffer_count, const uint8_t remote_address[16])
{
    int result;

    rdplib_platform_mutex_lock(&serial->lock);
    result = serial_send_locked(serial, buffers, buffer_count, remote_address);
    rdplib_platform_mutex_unlock(&serial->lock);
    return result;
}

int serial_send_windows(serial_t *serial, const rdp_serial_buffer_t *buffers, uint32_t buffer_count, const uint8_t remote_address[16])
{
    int result;

    rdplib_platform_mutex_lock(&serial->lock);
    g_rdp_serial_activity_time_ms = rdplib_platform_current_time_ms();
    serial_retire_pending_writes_windows_locked(serial, 0);
    result = serial_send_locked(serial, buffers, buffer_count, remote_address);
    rdplib_platform_mutex_unlock(&serial->lock);
    return result;
}

typedef struct serial_frame_tail_t
{
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t payload_bytes;
    uint16_t checksum;
    uint8_t payload[536];
} serial_frame_tail_t;

static int serial_receive_frame_locked(serial_t *serial, void *destination, uint32_t destination_capacity, uint8_t source_address[16])
{
    static const char magic[18] = "\n\rwho's yo daddy?";
    char candidate_magic[18];
    serial_frame_tail_t frame;

    (void)destination_capacity; // The source ignores the caller's copy limit.

    for (;;)
    {
        if (serial->receive_sync_state != 0 && serial->receive_sync_state != 1)
        {
            // All 3 clients spin forever if this internal state is inconsistent.
            for (;;)
            {
            }
        }

        if (serial->receive_sync_state == 0)
        {
            while (serial->receive_buffer.used_bytes >= sizeof(candidate_magic))
            {
                serial_rx_peek(&serial->receive_buffer, candidate_magic, (uint32_t)sizeof(candidate_magic));
                if (strcmp(candidate_magic, magic) == 0)
                {
                    serial_rx_read(&serial->receive_buffer, NULL, (uint32_t)sizeof(candidate_magic));
                    serial->receive_sync_state = 1;
                    break;
                }

                serial_rx_read(&serial->receive_buffer, NULL, 1);
                ++serial->statistics.sync_discarded_bytes;
            }
            if (serial->receive_sync_state == 0)
            {
                return -1;
            }
        }

        if (serial->receive_buffer.used_bytes < 8)
        {
            return -1;
        }

        serial_rx_peek(&serial->receive_buffer, &frame, 8);
        if (frame.payload_bytes > sizeof(frame.payload) || (frame.destination_port != serial->local_port && frame.destination_port != 0))
        {
            serial->receive_sync_state = 0;
            if (frame.payload_bytes > sizeof(frame.payload))
            {
                ++serial->statistics.oversize_frame_count;
            }
            else
            {
                ++serial->statistics.port_mismatch_count;
            }
            continue;
        }

        if (serial->receive_buffer.used_bytes < (uint32_t)frame.payload_bytes + 8u)
        {
            return -1;
        }

        serial_rx_peek(&serial->receive_buffer, &frame, (uint32_t)frame.payload_bytes + 8u);
        if (serial_calculate_frame_checksum(&frame, (uint16_t)(frame.payload_bytes + 8u)) != 0)
        {
            serial->receive_sync_state = 0;
            ++serial->statistics.checksum_failure_count;
            continue;
        }

        serial->statistics.received_frame_bytes += (uint32_t)frame.payload_bytes + 26u;
        serial_rx_read(&serial->receive_buffer, NULL, (uint32_t)frame.payload_bytes + 8u);
        memcpy(destination, frame.payload, frame.payload_bytes);
        memset(source_address, 0, 16);
        {
            uint16_t address_family = 69;
            memcpy(source_address, &address_family, sizeof(address_family));
        }
        memcpy(source_address + 2, &frame.source_port, sizeof(frame.source_port));
        serial->receive_sync_state = 0;
        return frame.payload_bytes;
    }
}

int serial_recv_from_mac(serial_t *serial, void *destination, uint32_t destination_capacity, uint8_t source_address[16])
{
    int result;

    rdplib_platform_mutex_lock(&serial->lock);
    // Both Macintosh clients compile the serial fill routine as a warning
    // stub. The parser still consumes bytes already present in the ring.
    result = serial_receive_frame_locked(serial, destination, destination_capacity, source_address);
    rdplib_platform_mutex_unlock(&serial->lock);
    return result;
}

int serial_recv_from_windows(serial_t *serial, void *destination, uint32_t destination_capacity, uint8_t source_address[16])
{
    int result;

    rdplib_platform_mutex_lock(&serial->lock);
    g_rdp_serial_activity_time_ms = rdplib_platform_current_time_ms();
    serial_retire_pending_writes_windows_locked(serial, 0);
    serial_rx_fill_windows(&serial->receive_buffer, serial->endpoint);
    result = serial_receive_frame_locked(serial, destination, destination_capacity, source_address);
    rdplib_platform_mutex_unlock(&serial->lock);
    return result;
}

void serial_set_time_next_recv(serial_t *serial, uint32_t next_receive_time_ms)
{
    rdplib_platform_mutex_lock(&serial->lock);
    serial->next_receive_time_ms = next_receive_time_ms;
    rdplib_platform_mutex_unlock(&serial->lock);
}

uint32_t serial_get_time_next_recv(serial_t *serial)
{
    uint32_t next_receive_time_ms;

    rdplib_platform_mutex_lock(&serial->lock);
    next_receive_time_ms = serial->next_receive_time_ms;
    rdplib_platform_mutex_unlock(&serial->lock);
    return next_receive_time_ms;
}

int serial_tx_ready(serial_t *serial)
{
    uint32_t pending_send_count;

    rdplib_platform_mutex_lock(&serial->lock);
    pending_send_count = serial->pending_sends.count;
    rdplib_platform_mutex_unlock(&serial->lock);
    return serial_pending_send_count_is_ready(pending_send_count);
}

uint32_t serial_get_time_empty(serial_t *serial)
{
    uint32_t waiting_bytes;

    rdplib_platform_mutex_lock(&serial->lock);
    waiting_bytes = serial->queued_send_bytes;
    if (waiting_bytes)
    {
        rdp_list_link_t *head = serial->pending_sends.head;
        serial_tx_buf_t *active_write = head ? (serial_tx_buf_t *)head->value : NULL;

        // The source unconditionally dereferences active_write here.
        waiting_bytes -= active_write->write_bytes;
    }
    rdplib_platform_mutex_unlock(&serial->lock);

#ifdef RDPLIB_SOURCE_FAITHFUL
    // All 3 clients make this first clock call and discard its result.
    (void)rdplib_platform_current_time_ms();
#endif
    return serial_calculate_time_empty_ms(rdplib_platform_current_time_ms(), waiting_bytes, 0);
}

uint32_t serial_get_stall_time(serial_t *serial)
{
    uint32_t stall_time_ms = 0;

    rdplib_platform_mutex_lock(&serial->lock);
    if (serial->pending_sends.head)
    {
        serial_tx_buf_t *active_write = (serial_tx_buf_t *)serial->pending_sends.head->value;

        if (active_write)
        {
            stall_time_ms = serial_calculate_stall_time_ms(rdplib_platform_current_time_ms(), active_write->write_start_time_ms, active_write->write_bytes);
        }
    }
    rdplib_platform_mutex_unlock(&serial->lock);
    return stall_time_ms;
}
