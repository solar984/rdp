// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Application receive handoff recovered from rdp_enqueue_arrival,
// rdp_handle_complete_arrival, and rdp_receive.
//
// The clients use 2 lists and allow a single application thread to receive.
#ifndef RDP_APPLICATION_RECEIVE_H
#define RDP_APPLICATION_RECEIVE_H

#include <stdint.h>

#include "queue.h"
#include "rdplib_platform.h"

struct rdp_t;
struct connection_t;

typedef struct rdp_application_receive_t
{
    rdplib_platform_semaphore_t arrival_semaphore;
    rdp_rxq_t producer_queue;
    rdplib_platform_mutex_t producer_lock;
    rdp_rxq_t consumer_queue;
    rdplib_platform_mutex_t consumer_lock;
} rdp_application_receive_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Add a complete message to the application queue, or release it when the
// connection has entered linger.  This takes the producer lock.  A successful
// call gives the queue ownership of the message.
void rdp_enqueue_arrival(rdp_application_receive_t *receive, msg_arrival_t *message);

// Handle FIN, unreliable sequencing, and reliable stream ordering before a
// complete message reaches the application.  The connection receive state
// must be locked.
void rdp_handle_complete_arrival(struct rdp_t *owner, struct connection_t *connection, msg_arrival_t *message);

// Return an application message and wait for at most timeout_ms.  0 polls
// and -1 waits forever.  Only a single application thread may call this.
// Moves the whole producer list into an empty receive list instead of merging them.
msg_arrival_t *rdp_receive(struct rdp_t *owner, int32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* RDP_APPLICATION_RECEIVE_H */
