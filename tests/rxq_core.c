// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fast.h"
#include "rxq.h"

_Static_assert(_Generic(&rxq_flush_all_messages, uint32_t (*)(rxq_t *, connection_t *): 1, default: 0), "rxq_flush_all_messages signature");
_Static_assert(_Generic(&rxq_init, void (*)(rxq_t *): 1, default: 0), "rxq_init signature");
_Static_assert(_Generic(&rxq_destroy, void (*)(rxq_t *): 1, default: 0), "rxq_destroy signature");
_Static_assert(_Generic(&rxq_remove_head, msg_arrival_t *(*)(rxq_t *): 1, default: 0), "rxq_remove_head signature");
_Static_assert(_Generic(&rxq_peek_head, msg_arrival_t *(*)(rxq_t *): 1, default: 0), "rxq_peek_head signature");
_Static_assert(_Generic(&rxq_add_tail, void (*)(rxq_t *, msg_arrival_t *): 1, default: 0), "rxq_add_tail signature");

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

_Static_assert(offsetof(rxq_t, list) == 0, "rxq_t::list moved");
_Static_assert(sizeof(rxq_t) == sizeof(list_t), "rxq_t must remain list-compatible");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(rxq_t) == 0x14, "rxq_t no longer matches the recovered 32-bit layout");
#endif

typedef union owner_token_storage_t
{
    void *pointer_alignment;
    uint64_t integer_alignment;
} owner_token_storage_t;

static connection_t *owner_token(owner_token_storage_t *storage)
{
    return (connection_t *)(void *)storage;
}

static void initialize_message(msg_arrival_t *message, connection_t *sender, uint16_t fragid)
{
    memset(message, 0, sizeof(*message));
    msg_arrival_init(message, fragid);
    msg_arrival_prepare_for_rxq(message);
    message->sender = sender;
}

static msg_arrival_t *allocate_message(connection_t *sender, uint16_t fragid)
{
    msg_arrival_t *message = (msg_arrival_t *)fast_malloc((uint32_t)sizeof(*message));

    assert(message != NULL);
    initialize_message(message, sender, fragid);
    return message;
}

static void assert_queue_order(const rxq_t *rxq, msg_arrival_t *const *expected, uint32_t count)
{
    const rdp_link_t *link = rxq->list.head;
    const rdp_link_t *previous = NULL;
    uint32_t index;

    assert(rxq->list.size == count);
    for (index = 0; index < count; ++index)
    {
        assert(link == &expected[index]->rxq_link);
        assert(link->item == expected[index]);
        assert(link->prev == previous);
        previous = link;
        link = link->next;
    }
    assert(link == NULL);
    assert(rxq->list.tail == previous);
    if (count == 0)
    {
        assert(rxq->list.head == NULL);
    }
    else
    {
        assert(rxq->list.head == &expected[0]->rxq_link);
        assert(rxq->list.head->prev == NULL);
        assert(rxq->list.tail->next == NULL);
    }
}

static void release_queue_messages(rxq_t *rxq)
{
    msg_arrival_t *message;

    while ((message = rxq_remove_head(rxq)) != NULL)
    {
        fast_free(message);
    }
    rxq_destroy(rxq);
}

static void test_layout_and_lifecycle(void)
{
    rxq_t rxq;
    Prxq_t ptr = &rxq;

    memset(&rxq, 0xA5, sizeof(rxq));
    rxq_init(ptr);
    assert((void *)ptr == (void *)&ptr->list);
    assert(ptr->list.head == NULL);
    assert(ptr->list.tail == NULL);
    assert(ptr->list.size == 0);
    assert(ptr->list.keycmp == NULL);
    assert(ptr->list.sorted == 0);
    rxq_destroy(ptr);
}

