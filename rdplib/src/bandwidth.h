// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// _bandwidth_t -- byte rate queue model used by the transmit path.
//
// The 3 fields and the 12 byte layout are verified in the PPC, Intel,
// and TAKP Windows instructions.
#ifndef RDP_BANDWIDTH_H
#define RDP_BANDWIDTH_H

#include <stdint.h>
typedef struct _bandwidth_t
{
    uint32_t queued_bytes;   // Virtual backlog drained at bytes_per_second.
    uint32_t update_time_ms; // Millisecond clock sample of the last backlog update.
    uint32_t bytes_per_second;
} _bandwidth_t;

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t bandwidth_init(_bandwidth_t *bandwidth);
uint32_t bandwidth_get_queue_size(_bandwidth_t *bandwidth);
uint32_t bandwidth_get_time_empty(const _bandwidth_t *bandwidth);
uint32_t bandwidth_enqueue_bytes(_bandwidth_t *bandwidth, uint32_t byte_count);

#ifdef __cplusplus
}
#endif

#endif /* RDP_BANDWIDTH_H */
