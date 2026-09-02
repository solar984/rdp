// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "arrival_fragid.h"

_Static_assert(_Generic(&arrival_fragid_init, void (*)(arrival_fragid_t *): 1, default: 0), "arrival_fragid_init signature");
_Static_assert(_Generic(&arrival_fragid_create, void (*)(arrival_fragid_t *): 1, default: 0), "arrival_fragid_create signature");
_Static_assert(_Generic(&arrival_fragid_destroy, void (*)(arrival_fragid_t *): 1, default: 0), "arrival_fragid_destroy signature");
_Static_assert(_Generic(&arrival_fragid_remove_head, msg_arrival_t *(*)(arrival_fragid_t *): 1, default: 0),
               "arrival_fragid_remove_head signature");
_Static_assert(_Generic(&arrival_fragid_lookup, msg_arrival_t *(*)(arrival_fragid_t *, uint16_t *): 1, default: 0),
               "arrival_fragid_lookup signature");
_Static_assert(_Generic(&arrival_fragid_remove_by_ptr, msg_arrival_t *(*)(arrival_fragid_t *, msg_arrival_t *): 1, default: 0),
               "arrival_fragid_remove_by_ptr signature");
_Static_assert(_Generic(&arrival_fragid_insert, void (*)(arrival_fragid_t *, msg_arrival_t *): 1, default: 0),
               "arrival_fragid_insert signature");

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(offsetof(arrival_fragid_t, list) == 0, "arrival_fragid_t::list moved");
_Static_assert(sizeof(arrival_fragid_t) == sizeof(list_t), "arrival_fragid_t must remain a native list wrapper");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(arrival_fragid_t) == 0x14, "arrival_fragid_t no longer matches the recovered 32-bit layout");
#endif

static void initialize_arrival(msg_arrival_t *arrival, uint16_t fragid)
{
    memset(arrival, 0, sizeof(*arrival));
    msg_arrival_init(arrival, fragid);
}

static void assert_order(const arrival_fragid_t *arrival_fragid, msg_arrival_t *const *expected, uint32_t count)
{
    const rdp_link_t *link = arrival_fragid->list.head;
    const rdp_link_t *previous = NULL;
    uint32_t index;

    assert(arrival_fragid->list.size == count);
    for (index = 0; index < count; ++index)
    {
        assert(link == &expected[index]->rxq_link);
        assert(link->item == expected[index]);
        assert(link->prev == previous);
        previous = link;
        link = link->next;
    }
    assert(link == NULL);
    assert(arrival_fragid->list.tail == previous);
    if (count == 0)
    {
        assert(arrival_fragid->list.head == NULL);
    }
    else
    {
        assert(arrival_fragid->list.head == &expected[0]->rxq_link);
        assert(arrival_fragid->list.head->prev == NULL);
        assert(arrival_fragid->list.tail->next == NULL);
    }
}

static void test_layout_init_and_create(void)
{
    arrival_fragid_t arrival_fragid;
    Parrival_fragid_t ptr = &arrival_fragid;

    memset(&arrival_fragid, 0xA5, sizeof(arrival_fragid));
    arrival_fragid_init(ptr);

    assert((void *)ptr == (void *)&ptr->list);
    assert(ptr->list.head == NULL);
    assert(ptr->list.tail == NULL);
    assert(ptr->list.size == 0);
    assert(ptr->list.keycmp == NULL);
    assert(ptr->list.sorted == 0);

    arrival_fragid_create(ptr);
    assert(ptr->list.head == NULL);
    assert(ptr->list.tail == NULL);
    assert(ptr->list.size == 0);
    assert(ptr->list.keycmp == uint16_cmp);
    assert(ptr->list.sorted == 1);
    arrival_fragid_destroy(ptr);
}

static void test_sorted_wrap_lookup_and_removal(void)
{
    arrival_fragid_t arrival_fragid;
    msg_arrival_t arrivals[5];
    msg_arrival_t *expected_all[] = {&arrivals[1], &arrivals[3], &arrivals[0], &arrivals[4], &arrivals[2]};
    msg_arrival_t *expected_without_head[] = {&arrivals[3], &arrivals[0], &arrivals[4], &arrivals[2]};
    msg_arrival_t *expected_without_middle[] = {&arrivals[3], &arrivals[4], &arrivals[2]};
    msg_arrival_t *expected_without_tail[] = {&arrivals[3], &arrivals[4]};
    uint16_t key;

    arrival_fragid_init(&arrival_fragid);
    arrival_fragid_create(&arrival_fragid);
    initialize_arrival(&arrivals[0], UINT16_C(0x0000));
    initialize_arrival(&arrivals[1], UINT16_C(0xFFFE));
    initialize_arrival(&arrivals[2], UINT16_C(0x0001));
    initialize_arrival(&arrivals[3], UINT16_C(0xFFFF));
    initialize_arrival(&arrivals[4], UINT16_C(0x0000));

    arrival_fragid_insert(&arrival_fragid, &arrivals[0]);
    arrival_fragid_insert(&arrival_fragid, &arrivals[1]);
    arrival_fragid_insert(&arrival_fragid, &arrivals[2]);
    arrival_fragid_insert(&arrival_fragid, &arrivals[3]);
    arrival_fragid_insert(&arrival_fragid, &arrivals[4]);
    assert_order(&arrival_fragid, expected_all, 5);

    key = UINT16_C(0x0000);
    assert(arrival_fragid_lookup(&arrival_fragid, &key) == &arrivals[0]);
    key = UINT16_C(0x1234);
    assert(arrival_fragid_lookup(&arrival_fragid, &key) == NULL);

    assert(arrival_fragid_remove_by_ptr(&arrival_fragid, &arrivals[1]) == &arrivals[1]);
    assert_order(&arrival_fragid, expected_without_head, 4);
    assert(arrival_fragid_remove_by_ptr(&arrival_fragid, &arrivals[0]) == &arrivals[0]);
    assert_order(&arrival_fragid, expected_without_middle, 3);
    assert(arrival_fragid_remove_by_ptr(&arrival_fragid, &arrivals[2]) == &arrivals[2]);
    assert_order(&arrival_fragid, expected_without_tail, 2);

    assert(arrival_fragid_remove_head(&arrival_fragid) == &arrivals[3]);
    assert(arrival_fragid_remove_head(&arrival_fragid) == &arrivals[4]);
    assert(arrival_fragid_remove_head(&arrival_fragid) == NULL);
    assert_order(&arrival_fragid, NULL, 0);
    arrival_fragid_destroy(&arrival_fragid);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_layout_init_and_create();
    test_sorted_wrap_lookup_and_removal();
    return 0;
}