static void test_fifo_helpers(void)
{
    rxq_t rxq;
    msg_arrival_t messages[3];
    msg_arrival_t *expected[] = {&messages[0], &messages[1], &messages[2]};
    uint32_t index;

    rxq_init(&rxq);
    assert(rxq_peek_head(&rxq) == NULL);
    assert(rxq_remove_head(&rxq) == NULL);

    for (index = 0; index < 3; ++index)
    {
        initialize_message(&messages[index], NULL, (uint16_t)(index + 1u));
        rxq_add_tail(&rxq, &messages[index]);
    }

    assert_queue_order(&rxq, expected, 3);
    for (index = 0; index < 3; ++index)
    {
        assert(rxq_peek_head(&rxq) == &messages[index]);
        assert(rxq_remove_head(&rxq) == &messages[index]);
    }
    assert(rxq_peek_head(&rxq) == NULL);
    assert(rxq_remove_head(&rxq) == NULL);
    assert_queue_order(&rxq, NULL, 0);
    rxq_destroy(&rxq);
}

static void test_mixed_owner_stable_flush_and_absent_owner(void)
{
    owner_token_storage_t owner_a_storage;
    owner_token_storage_t owner_b_storage;
    owner_token_storage_t owner_c_storage;
    connection_t *owner_a = owner_token(&owner_a_storage);
    connection_t *owner_b = owner_token(&owner_b_storage);
    connection_t *owner_c = owner_token(&owner_c_storage);
    rxq_t rxq;
    msg_arrival_t *messages[5];
    msg_arrival_t *survivors[2];
    uint32_t index;

    rxq_init(&rxq);
    messages[0] = allocate_message(owner_a, 10);
    messages[1] = allocate_message(owner_b, 11);
    messages[2] = allocate_message(owner_a, 12);
    messages[3] = allocate_message(owner_b, 13);
    messages[4] = allocate_message(owner_a, 14);
    for (index = 0; index < 5; ++index)
    {
        rxq_add_tail(&rxq, messages[index]);
    }

    survivors[0] = messages[1];
    survivors[1] = messages[3];
    assert(rxq_flush_all_messages(&rxq, owner_a) == 3);
    assert_queue_order(&rxq, survivors, 2);

    assert(rxq_flush_all_messages(&rxq, owner_c) == 0);
    assert_queue_order(&rxq, survivors, 2);
    release_queue_messages(&rxq);
}

static void test_all_matching_and_empty_flush(void)
{
    owner_token_storage_t owner_storage;
    connection_t *owner = owner_token(&owner_storage);
    rxq_t rxq;
    uint16_t fragid;

    rxq_init(&rxq);
    assert(rxq_flush_all_messages(&rxq, owner) == 0);
    assert_queue_order(&rxq, NULL, 0);

    for (fragid = 20; fragid < 23; ++fragid)
    {
        rxq_add_tail(&rxq, allocate_message(owner, fragid));
    }
    assert(rxq_flush_all_messages(&rxq, owner) == 3);
    assert_queue_order(&rxq, NULL, 0);
    assert(rxq_flush_all_messages(&rxq, owner) == 0);
    rxq_destroy(&rxq);
}

static void test_null_owner_selects_connectionless_messages(void)
{
    owner_token_storage_t owner_a_storage;
    owner_token_storage_t owner_b_storage;
    connection_t *owner_a = owner_token(&owner_a_storage);
    connection_t *owner_b = owner_token(&owner_b_storage);
    rxq_t rxq;
    msg_arrival_t *messages[4];
    msg_arrival_t *survivors[2];
    uint32_t index;

    rxq_init(&rxq);
    messages[0] = allocate_message(owner_a, 30);
    messages[1] = allocate_message(NULL, 31);
    messages[2] = allocate_message(owner_b, 32);
    messages[3] = allocate_message(NULL, 33);
    for (index = 0; index < 4; ++index)
    {
        rxq_add_tail(&rxq, messages[index]);
    }

    survivors[0] = messages[0];
    survivors[1] = messages[2];
    assert(rxq_flush_all_messages(&rxq, NULL) == 2);
    assert_queue_order(&rxq, survivors, 2);
    release_queue_messages(&rxq);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_layout_and_lifecycle();
    test_fifo_helpers();

    fast_malloc_init(584);
    test_mixed_owner_stable_flush_and_absent_owner();
    test_all_matching_and_empty_flush();
    test_null_owner_selects_connectionless_messages();
    fast_malloc_destroy();
    return 0;
}
