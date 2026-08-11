// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "rdplib_platform.h"

#include <errno.h>
#include <time.h>

void rdplib_platform_semaphore_init(rdplib_platform_semaphore_t *semaphore)
{
    semaphore->created = 0;
}

int rdplib_platform_semaphore_create(rdplib_platform_semaphore_t *semaphore)
{
    if (sem_init(&semaphore->value, 0, 0) != 0)
    {
        return 3;
    }

    semaphore->created = 1;
    return 0;
}

void rdplib_platform_semaphore_destroy(rdplib_platform_semaphore_t *semaphore)
{
    if (semaphore->created)
    {
        (void)sem_destroy(&semaphore->value);
    }
}

static int wait_without_timeout(rdplib_platform_semaphore_t *semaphore)
{
    int result;

    do
    {
        result = sem_wait(&semaphore->value);
    } while (result != 0 && errno == EINTR);

    return result == 0;
}

int rdplib_platform_semaphore_wait(rdplib_platform_semaphore_t *semaphore, int32_t timeout_ms)
{
    struct timespec deadline;
    int result;

    if (timeout_ms < 0)
    {
        return wait_without_timeout(semaphore);
    }
    if (timeout_ms == 0)
    {
        return sem_trywait(&semaphore->value) == 0;
    }

    (void)clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }

    do
    {
        result = sem_timedwait(&semaphore->value, &deadline);
    } while (result != 0 && errno == EINTR);

    return result == 0;
}

void rdplib_platform_semaphore_signal(rdplib_platform_semaphore_t *semaphore)
{
    (void)sem_post(&semaphore->value);
}
