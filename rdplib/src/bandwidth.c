// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "bandwidth.h"

// Supplied by the selected platform module.
extern uint32_t time_get_ms(void);

uint32_t bandwidth_init(_bandwidth_t *bandwidth)
{
    bandwidth->queued_bytes = 0;
    bandwidth->update_time_ms = time_get_ms();
    bandwidth->bytes_per_second = 3000;
    return bandwidth->update_time_ms;
}

uint32_t bandwidth_get_queue_size(_bandwidth_t *bandwidth)
{
    if (bandwidth->queued_bytes)
    {
        uint32_t now_ms = time_get_ms();
        uint32_t elapsed_ms = now_ms - bandwidth->update_time_ms;
        uint64_t transmitted = ((uint64_t)bandwidth->bytes_per_second * elapsed_ms) / 1000u;

        if (transmitted < bandwidth->queued_bytes)
        {
            bandwidth->queued_bytes -= (uint32_t)transmitted;
        }
        else
        {
            bandwidth->queued_bytes = 0;
        }
        bandwidth->update_time_ms = now_ms;
    }
    return bandwidth->queued_bytes;
}

uint32_t bandwidth_get_time_empty(const _bandwidth_t *bandwidth)
{
    return bandwidth->update_time_ms + (bandwidth->queued_bytes * 1000u) / bandwidth->bytes_per_second;
}

uint32_t bandwidth_enqueue_bytes(_bandwidth_t *bandwidth, uint32_t byte_count)
{
    if (bandwidth->queued_bytes)
    {
        uint32_t now_ms = time_get_ms();
        uint32_t elapsed_ms = now_ms - bandwidth->update_time_ms;
        uint64_t transmitted = ((uint64_t)bandwidth->bytes_per_second * elapsed_ms) / 1000u;

        if (transmitted < bandwidth->queued_bytes)
        {
            bandwidth->queued_bytes -= (uint32_t)transmitted;
        }
        else
        {
            bandwidth->queued_bytes = 0;
        }
        bandwidth->update_time_ms = now_ms;
    }

    bandwidth->queued_bytes += byte_count;
    bandwidth->update_time_ms = time_get_ms();
    return bandwidth->update_time_ms;
}
