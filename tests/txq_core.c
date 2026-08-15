// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "txq.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(offsetof(txq_t, list) == 0, "txq_t::list moved");
_Static_assert(offsetof(txq_t, queue_size) == sizeof(list_t), "txq_t::queue_size moved");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(txq_t) == 0x18, "txq_t no longer matches the recovered 32-bit layout");
#endif
_Static_assert(_Generic(&txq_init, void (*)(txq_t *): 1, default: 0), "txq_init signature");
_Static_assert(_Generic(&txq_create, void (*)(txq_t *): 1, default: 0), "txq_create signature");
_Static_assert(_Generic(&txq_remove_head, msg_outgoing_t *(*)(txq_t *): 1, default: 0), "txq_remove_head signature");
_Static_assert(_Generic(&txq_peek_head, msg_outgoing_t *(*)(txq_t *): 1, default: 0), "txq_peek_head signature");
_Static_assert(_Generic(&txq_add_tail, void (*)(txq_t *, msg_outgoing_t *): 1, default: 0), "txq_add_tail signature");
_Static_assert(_Generic(&txq_get_queue_size, uint32_t (*)(txq_t *): 1, default: 0), "txq_get_queue_size signature");
_Static_assert(_Generic(&txq_remove_msgid, msg_outgoing_t *(*)(txq_t *, uint16_t): 1, default: 0), "txq_remove_msgid signature");
_Static_assert(_Generic(&txq_get_oldest_time_sent, uint32_t (*)(txq_t *): 1, default: 0), "txq_get_oldest_time_sent signature");
#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&txq_destroy, void (*)(txq_t *): 1, default: 0), "txq_destroy signature");
#endif

static void initialize_message(msg_outgoing_t *outgoing, uint16_t msgid, uint32_t size, uint32_t time_first_sent)
{
    memset(outgoing, 0, sizeof(*outgoing));
    outgoing->txq_link.item = outgoing;
    outgoing->msgid = msgid;
    outgoing->size = size;
    outgoing->time_first_sent = time_first_sent;
}

static void assert_queue_order(const txq_t *txq, msg_outgoing_t *const *expected, uint32_t count)
{
    const rdp_link_t *link = txq->list.head;
    const rdp_link_t *previous = NULL;
    uint32_t index;

    assert(txq->list.size == count);
    for (index = 0; index < count; ++index)
    {
        assert(link == &expected[index]->txq_link);
        assert(link->item == expected[index]);
        assert(link->prev == previous);
        previous = link;
        link = link->next;
    }
    assert(link == NULL);
    assert(txq->list.tail == previous);
    if (count == 0)
    {
        assert(txq->list.head == NULL);
    }
    else
    {
        assert(txq->list.head == &expected[0]->txq_link);
        assert(txq->list.head->prev == NULL);
        assert(txq->list.tail->next == NULL);
    }
}

static void test_layout_init_and_create(void)
{
    txq_t txq;
    Ptxq_t ptr = &txq;

    memset(&txq, 0xA5, sizeof(txq));
    ptr->queue_size = UINT32_C(0x12345678);
    txq_init(ptr);

    assert((void *)ptr == (void *)&ptr->list);
    assert(ptr->list.head == NULL);
    assert(ptr->list.tail == NULL);
    assert(ptr->list.size == 0);
    assert(ptr->list.keycmp == NULL);
    assert(ptr->list.sorted == 0);
    assert(ptr->queue_size == UINT32_C(0x12345678));

    txq_create(ptr);
    assert(ptr->list.head == NULL);
    assert(ptr->list.tail == NULL);
    assert(ptr->list.size == 0);
    assert(ptr->list.keycmp == NULL);
    assert(ptr->list.sorted == 0);
    assert(ptr->queue_size == 0);
    assert(txq_get_queue_size(ptr) == 0);
    list_destroy(&ptr->list);
}

static void test_empty_and_fifo_helpers(void)
{
    txq_t txq;
    msg_outgoing_t messages[3];
    msg_outgoing_t *expected[] = {&messages[0], &messages[1], &messages[2]};
    const uint32_t sizes[] = {7, 11, 13};
    uint32_t expected_size;
    uint32_t index;

    txq_init(&txq);
    txq_create(&txq);
    assert(txq_peek_head(&txq) == NULL);
    assert(txq_remove_head(&txq) == NULL);
    assert(txq_get_queue_size(&txq) == 0);

    expected_size = 0;
    for (index = 0; index < 3; ++index)
    {
        initialize_message(&messages[index], (uint16_t)(100u + index), sizes[index], 0);
        txq_add_tail(&txq, &messages[index]);
        expected_size += sizes[index];
        assert(txq_get_queue_size(&txq) == expected_size);
    }
    assert_queue_order(&txq, expected, 3);

    for (index = 0; index < 3; ++index)
    {
        assert(txq_peek_head(&txq) == &messages[index]);
        assert(txq_remove_head(&txq) == &messages[index]);
        expected_size -= sizes[index];
        assert(txq_get_queue_size(&txq) == expected_size);
    }
    assert(txq_peek_head(&txq) == NULL);
    assert(txq_remove_head(&txq) == NULL);
    assert_queue_order(&txq, NULL, 0);
    list_destroy(&txq.list);
}

