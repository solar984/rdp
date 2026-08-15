// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

#ifdef RDP_DEAD_CODE

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hash.h"

_Static_assert(_Generic((keyhash_f)0, uint32_t (*)(void *): 1, default: 0), "keyhash_f signature");
_Static_assert(_Generic(&hash_init, void (*)(hash_t *): 1, default: 0), "hash_init signature");
_Static_assert(_Generic(&hash_create, uint32_t (*)(hash_t *, uint32_t, keycmp_f): 1, default: 0), "hash_create signature");
_Static_assert(_Generic(&hash_destroy, void (*)(hash_t *): 1, default: 0), "hash_destroy signature");
_Static_assert(_Generic(&hash_remove_any, void *(*)(hash_t *): 1, default: 0), "hash_remove_any signature");

_Static_assert(offsetof(hash_t, table) == 0, "hash_t::table layout");
_Static_assert(offsetof(hash_t, table_size) == sizeof(void *), "hash_t::table_size layout");
_Static_assert(offsetof(hash_t, keyhash) == (sizeof(void *) == 8 ? 16 : 8), "hash_t::keyhash layout");
_Static_assert(sizeof(hash_t) == (sizeof(void *) == 8 ? 24 : 12), "hash_t size");

#if defined(_WIN32) && !defined(_WIN64)
_Static_assert(offsetof(hash_t, table) == 0x00, "hash_t::table x86 offset");
_Static_assert(offsetof(hash_t, table_size) == 0x04, "hash_t::table_size x86 offset");
_Static_assert(offsetof(hash_t, keyhash) == 0x08, "hash_t::keyhash x86 offset");
_Static_assert(sizeof(hash_t) == 0x0C, "hash_t x86 size");
#endif

typedef struct guarded_hash_t
{
    uint64_t before;
    hash_t hash;
    uint64_t after;
} guarded_hash_t;

static uint32_t historical_keyhash(void *key)
{
    return *(uint32_t *)key;
}

static int compare_int_keys(const void *key_1, const void *key_2)
{
    int value_1;
    int value_2;

    value_1 = *(const int *)key_1;
    value_2 = *(const int *)key_2;
    return (value_1 > value_2) - (value_1 < value_2);
}

static void initialize_link(rdp_link_t *link, void *item, void *key)
{
    memset(link, 0, sizeof(*link));
    link->item = item;
    link->key.p = key;
}

static void test_init_selectivity(void)
{
    guarded_hash_t guarded;

    memset(&guarded, 0xA5, sizeof(guarded));
    guarded.before = UINT64_C(0x0123456789ABCDEF);
    guarded.after = UINT64_C(0xFEDCBA9876543210);
    guarded.hash.keyhash = historical_keyhash;

    hash_init(&guarded.hash);

    assert(guarded.before == UINT64_C(0x0123456789ABCDEF));
    assert(guarded.after == UINT64_C(0xFEDCBA9876543210));
    assert(guarded.hash.table == NULL);
    assert(guarded.hash.table_size == 0);
    assert(guarded.hash.keyhash == historical_keyhash);
}

static void test_create_remove_and_destroy(void)
{
    guarded_hash_t guarded;
    rdp_link_t high_link;
    rdp_link_t low_link;
    rdp_link_t later_link;
    int high_item = 70;
    int low_item = 30;
    int later_item = 40;
    int high_key = 7;
    int low_key = 3;
    int later_key = 4;
    uint32_t bin;

    memset(&guarded, 0xA5, sizeof(guarded));
    guarded.before = UINT64_C(0x1020304050607080);
    guarded.after = UINT64_C(0x8070605040302010);
    hash_init(&guarded.hash);
    guarded.hash.keyhash = historical_keyhash;

    assert(hash_create(&guarded.hash, 3, compare_int_keys) == 0);
    assert(guarded.hash.table != NULL);
    assert(guarded.hash.table_size == 3);
    assert(guarded.hash.keyhash == historical_keyhash);
    for (bin = 0; bin < guarded.hash.table_size; ++bin)
    {
        assert(guarded.hash.table[bin].head == NULL);
        assert(guarded.hash.table[bin].tail == NULL);
        assert(guarded.hash.table[bin].size == 0);
        assert(guarded.hash.table[bin].sorted == 1);
        assert(guarded.hash.table[bin].keycmp == compare_int_keys);
    }

    assert(hash_remove_any(&guarded.hash) == NULL);

    initialize_link(&high_link, &high_item, &high_key);
    initialize_link(&low_link, &low_item, &low_key);
    initialize_link(&later_link, &later_item, &later_key);
    list_insert(&guarded.hash.table[1], &high_link);
    list_insert(&guarded.hash.table[1], &low_link);
    list_insert(&guarded.hash.table[2], &later_link);

    assert(guarded.hash.table[0].size == 0);
    assert(guarded.hash.table[1].size == 2);
    assert(guarded.hash.table[2].size == 1);
    assert(hash_remove_any(&guarded.hash) == &low_item);
    assert(guarded.hash.table[1].size == 1);
    assert(hash_remove_any(&guarded.hash) == &high_item);
    assert(guarded.hash.table[1].size == 0);
    assert(hash_remove_any(&guarded.hash) == &later_item);
    assert(guarded.hash.table[2].size == 0);
    assert(hash_remove_any(&guarded.hash) == NULL);

    hash_destroy(&guarded.hash);
    assert(guarded.before == UINT64_C(0x1020304050607080));
    assert(guarded.after == UINT64_C(0x8070605040302010));
    assert(guarded.hash.table_size == 3);
    assert(guarded.hash.keyhash == historical_keyhash);
}

static void test_destroy_null_table_is_selective(void)
{
    hash_t hash;

    hash.table = NULL;
    hash.table_size = UINT32_C(0xA5A5A5A5);
    hash.keyhash = historical_keyhash;

    hash_destroy(&hash);

    assert(hash.table == NULL);
    assert(hash.table_size == UINT32_C(0xA5A5A5A5));
    assert(hash.keyhash == historical_keyhash);
}

#endif /* RDP_DEAD_CODE */

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

#ifdef RDP_DEAD_CODE
    test_init_selectivity();
    test_create_remove_and_destroy();
    test_destroy_null_table_is_selective();
#endif
    return 0;
}
