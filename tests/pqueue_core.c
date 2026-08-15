// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pqueue.h"

_Static_assert(sizeof(void *) == 4 || sizeof(void *) == 8, "supported pointer size");
_Static_assert(sizeof(union _key) == sizeof(void *), "_key size");

_Static_assert(offsetof(qlink, index) == 0, "_qlink.index layout");
_Static_assert(offsetof(qlink, item) == sizeof(void *), "_qlink.item layout");
_Static_assert(offsetof(qlink, key) == 2 * sizeof(void *), "_qlink.key layout");
_Static_assert(sizeof(qlink) == 3 * sizeof(void *), "_qlink size");
_Static_assert(sizeof(struct _qlink) == sizeof(qlink), "_qlink tag and typedef");

_Static_assert(offsetof(pqueue_t, array) == 0, "_pqueue_t.array layout");
_Static_assert(offsetof(pqueue_t, next_element) == sizeof(void *), "_pqueue_t.next_element layout");
_Static_assert(offsetof(pqueue_t, array_size) == sizeof(void *) + sizeof(uint32_t), "_pqueue_t.array_size layout");
_Static_assert(offsetof(pqueue_t, grow_size) == sizeof(void *) + 2 * sizeof(uint32_t), "_pqueue_t.grow_size layout");
_Static_assert(offsetof(pqueue_t, keycmp) == (sizeof(void *) == 8 ? 3 * sizeof(void *) : 4 * sizeof(void *)), "_pqueue_t.keycmp layout");
_Static_assert(sizeof(pqueue_t) == (sizeof(void *) == 8 ? 4 * sizeof(void *) : 5 * sizeof(void *)), "_pqueue_t size");
_Static_assert(sizeof(struct _pqueue_t) == sizeof(pqueue_t), "_pqueue_t tag and typedef");

_Static_assert(offsetof(union _key, i) == 0, "_key.i layout");
_Static_assert(offsetof(union _key, p) == 0, "_key.p layout");
_Static_assert(sizeof(((union _key *)0)->i) == sizeof(int32_t), "_key.i width");

typedef int (*keycmp_signature_t)(const void *, const void *);
typedef uint32_t (*pqueue_create_signature_t)(pqueue_t *, uint32_t, keycmp_f);
typedef void (*pqueue_destroy_signature_t)(pqueue_t *);
typedef uint32_t (*pqueue_insert_signature_t)(pqueue_t *, qlink *);
typedef void *(*pqueue_remove_by_link_signature_t)(pqueue_t *, qlink *);
typedef void (*pqueue_resort_by_link_signature_t)(pqueue_t *, qlink *);
typedef void *(*pqueue_remove_head_signature_t)(pqueue_t *);
typedef void *(*pqueue_peek_head_signature_t)(pqueue_t *);
#ifdef RDP_DEAD_CODE
typedef uint32_t (*pqueue_get_size_signature_t)(pqueue_t *);
#endif

