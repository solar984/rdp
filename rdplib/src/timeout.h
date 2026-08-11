// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// _timeout_t -- weighted 64 sample RTT estimator.
#ifndef RDP_TIMEOUT_H
#define RDP_TIMEOUT_H

#include <stdint.h>

typedef struct rdp_timeout_sample_t
{
    uint16_t milliseconds;
    uint16_t weight; // max(1023 / transmission_count, 1)
} rdp_timeout_sample_t;

// The recovered source contains a native uint64_t accumulator. Its offset is
// compiler dependent in the binaries, but no source level padding is needed.
typedef struct _timeout_t
{
    rdp_timeout_sample_t samples[64];
    uint32_t next_sample;
    uint64_t sum_squares;
    uint32_t weighted_sum;
    uint16_t total_weight;
    uint16_t mean_ms;
    uint16_t deviation_ms;
} _timeout_t;

#ifdef __cplusplus
extern "C"
{
#endif

// initial_weight is present in the original signature but ignored by all 3 clients.
void timeout_init(_timeout_t *state, uint32_t initial_milliseconds, uint16_t initial_weight);

// transmission_count must be nonzero; the live ACK path submits only 1.
void timeout_add_sample(_timeout_t *state, uint32_t milliseconds, uint16_t transmission_count);

#ifdef __cplusplus
}
#endif

#endif /* RDP_TIMEOUT_H */
