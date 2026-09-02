// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include <stdint.h>

#ifndef _WIN32
#include <errno.h>
#endif

#include "rdplib_platform.h"
#include "test_assert.h"
#include "uevent.h"
#include "usemaphore.h"

_Static_assert(_Generic(&uevent_init, void (*)(uevent_t *): 1, default: 0), "uevent_init signature");
_Static_assert(_Generic(&uevent_create, uint32_t (*)(uevent_t *): 1, default: 0), "uevent_create signature");
_Static_assert(_Generic(&uevent_destroy, void (*)(uevent_t *): 1, default: 0), "uevent_destroy signature");
_Static_assert(_Generic(&uevent_signal, void (*)(uevent_t *): 1, default: 0), "uevent_signal signature");
_Static_assert(_Generic(&uevent_wait, void (*)(uevent_t *): 1, default: 0), "uevent_wait signature");

_Static_assert(_Generic(&usemaphore_init, void (*)(usemaphore_t *): 1, default: 0), "usemaphore_init signature");
_Static_assert(_Generic(&usemaphore_destroy, void (*)(usemaphore_t *): 1, default: 0), "usemaphore_destroy signature");
_Static_assert(_Generic(&usemaphore_create, uint32_t (*)(usemaphore_t *): 1, default: 0), "usemaphore_create signature");
_Static_assert(_Generic(&usemaphore_decrement, uint32_t (*)(usemaphore_t *, uint32_t): 1, default: 0), "usemaphore_decrement signature");
_Static_assert(_Generic(&usemaphore_increment, void (*)(usemaphore_t *): 1, default: 0), "usemaphore_increment signature");

#if defined(_WIN32) && !defined(_WIN64)
_Static_assert(sizeof(uevent_t) == 0x04, "uevent_t x86 size");
_Static_assert(sizeof(usemaphore_t) == 0x04, "usemaphore_t x86 size");
#endif

static void test_event(void)
{
    uevent_t event;

    uevent_init(&event);
#ifdef _WIN32
    assert(event.event == NULL);
#else
    assert(event.created == 0);
#endif
    assert(uevent_create(&event) == 0);
    uevent_signal(&event);
    uevent_wait(&event);
    uevent_destroy(&event);
}

static void test_semaphore(void)
{
    usemaphore_t sem;

    usemaphore_init(&sem);
#ifdef _WIN32
    assert(sem.semid == NULL);
#else
    assert(sem.created == 0);
#endif
    assert(usemaphore_create(&sem) == 0);
    assert(usemaphore_decrement(&sem, 0) == 0);
    usemaphore_increment(&sem);
    assert(usemaphore_decrement(&sem, 0) == 1);
    assert(usemaphore_decrement(&sem, 0) == 0);
    usemaphore_destroy(&sem);
}

static void test_no_buffer_space_error(void)
{
#ifdef _WIN32
    WSASetLastError(WSAENOBUFS);
#else
    errno = ENOBUFS;
#endif
    assert(rdplib_platform_last_socket_error() == 10055);
}

int main(void)
{
    test_event();
    test_semaphore();
    test_no_buffer_space_error();
    return 0;
}