static void test_remove_msgid_positions_and_stable_order(void)
{
    txq_t txq;
    msg_outgoing_t messages[4];
    msg_outgoing_t *expected_all[] = {&messages[0], &messages[1], &messages[2], &messages[3]};
    msg_outgoing_t *expected_after_head[] = {&messages[1], &messages[2], &messages[3]};
    msg_outgoing_t *expected_after_middle[] = {&messages[1], &messages[3]};
    msg_outgoing_t *expected_after_tail[] = {&messages[1]};
    const uint32_t sizes[] = {3, 5, 7, 11};
    uint32_t index;

    txq_init(&txq);
    txq_create(&txq);
    for (index = 0; index < 4; ++index)
    {
        initialize_message(&messages[index], (uint16_t)(200u + index), sizes[index], 0);
        txq_add_tail(&txq, &messages[index]);
    }

    assert(txq_remove_msgid(&txq, UINT16_C(999)) == NULL);
    assert(txq_get_queue_size(&txq) == 26);
    assert_queue_order(&txq, expected_all, 4);

    assert(txq_remove_msgid(&txq, UINT16_C(200)) == &messages[0]);
    assert(txq_get_queue_size(&txq) == 23);
    assert_queue_order(&txq, expected_after_head, 3);

    assert(txq_remove_msgid(&txq, UINT16_C(202)) == &messages[2]);
    assert(txq_get_queue_size(&txq) == 16);
    assert_queue_order(&txq, expected_after_middle, 2);

    assert(txq_remove_msgid(&txq, UINT16_C(203)) == &messages[3]);
    assert(txq_get_queue_size(&txq) == 5);
    assert_queue_order(&txq, expected_after_tail, 1);

    assert(txq_remove_head(&txq) == &messages[1]);
    assert(txq_get_queue_size(&txq) == 0);
    assert_queue_order(&txq, NULL, 0);
    list_destroy(&txq.list);
}

#ifndef RDPLIB_DEBUG
static void test_remove_msgid_removes_all_duplicates(void)
{
    txq_t txq;
    msg_outgoing_t messages[5];
    msg_outgoing_t *expected[] = {&messages[1], &messages[3]};
    const uint16_t msgids[] = {400, 401, 400, 402, 400};
    const uint32_t sizes[] = {2, 3, 5, 7, 11};
    uint32_t index;

    txq_init(&txq);
    txq_create(&txq);
    for (index = 0; index < 5; ++index)
    {
        initialize_message(&messages[index], msgids[index], sizes[index], 0);
        txq_add_tail(&txq, &messages[index]);
    }

    assert(txq_remove_msgid(&txq, 400) == &messages[4]);
    assert(txq_get_queue_size(&txq) == 10);
    assert_queue_order(&txq, expected, 2);

    assert(txq_remove_head(&txq) == &messages[1]);
    assert(txq_remove_head(&txq) == &messages[3]);
    assert(txq_get_queue_size(&txq) == 0);
    list_destroy(&txq.list);
}
#endif

static void test_oldest_time_wrap_and_non_mutation(void)
{
    txq_t txq;
    msg_outgoing_t messages[3];
    msg_outgoing_t *expected[] = {&messages[0], &messages[1], &messages[2]};
    uint32_t index;

    txq_init(&txq);
    txq_create(&txq);
    initialize_message(&messages[0], 300, 13, 100);
    initialize_message(&messages[1], 301, 17, 50);
    initialize_message(&messages[2], 302, 19, 75);
    for (index = 0; index < 3; ++index)
    {
        txq_add_tail(&txq, &messages[index]);
    }

    assert(txq_get_oldest_time_sent(&txq) == 50);
    assert(txq_get_queue_size(&txq) == 49);
    assert_queue_order(&txq, expected, 3);

    messages[0].time_first_sent = UINT32_C(0x00000010);
    messages[1].time_first_sent = UINT32_C(0xFFFFFFF0);
    messages[2].time_first_sent = UINT32_C(0x00000020);
    assert(txq_get_oldest_time_sent(&txq) == UINT32_C(0xFFFFFFF0));
    assert(txq_get_queue_size(&txq) == 49);
    assert_queue_order(&txq, expected, 3);

    for (index = 0; index < 3; ++index)
    {
        assert(txq_remove_head(&txq) == &messages[index]);
    }
    assert(txq_get_queue_size(&txq) == 0);
    list_destroy(&txq.list);
}

#ifdef RDP_DEAD_CODE
static void test_dead_destroy_is_referenced(void)
{
    void (*destroy_function)(txq_t *) = txq_destroy;

    assert(destroy_function == txq_destroy);
    (void)destroy_function;
}
#endif

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_layout_init_and_create();
    test_empty_and_fifo_helpers();
    test_remove_msgid_positions_and_stable_order();
#ifndef RDPLIB_DEBUG
    test_remove_msgid_removes_all_duplicates();
#endif
    test_oldest_time_wrap_and_non_mutation();
#ifdef RDP_DEAD_CODE
    test_dead_destroy_is_referenced();
#endif
    return 0;
}
