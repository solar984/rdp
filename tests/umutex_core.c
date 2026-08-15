// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "umutex.h"

_Static_assert(_Generic(&umutex_create, void (*)(umutex_t *): 1, default: 0), "umutex_create signature");
#ifdef RDPLIB_DEBUG
_Static_assert(_Generic(&umutex_owner, unsigned long (*)(umutex_t *): 1, default: 0), "umutex_owner signature");
#endif
_Static_assert(_Generic(&umutex_destroy, void (*)(umutex_t *): 1, default: 0), "umutex_destroy signature");
_Static_assert(_Generic(&umutex_lock, void (*)(umutex_t *): 1, default: 0), "umutex_lock signature");
_Static_assert(_Generic(&umutex_unlock, void (*)(umutex_t *): 1, default: 0), "umutex_unlock signature");

#ifdef _WIN32
_Static_assert(offsetof(umutex_t, cs) == 0, "umutex_t::cs moved");
#ifdef RDPLIB_DEBUG
_Static_assert(offsetof(umutex_t, owner) == sizeof(CRITICAL_SECTION), "umutex_t::owner moved");
#endif
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(umutex_t) == 0x18 + RDP_WIN32_UMUTEX_OWNER_BYTES, "umutex_t Win32 size");
#else
_Static_assert(sizeof(umutex_t) == 0x28 + RDP_WIN32_UMUTEX_OWNER_BYTES, "umutex_t Win64 size");
#endif
#else
_Static_assert(offsetof(umutex_t, platform) == 0, "umutex_t::platform moved");
#ifndef RDPLIB_DEBUG
_Static_assert(sizeof(umutex_t) == sizeof(rdplib_platform_mutex_t), "non-debug umutex_t must be the platform mutex");
#endif
#endif

static void test_valid_lifecycle(void)
{
    umutex_t mutex;

    memset(&mutex, 0, sizeof(mutex));
    umutex_create(&mutex);
#ifdef RDPLIB_DEBUG
    assert(!umutex_owner(&mutex));
#endif

    umutex_lock(&mutex);
#ifdef RDPLIB_DEBUG
    assert(umutex_owner(&mutex));
#ifdef _WIN32
    assert(mutex.owner == GetCurrentThread());
#else
    assert(mutex.owned);
#endif
#endif
#ifndef _WIN32
    assert(mutex.platform.initialized);
#endif

    umutex_unlock(&mutex);
#ifdef RDPLIB_DEBUG
    assert(!umutex_owner(&mutex));
#endif
    umutex_destroy(&mutex);
#ifndef _WIN32
    assert(!mutex.platform.initialized);
#ifdef RDPLIB_DEBUG
    assert(!mutex.owned);
#endif
#endif
}

#ifndef _WIN32
static void test_posix_invalid_lifecycle_guards(void)
{
    umutex_t mutex;

    memset(&mutex, 0, sizeof(mutex));
    umutex_lock(&mutex);
    umutex_unlock(&mutex);
    umutex_destroy(&mutex);
    assert(!mutex.platform.initialized);
#ifdef RDPLIB_DEBUG
    assert(!mutex.owned);
#endif
}
#endif

int main(void)
{
    test_valid_lifecycle();
#ifndef _WIN32
    test_posix_invalid_lifecycle_guards();
#endif
    return 0;
}
