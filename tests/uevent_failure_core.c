// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include <semaphore.h>
#include <stdint.h>

#include "test_assert.h"
#include "uevent.h"

static uint32_t init_calls;
static uint32_t destroy_calls;
static uint32_t signal_calls;
static uint32_t wait_calls;

int uevent_test_sem_init(sem_t *sem, int shared, unsigned int value)
{
    (void)sem;
    (void)shared;
    (void)value;
    ++init_calls;
    return -1;
}

int uevent_test_sem_destroy(sem_t *sem)
{
    (void)sem;
    ++destroy_calls;
    return 0;
}

int uevent_test_sem_post(sem_t *sem)
{
    (void)sem;
    ++signal_calls;
    return 0;
}

int uevent_test_sem_wait(sem_t *sem)
{
    (void)sem;
    ++wait_calls;
    return 0;
}

int main(void)
{
    uevent_t event;
    uint32_t result;

    uevent_init(&event);
    result = uevent_create(&event);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(result == 0);
#else
    assert(result == 3);
#endif
    assert(event.created == 0);
    assert(init_calls == 1);

    uevent_signal(&event);
    uevent_wait(&event);
    uevent_destroy(&event);
    assert(signal_calls == 0);
    assert(wait_calls == 0);
    assert(destroy_calls == 0);
    return 0;
}
