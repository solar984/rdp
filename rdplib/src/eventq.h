// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Event queue and wait record.
#ifndef RDPLIB_EVENTQ_H
#define RDPLIB_EVENTQ_H

#include <stdint.h>

#include "container.h"
#include "event.h"
#include "rdplib_platform.h"

typedef struct rdp_eventq_t
{
    rdp_pqueue_t queue;
    rdplib_platform_mutex_t lock;
} rdp_eventq_t;

typedef struct rdp_timeval_t
{
    uint32_t seconds;
    uint32_t microseconds;
} rdp_timeval_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Min heap comparator.
// Put finite deadlines before infinite entries.  Finite deadlines are
// compared with wrapping signed 32 bit subtraction.
int ascending_timeout_data_cmp(const void *left, const void *right);

// Creates the event min heap, initially sized for 1 entry, after initializing its platform lock.
int eventq_create(rdp_eventq_t *events);

// Releases heap storage and its platform lock. Membership must be empty.
void eventq_destroy(rdp_eventq_t *events);

// Return NULL when the queue is empty or its first deadline is infinite.  The
// caller must lock the queue because the clients do not lock it here.
rdp_timeval_t *eventq_get_event_timeout(rdp_eventq_t *events, rdp_timeval_t *timeout);

#ifdef __cplusplus
}
#endif

#endif /* RDPLIB_EVENTQ_H */
