// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "serial.h"

#include <string.h>

#include "rdplib_platform.h"

void serial_rx_fill_windows(serial_rx_t *receive, intptr_t endpoint)
{
    for (;;)
    {
        rdplib_platform_serial_async_t async_state;
        uint32_t free_bytes;
        uint32_t bytes_read = 0;
        uint32_t error_code = 0;
        int succeeded;

        if (receive->write_cursor < receive->read_cursor)
        {
            free_bytes = (uint32_t)(receive->read_cursor - receive->write_cursor);
        }
        else
        {
            free_bytes = (uint32_t)((receive->buffer + receive->capacity) - receive->write_cursor);
            if (receive->used_bytes == receive->capacity)
            {
                return;
            }
        }
        if (!free_bytes)
        {
            return;
        }

        memset(&async_state, 0, sizeof(async_state));
        succeeded = rdplib_platform_serial_read(endpoint, &async_state, receive->write_cursor, free_bytes, &bytes_read, &error_code);
        if (!succeeded && error_code == RDPLIB_PLATFORM_ERROR_IO_PENDING)
        {
            succeeded = rdplib_platform_serial_get_read_result(endpoint, &async_state, 1, &bytes_read, &error_code);
            if (!succeeded)
            {
                bytes_read = 0;
            }
        }

        if (succeeded)
        {
            receive->write_cursor += bytes_read;
            if (receive->write_cursor == receive->buffer + receive->capacity)
            {
                receive->write_cursor = receive->buffer;
            }
            receive->used_bytes += bytes_read;
        }
        else
        {
#ifdef RDPLIB_SOURCE_FAITHFUL
            // The Windows source samples time and GetLastError again here;
            // neither value is stored or returned by any caller.
            (void)rdplib_platform_current_time_ms();
#endif
        }

        if (bytes_read < free_bytes)
        {
            return;
        }
    }
}

void serial_rx_init(serial_rx_t *receive)
{
    memset(receive, 0, sizeof(*receive));
}

int serial_rx_create(serial_rx_t *receive, uint32_t capacity)
{
    receive->buffer = (uint8_t *)rdplib_platform_malloc(capacity);
    if (!receive->buffer)
    {
        return 2;
    }

    receive->write_cursor = receive->buffer;
    receive->read_cursor = receive->buffer;
    receive->capacity = capacity;
    return 0;
}

void serial_rx_destroy(serial_rx_t *receive)
{
    if (receive->buffer)
    {
        rdplib_platform_free(receive->buffer);
        receive->buffer = NULL;
    }
}

static uint32_t serial_rx_contiguous_bytes(const serial_rx_t *receive)
{
    if (receive->read_cursor < receive->write_cursor)
    {
        return (uint32_t)(receive->write_cursor - receive->read_cursor);
    }

    if (!receive->used_bytes)
    {
        return 0;
    }

    return (uint32_t)((receive->buffer + receive->capacity) - receive->read_cursor);
}

void serial_rx_peek(const serial_rx_t *receive, void *destination, uint32_t byte_count)
{
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *source = receive->read_cursor;
    uint32_t contiguous_bytes = serial_rx_contiguous_bytes(receive);

    if (contiguous_bytes < byte_count)
    {
        memcpy(output, source, contiguous_bytes);
        output += contiguous_bytes;
        byte_count -= contiguous_bytes;
        source = receive->buffer;
    }

    memcpy(output, source, byte_count);
}

void serial_rx_read(serial_rx_t *receive, void *destination, uint32_t byte_count)
{
    uint8_t *output = (uint8_t *)destination;

    while (byte_count)
    {
        uint32_t contiguous_bytes = serial_rx_contiguous_bytes(receive);
        uint32_t copy_bytes = contiguous_bytes < byte_count ? contiguous_bytes : byte_count;

        if (output)
        {
            memcpy(output, receive->read_cursor, copy_bytes);
            output += copy_bytes;
        }

        byte_count -= copy_bytes;
        receive->read_cursor += copy_bytes;
        if (receive->read_cursor == receive->buffer + receive->capacity)
        {
            receive->read_cursor = receive->buffer;
        }
        receive->used_bytes -= copy_bytes;
    }
}
