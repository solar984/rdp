// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "list.h"

_Static_assert(offsetof(rdp_link_t, next) == 0, "_link.next layout");
_Static_assert(offsetof(rdp_link_t, prev) == sizeof(void *), "_link.prev layout");
_Static_assert(offsetof(rdp_link_t, item) == 2 * sizeof(void *), "_link.item layout");
_Static_assert(offsetof(rdp_link_t, key) == 3 * sizeof(void *), "_link.key layout");
_Static_assert(sizeof(rdp_link_t) == 4 * sizeof(void *), "_link size");
_Static_assert(sizeof(struct _link) == sizeof(rdp_link_t), "_link tag and typedef");

_Static_assert(offsetof(list_t, head) == 0, "_list_t.head layout");
_Static_assert(offsetof(list_t, tail) == sizeof(void *), "_list_t.tail layout");
_Static_assert(offsetof(list_t, size) == 2 * sizeof(void *), "_list_t.size layout");
_Static_assert(offsetof(list_t, keycmp) == 3 * sizeof(void *), "_list_t.keycmp layout");
_Static_assert(offsetof(list_t, sorted) == 4 * sizeof(void *), "_list_t.sorted layout");
_Static_assert(sizeof(list_t) == 5 * sizeof(void *), "_list_t size");
_Static_assert(sizeof(struct _list_t) == sizeof(list_t), "_list_t tag and typedef");

static const void *expected_left;
static const void *expected_right;
static uint32_t comparison_count;

static int compare_integer_keys(const void *left, const void *right)
{
    int left_value = *(const int *)left;
    int right_value = *(const int *)right;

    if (expected_left)
    {
        assert(left == expected_left);
        assert(right == expected_right);
        expected_left = NULL;
        expected_right = NULL;
    }

    ++comparison_count;
    return (left_value > right_value) - (left_value < right_value);
}

static void initialize_link(rdp_link_t *link, void *item, void *key)
{
    memset(link, 0, sizeof(*link));
    link->item = item;
    link->key.p = key;
}

static void test_layout_and_initialization(void)
{
    list_t list;
    rdp_link_t link;
    int item = 7;
    int key = 19;

    memset(&list, 0xa5, sizeof(list));
    list_init(&list);
    assert(list.head == NULL);
    assert(list.tail == NULL);
    assert(list.size == 0);
    assert(list.keycmp == NULL);
    assert(list.sorted == 0);

    initialize_link(&link, &item, &key);
    list_add_tail(&list, &link);
    list_create(&list, 1, compare_integer_keys);
    assert(list.head == &link);
    assert(list.tail == &link);
    assert(list.size == 1);
    assert(list.keycmp == compare_integer_keys);
    assert(list.sorted == 1);

    assert(list_remove_head(&list) == &item);
    list_destroy(&list);
}

static void test_empty_and_single_item_operations(void)
{
    list_t list;
    rdp_link_t link;
    int item = 11;
    int key = 13;

    list_init(&list);
    list_create(&list, 0, compare_integer_keys);
    assert(list_get_size(&list) == 0);
    assert(list_peek_head(&list) == NULL);
    assert(list_remove_head(&list) == NULL);
    assert(list_remove_tail(&list) == NULL);
    assert(list_find_link_by_key(&list, &key) == NULL);
    assert(list_lookup(&list, &key) == NULL);
    assert(list_remove_by_key(&list, &key) == NULL);

    initialize_link(&link, &item, &key);
    list_add_head(&list, &link);
    assert(list.head == &link);
    assert(list.tail == &link);
    assert(list.size == 1);
    assert(link.next == NULL);
    assert(link.prev == NULL);
    assert(list_peek_head(&list) == &item);
    assert(list_remove_tail(&list) == &item);
    assert(list.head == NULL);
    assert(list.tail == NULL);
    assert(list.size == 0);

    list_add_tail(&list, &link);
    assert(list_remove_head(&list) == &item);
    assert(list.head == NULL);
    assert(list.tail == NULL);
    assert(list.size == 0);
    list_destroy(&list);
}

static void test_head_tail_and_middle_removal(void)
{
    list_t list;
    rdp_link_t first;
    rdp_link_t second;
    rdp_link_t third;
    rdp_link_t fourth;
    int first_item = 1;
    int second_item = 2;
    int third_item = 3;
    int fourth_item = 4;

    list_init(&list);
    list_create(&list, 0, NULL);
    initialize_link(&first, &first_item, NULL);
    initialize_link(&second, &second_item, NULL);
    initialize_link(&third, &third_item, NULL);
    initialize_link(&fourth, &fourth_item, NULL);

    list_add_tail(&list, &second);
    list_add_head(&list, &first);
    list_add_tail(&list, &third);
    list_add_tail(&list, &fourth);

    assert(list.size == 4);
    assert(list.head == &first);
    assert(list.tail == &fourth);
    assert(first.prev == NULL && first.next == &second);
    assert(second.prev == &first && second.next == &third);
    assert(third.prev == &second && third.next == &fourth);
    assert(fourth.prev == &third && fourth.next == NULL);

    assert(list_remove_by_link(&list, &second) == &second_item);
    assert(list.size == 3);
    assert(first.next == &third);
    assert(third.prev == &first);
    assert(second.prev == &first && second.next == &third);

    assert(list_remove_by_link(&list, &first) == &first_item);
    assert(list.head == &third);
    assert(third.prev == NULL);
    assert(first.next == &third);

    assert(list_remove_by_link(&list, &fourth) == &fourth_item);
    assert(list.tail == &third);
    assert(third.next == NULL);
    assert(fourth.prev == &third);

    assert(list_remove_by_link(&list, &third) == &third_item);
    assert(list.head == NULL);
    assert(list.tail == NULL);
    assert(list.size == 0);
    list_destroy(&list);
}

