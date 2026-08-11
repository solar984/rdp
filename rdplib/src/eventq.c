// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "eventq.h"

#include "connection.h"

int ascending_timeout_data_cmp(const void *left, const void *right)
{
    const rdp_timeout_data_t *left_timeout = (const rdp_timeout_data_t *)left;
    const rdp_timeout_data_t *right_timeout = (const rdp_timeout_data_t *)right;
    int32_t difference = (int32_t)(left_timeout->infinite - right_timeout->infinite);

    if (difference == 0 && !left_timeout->infinite)
    {
        difference = (int32_t)(left_timeout->deadline_ms - right_timeout->deadline_ms);
        if (difference > 0)
        {
            return 1;
        }
        if (difference < 0)
        {
            return -1;
        }
    }

    return difference;
}

int eventq_create(rdp_eventq_t *events)
{
    rdplib_platform_mutex_init(&events->lock);
    return pqueue_create(&events->queue, 1, ascending_timeout_data_cmp);
}

void eventq_destroy(rdp_eventq_t *events)
{
    pqueue_destroy(&events->queue);
    rdplib_platform_mutex_destroy(&events->lock);
}

rdp_timeval_t *eventq_get_event_timeout(rdp_eventq_t *events, rdp_timeval_t *timeout)
{
    connection_t *connection = (connection_t *)pqueue_peek_head(&events->queue);
    int32_t difference;

    if (!connection || connection->event_timeout.infinite)
    {
        return NULL;
    }

    difference = (int32_t)(connection->event_timeout.deadline_ms - rdplib_platform_current_time_ms());
    if (difference < 0)
    {
        timeout->seconds = 0;
        timeout->microseconds = 0;
    }
    else
    {
        timeout->seconds = (uint32_t)difference / 1000u;
        timeout->microseconds = ((uint32_t)difference % 1000u) * 1000u;
    }

    return timeout;
}
