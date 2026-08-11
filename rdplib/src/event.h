// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Absolute event deadline used as the priority queue key.
#ifndef RDP_EVENT_H
#define RDP_EVENT_H

#include <stdint.h>

typedef struct _timeout_data
{
    uint32_t infinite;    // Nonzero sorts after every finite event.
    uint32_t deadline_ms; // Absolute wrapping millisecond clock value.
} rdp_timeout_data_t;

#endif /* RDP_EVENT_H */