static void test_sorted_insertion_and_duplicates(void)
{
    list_t list;
    rdp_link_t ten;
    rdp_link_t fifteen;
    rdp_link_t first_twenty;
    rdp_link_t second_twenty;
    rdp_link_t thirty;
    int ten_item = 10;
    int fifteen_item = 15;
    int first_twenty_item = 20;
    int second_twenty_item = 21;
    int thirty_item = 30;
    int ten_key = 10;
    int fifteen_key = 15;
    int first_twenty_key = 20;
    int second_twenty_key = 20;
    int thirty_key = 30;

    list_init(&list);
    list_create(&list, 1, compare_integer_keys);
    initialize_link(&first_twenty, &first_twenty_item, &first_twenty_key);
    initialize_link(&ten, &ten_item, &ten_key);
    initialize_link(&second_twenty, &second_twenty_item, &second_twenty_key);
    initialize_link(&fifteen, &fifteen_item, &fifteen_key);
    initialize_link(&thirty, &thirty_item, &thirty_key);

    list_insert(&list, &first_twenty);
    list_insert(&list, &ten);
    list_insert(&list, &second_twenty);
    list_insert(&list, &fifteen);
    list_insert(&list, &thirty);

    assert(list.size == 5);
    assert(list.head == &ten);
    assert(list.tail == &thirty);
    assert(ten.prev == NULL && ten.next == &fifteen);
    assert(fifteen.prev == &ten && fifteen.next == &first_twenty);
    assert(first_twenty.prev == &fifteen && first_twenty.next == &second_twenty);
    assert(second_twenty.prev == &first_twenty && second_twenty.next == &thirty);
    assert(thirty.prev == &second_twenty && thirty.next == NULL);

    assert(list_find_link_by_key(&list, &first_twenty_key) == &first_twenty);
    assert(list_remove_head(&list) == &ten_item);
    assert(list_remove_head(&list) == &fifteen_item);
    assert(list_remove_head(&list) == &first_twenty_item);
    assert(list_remove_head(&list) == &second_twenty_item);
    assert(list_remove_head(&list) == &thirty_item);
    list_destroy(&list);
}

static void test_lookup_order_and_early_stop(void)
{
    list_t list;
    rdp_link_t ten;
    rdp_link_t twenty;
    rdp_link_t thirty;
    int ten_item = 10;
    int twenty_item = 20;
    int thirty_item = 30;
    int ten_key = 10;
    int twenty_key = 20;
    int thirty_key = 30;
    int missing_key = 15;

    list_init(&list);
    list_create(&list, 1, compare_integer_keys);
    initialize_link(&ten, &ten_item, &ten_key);
    initialize_link(&twenty, &twenty_item, &twenty_key);
    initialize_link(&thirty, &thirty_item, &thirty_key);
    list_add_tail(&list, &ten);
    list_add_tail(&list, &twenty);
    list_add_tail(&list, &thirty);

    comparison_count = 0;
    expected_left = &ten_key;
    expected_right = &missing_key;
    assert(list_find_link_by_key(&list, &missing_key) == NULL);
    assert(expected_left == NULL);
    assert(comparison_count == 2);

    list.sorted = 0;
    comparison_count = 0;
    expected_left = &ten_key;
    expected_right = &missing_key;
    assert(list_find_link_by_key(&list, &missing_key) == NULL);
    assert(expected_left == NULL);
    assert(comparison_count == 3);

    assert(list_remove_head(&list) == &ten_item);
    assert(list_remove_head(&list) == &twenty_item);
    assert(list_remove_head(&list) == &thirty_item);
    list_destroy(&list);
}

static void test_header_lookup_helpers(void)
{
    list_t list;
    rdp_link_t first;
    rdp_link_t second;
    rdp_link_t third;
    int first_item = 10;
    int second_item = 20;
    int third_item = 30;
    int first_key = 1;
    int second_key = 2;
    int third_key = 2;
    int missing_key = 4;

    list_init(&list);
    list_create(&list, 1, compare_integer_keys);
    initialize_link(&first, &first_item, &first_key);
    initialize_link(&second, &second_item, &second_key);
    initialize_link(&third, &third_item, &third_key);
    list_insert(&list, &first);
    list_insert(&list, &second);
    list_insert(&list, &third);

    assert(list_get_size(&list) == 3);
    assert(list_peek_head(&list) == &first_item);
    assert(list_lookup(&list, &second_key) == &second_item);
    assert(list_lookup(&list, &missing_key) == NULL);
    assert(list_remove_by_key(&list, &second_key) == &second_item);
    assert(list_get_size(&list) == 2);
    assert(first.next == &third);
    assert(third.prev == &first);
    assert(list_remove_by_key(&list, &second_key) == &third_item);
    assert(list_remove_by_key(&list, &second_key) == NULL);
    assert(list_remove_by_key(&list, &first_key) == &first_item);
    assert(list_get_size(&list) == 0);
    list_destroy(&list);
}

int main(void)
{
    test_layout_and_initialization();
    test_empty_and_single_item_operations();
    test_head_tail_and_middle_removal();
    test_sorted_insertion_and_duplicates();
    test_lookup_order_and_early_stop();
    test_header_lookup_helpers();
    return 0;
}