#define ASSERT_EXACT_SIGNATURE(function_name, signature_type) \
    _Static_assert(_Generic(&(function_name), signature_type: 1, default: 0), #function_name " signature")

_Static_assert(_Generic((keycmp_f)0, keycmp_signature_t: 1, default: 0), "keycmp_f signature");
ASSERT_EXACT_SIGNATURE(pqueue_create, pqueue_create_signature_t);
ASSERT_EXACT_SIGNATURE(pqueue_destroy, pqueue_destroy_signature_t);
ASSERT_EXACT_SIGNATURE(pqueue_insert, pqueue_insert_signature_t);
ASSERT_EXACT_SIGNATURE(pqueue_remove_by_link, pqueue_remove_by_link_signature_t);
ASSERT_EXACT_SIGNATURE(pqueue_resort_by_link, pqueue_resort_by_link_signature_t);
ASSERT_EXACT_SIGNATURE(pqueue_remove_head, pqueue_remove_head_signature_t);
ASSERT_EXACT_SIGNATURE(pqueue_peek_head, pqueue_peek_head_signature_t);
#ifdef RDP_DEAD_CODE
ASSERT_EXACT_SIGNATURE(pqueue_get_size, pqueue_get_size_signature_t);
#endif

#undef ASSERT_EXACT_SIGNATURE

enum
{
    COMPARISON_CAPACITY = 32
};

typedef struct comparison_t
{
    const void *left;
    const void *right;
} comparison_t;

static comparison_t comparisons[COMPARISON_CAPACITY];
static uint32_t comparison_count;
static int record_comparisons;

static int compare_integer_keys(const void *left, const void *right)
{
    int left_value;
    int right_value;

    if (record_comparisons)
    {
        assert(comparison_count < COMPARISON_CAPACITY);
        comparisons[comparison_count].left = left;
        comparisons[comparison_count].right = right;
        ++comparison_count;
    }

    left_value = *(const int *)left;
    right_value = *(const int *)right;
    return (left_value > right_value) - (left_value < right_value);
}

static void initialize_link(qlink *link, void *item, void *key)
{
    memset(link, 0, sizeof(*link));
    link->item = item;
    link->key.p = key;
}

static int link_key(const qlink *link)
{
    return *(const int *)link->key.p;
}

static void assert_heap(const pqueue_t *q)
{
    uint32_t element;
    uint32_t left_child;
    uint32_t right_child;

    for (element = 0; element < q->next_element; ++element)
    {
        assert(q->array[element] != NULL);
        assert(q->array[element]->index == element);
        left_child = 2u * element + 1u;
        right_child = left_child + 1u;
        if (left_child < q->next_element)
        {
            assert(link_key(q->array[element]) <= link_key(q->array[left_child]));
        }
        if (right_child < q->next_element)
        {
            assert(link_key(q->array[element]) <= link_key(q->array[right_child]));
        }
    }
}

static void assert_comparison(uint32_t index, const void *left, const void *right)
{
    assert(index < comparison_count);
    assert(comparisons[index].left == left);
    assert(comparisons[index].right == right);
}

static void test_creation_empty_and_item_semantics(void)
{
    pqueue_t q;
    qlink link;
    int item = 17;
    int key = 4;

    memset(&q, 0xa5, sizeof(q));
    assert(pqueue_create(&q, 3, compare_integer_keys) == 0);
    assert(q.array != NULL);
    assert(q.next_element == 0);
    assert(q.array_size == 3);
    assert(q.grow_size == 3);
    assert(q.keycmp == compare_integer_keys);
    assert(pqueue_peek_head(&q) == NULL);
    assert(pqueue_remove_head(&q) == NULL);
#ifdef RDP_DEAD_CODE
    assert(pqueue_get_size(&q) == 0);
#endif

    initialize_link(&link, &item, &key);
    assert(pqueue_insert(&q, &link) == 0);
    assert(link.index == 0);
    assert(pqueue_peek_head(&q) == &item);
#ifdef RDP_DEAD_CODE
    assert(pqueue_get_size(&q) == 1);
#endif
    assert(pqueue_remove_by_link(&q, &link) == &item);
    assert(link.index == 0);
    assert(q.next_element == 0);
#ifdef RDP_DEAD_CODE
    assert(pqueue_get_size(&q) == 0);
#endif

    pqueue_destroy(&q);
    assert(q.array == NULL);
}

static void test_insert_growth_and_head_removal(void)
{
    static const int expected_capacity[] = {2, 2, 4, 4, 6, 6, 8};
    static const int expected_order[] = {1, 2, 3, 5, 7, 8, 9};
    pqueue_t q;
    qlink links[7];
    int items[] = {7, 3, 9, 1, 5, 8, 2};
    int keys[] = {7, 3, 9, 1, 5, 8, 2};
    uint32_t index;

    assert(pqueue_create(&q, 2, compare_integer_keys) == 0);
    for (index = 0; index < 7; ++index)
    {
        initialize_link(&links[index], &items[index], &keys[index]);
        assert(pqueue_insert(&q, &links[index]) == 0);
        assert(q.next_element == index + 1u);
        assert(q.array_size == (uint32_t)expected_capacity[index]);
        assert_heap(&q);
#ifdef RDP_DEAD_CODE
        assert(pqueue_get_size(&q) == index + 1u);
#endif
    }

    assert(*(int *)pqueue_peek_head(&q) == expected_order[0]);
    for (index = 0; index < 7; ++index)
    {
        assert(*(int *)pqueue_remove_head(&q) == expected_order[index]);
        assert(q.next_element == 6u - index);
        assert_heap(&q);
    }
    assert(pqueue_remove_head(&q) == NULL);
    pqueue_destroy(&q);
}

static void test_siftdown_comparison_order_and_equal_children(void)
{
    pqueue_t q;
    qlink root;
    qlink left;
    qlink right;
    qlink last;
    int items[] = {100, 101, 102, 103};
    int root_key = 0;
    int left_key = 5;
    int right_key = 5;
    int last_key = 9;

    assert(pqueue_create(&q, 4, compare_integer_keys) == 0);
    initialize_link(&root, &items[0], &root_key);
    initialize_link(&left, &items[1], &left_key);
    initialize_link(&right, &items[2], &right_key);
    initialize_link(&last, &items[3], &last_key);
    assert(pqueue_insert(&q, &root) == 0);
    assert(pqueue_insert(&q, &left) == 0);
    assert(pqueue_insert(&q, &right) == 0);
    assert(pqueue_insert(&q, &last) == 0);
    assert(q.array[0] == &root);
    assert(q.array[1] == &left);
    assert(q.array[2] == &right);
    assert(q.array[3] == &last);

    comparison_count = 0;
    record_comparisons = 1;
    assert(pqueue_remove_head(&q) == &items[0]);
    record_comparisons = 0;

    assert(comparison_count == 3);
    assert_comparison(0, &last_key, &left_key);
    assert_comparison(1, &last_key, &right_key);
    assert_comparison(2, &left_key, &right_key);
    assert(q.array[0] == &right);
    assert(q.array[1] == &left);
    assert(q.array[2] == &last);
    assert_heap(&q);

    assert(pqueue_remove_head(&q) == &items[2]);
    assert(pqueue_remove_head(&q) == &items[1]);
    assert(pqueue_remove_head(&q) == &items[3]);
    pqueue_destroy(&q);
}

static void test_siftdown_no_right_child_comparison_order(void)
{
    pqueue_t q;
    qlink root;
    qlink left;
    qlink last;
    int items[] = {200, 201, 202};
    int root_key = 0;
    int left_key = 5;
    int last_key = 9;

    assert(pqueue_create(&q, 3, compare_integer_keys) == 0);
    initialize_link(&root, &items[0], &root_key);
    initialize_link(&left, &items[1], &left_key);
    initialize_link(&last, &items[2], &last_key);
    assert(pqueue_insert(&q, &root) == 0);
    assert(pqueue_insert(&q, &left) == 0);
    assert(pqueue_insert(&q, &last) == 0);

    comparison_count = 0;
    record_comparisons = 1;
    assert(pqueue_remove_head(&q) == &items[0]);
    record_comparisons = 0;

    assert(comparison_count == 1);
    assert_comparison(0, &last_key, &left_key);
    assert(q.array[0] == &left);
    assert(q.array[1] == &last);
    assert_heap(&q);

    assert(pqueue_remove_head(&q) == &items[1]);
    assert(pqueue_remove_head(&q) == &items[2]);
    pqueue_destroy(&q);
}

static void test_remove_by_link_siftdown_and_last_slot(void)
{
    static const int expected_layout[] = {1, 4, 3, 9, 5, 6};
    static const int expected_order[] = {1, 3, 4, 5, 9};
    pqueue_t q;
    qlink links[7];
    int items[] = {1, 2, 3, 4, 5, 6, 9};
    int keys[] = {1, 2, 3, 4, 5, 6, 9};
    uint32_t index;

    assert(pqueue_create(&q, 7, compare_integer_keys) == 0);
    for (index = 0; index < 7; ++index)
    {
        initialize_link(&links[index], &items[index], &keys[index]);
        assert(pqueue_insert(&q, &links[index]) == 0);
    }

    assert(links[1].index == 1);
    assert(pqueue_remove_by_link(&q, &links[1]) == &items[1]);
    assert(links[1].index == 1);
    assert(q.next_element == 6);
    for (index = 0; index < 6; ++index)
    {
        assert(link_key(q.array[index]) == expected_layout[index]);
    }
    assert_heap(&q);

    assert(links[5].index == 5);
    assert(pqueue_remove_by_link(&q, &links[5]) == &items[5]);
    assert(links[5].index == 5);
    assert(q.next_element == 5);
    assert_heap(&q);

    for (index = 0; index < 5; ++index)
    {
        assert(*(int *)pqueue_remove_head(&q) == expected_order[index]);
    }
    pqueue_destroy(&q);
}

static void test_remove_by_link_siftup(void)
{
    static const int expected_layout[] = {1, 4, 2, 5, 6, 3};
    pqueue_t q;
    qlink links[7];
    int items[] = {1, 5, 2, 9, 6, 3, 4};
    int keys[] = {1, 5, 2, 9, 6, 3, 4};
    uint32_t index;

    assert(pqueue_create(&q, 7, compare_integer_keys) == 0);
    for (index = 0; index < 7; ++index)
    {
        initialize_link(&links[index], &items[index], &keys[index]);
        assert(pqueue_insert(&q, &links[index]) == 0);
    }

    assert(links[3].index == 3);
    assert(pqueue_remove_by_link(&q, &links[3]) == &items[3]);
    assert(links[3].index == 3);
    assert(q.next_element == 6);
    for (index = 0; index < 6; ++index)
    {
        assert(link_key(q.array[index]) == expected_layout[index]);
    }
    assert_heap(&q);

    while (pqueue_remove_head(&q))
    {
    }
    pqueue_destroy(&q);
}

static void test_resort_by_link_in_both_directions(void)
{
    static const int after_siftup[] = {5, 10, 30, 40, 20, 60, 70};
    static const int after_siftdown[] = {10, 20, 30, 40, 65, 60, 70};
    static const int expected_items[] = {10, 20, 30, 40, 60, 50, 70};
    pqueue_t q;
    qlink links[7];
    int items[] = {10, 20, 30, 40, 50, 60, 70};
    int keys[] = {10, 20, 30, 40, 50, 60, 70};
    uint32_t index;

    assert(pqueue_create(&q, 7, compare_integer_keys) == 0);
    for (index = 0; index < 7; ++index)
    {
        initialize_link(&links[index], &items[index], &keys[index]);
        assert(pqueue_insert(&q, &links[index]) == 0);
    }

    keys[4] = 5;
    pqueue_resort_by_link(&q, &links[4]);
    assert(links[4].index == 0);
    assert(pqueue_peek_head(&q) == &items[4]);
    for (index = 0; index < 7; ++index)
    {
        assert(link_key(q.array[index]) == after_siftup[index]);
    }
    assert_heap(&q);

    keys[4] = 65;
    pqueue_resort_by_link(&q, &links[4]);
    assert(links[4].index == 4);
    assert(pqueue_peek_head(&q) == &items[0]);
    for (index = 0; index < 7; ++index)
    {
        assert(link_key(q.array[index]) == after_siftdown[index]);
    }
    assert_heap(&q);

    for (index = 0; index < 7; ++index)
    {
        assert(*(int *)pqueue_remove_head(&q) == expected_items[index]);
    }
    pqueue_destroy(&q);
}

int main(void)
{
    test_creation_empty_and_item_semantics();
    test_insert_growth_and_head_removal();
    test_siftdown_comparison_order_and_equal_children();
    test_siftdown_no_right_child_comparison_order();
    test_remove_by_link_siftdown_and_last_slot();
    test_remove_by_link_siftup();
    test_resort_by_link_in_both_directions();
    return 0;
}
