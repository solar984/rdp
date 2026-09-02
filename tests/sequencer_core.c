// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sequencer.h"

_Static_assert(_Generic(&sequencer_init, void (*)(sequencer_t *): 1, default: 0), "sequencer_init signature");
_Static_assert(_Generic(&sequencer_create, void (*)(sequencer_t *): 1, default: 0), "sequencer_create signature");
_Static_assert(_Generic(&sequencer_destroy, void (*)(sequencer_t *): 1, default: 0), "sequencer_destroy signature");
_Static_assert(_Generic(&sequencer_remove_head, msg_arrival_t *(*)(sequencer_t *): 1, default: 0), "sequencer_remove_head signature");
_Static_assert(_Generic(&sequencer_peek_head, msg_arrival_t *(*)(sequencer_t *): 1, default: 0), "sequencer_peek_head signature");
_Static_assert(_Generic(&sequencer_insert, void (*)(sequencer_t *, msg_arrival_t *): 1, default: 0), "sequencer_insert signature");

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(offsetof(sequencer_t, list) == 0, "sequencer_t::list moved");
_Static_assert(sizeof(sequencer_t) == sizeof(list_t), "sequencer_t must remain a native list wrapper");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(sequencer_t) == 0x14, "sequencer_t no longer matches the recovered 32-bit layout");
#endif

static void initialize_arrival(msg_arrival_t *arrival, uint8_t stream_seqnum)
{
    memset(arrival, 0, sizeof(*arrival));
    msg_arrival_init(arrival, 0);
    arrival->stream_seqnum = stream_seqnum;
    msg_arrival_prepare_for_sequencer(arrival);
}

static void assert_order(const sequencer_t *sequencer, msg_arrival_t *const *expected, uint32_t count)
{
    const rdp_link_t *link = sequencer->list.head;
    const rdp_link_t *previous = NULL;
    uint32_t index;

    assert(sequencer->list.size == count);
    for (index = 0; index < count; ++index)
    {
        assert(link == &expected[index]->rxq_link);
        assert(link->item == expected[index]);
        assert(link->key.p == &expected[index]->stream_seqnum);
        assert(link->prev == previous);
        previous = link;
        link = link->next;
    }
    assert(link == NULL);
    assert(sequencer->list.tail == previous);
    if (count == 0)
    {
        assert(sequencer->list.head == NULL);
    }
    else
    {
        assert(sequencer->list.head == &expected[0]->rxq_link);
        assert(sequencer->list.head->prev == NULL);
        assert(sequencer->list.tail->next == NULL);
    }
}

static void test_layout_init_and_create(void)
{
    sequencer_t sequencer;
    Psequencer_t ptr = &sequencer;

    memset(&sequencer, 0xA5, sizeof(sequencer));
    sequencer_init(ptr);

    assert((void *)ptr == (void *)&ptr->list);
    assert(ptr->list.head == NULL);
    assert(ptr->list.tail == NULL);
    assert(ptr->list.size == 0);
    assert(ptr->list.keycmp == NULL);
    assert(ptr->list.sorted == 0);

    sequencer_create(ptr);
    assert(ptr->list.head == NULL);
    assert(ptr->list.tail == NULL);
    assert(ptr->list.size == 0);
    assert(ptr->list.keycmp == uint8_cmp);
    assert(ptr->list.sorted == 1);
    assert(sequencer_peek_head(ptr) == NULL);
    assert(sequencer_remove_head(ptr) == NULL);
    sequencer_destroy(ptr);
}

static void test_stable_sorted_wrap_order(void)
{
    sequencer_t sequencer;
    msg_arrival_t arrivals[5];
    msg_arrival_t *expected[] = {&arrivals[1], &arrivals[3], &arrivals[0], &arrivals[4], &arrivals[2]};
    uint32_t index;

    sequencer_init(&sequencer);
    sequencer_create(&sequencer);
    initialize_arrival(&arrivals[0], UINT8_C(0x00));
    initialize_arrival(&arrivals[1], UINT8_C(0xFE));
    initialize_arrival(&arrivals[2], UINT8_C(0x01));
    initialize_arrival(&arrivals[3], UINT8_C(0xFF));
    initialize_arrival(&arrivals[4], UINT8_C(0x00));

    sequencer_insert(&sequencer, &arrivals[0]);
    sequencer_insert(&sequencer, &arrivals[1]);
    sequencer_insert(&sequencer, &arrivals[2]);
    sequencer_insert(&sequencer, &arrivals[3]);
    sequencer_insert(&sequencer, &arrivals[4]);
    assert_order(&sequencer, expected, 5);

    for (index = 0; index < 5; ++index)
    {
        assert(sequencer_peek_head(&sequencer) == expected[index]);
        assert(sequencer_remove_head(&sequencer) == expected[index]);
    }
    assert(sequencer_peek_head(&sequencer) == NULL);
    assert(sequencer_remove_head(&sequencer) == NULL);
    assert_order(&sequencer, NULL, 0);
    sequencer_destroy(&sequencer);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_layout_init_and_create();
    test_stable_sorted_wrap_order();
    return 0;
}
