// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "container.h"

#include <stddef.h>

void list_init(rdp_list_t *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->compare = NULL;
    list->sorted = 0;
}

void list_create(rdp_list_t *list, int sorted, rdp_container_compare_t compare)
{
    list->sorted = sorted;
    list->compare = compare;
}

void list_destroy(rdp_list_t *list)
{
    (void)list;
}

void list_add_head(rdp_list_t *list, rdp_list_link_t *link)
{
    link->next = list->head;
    link->previous = NULL;
    list->head = link;

    if (!list->tail)
    {
        list->tail = link;
    }
    else
    {
        link->next->previous = link;
    }

    ++list->count;
}

void list_add_tail(rdp_list_t *list, rdp_list_link_t *link)
{
    link->next = NULL;
    link->previous = list->tail;
    list->tail = link;

    if (!list->head)
    {
        list->head = link;
    }
    else
    {
        link->previous->next = link;
    }

    ++list->count;
}

void *list_remove_head(rdp_list_t *list)
{
    rdp_list_link_t *link = list->head;
    void *value;

    if (!link)
    {
        return NULL;
    }

    value = link->value;
    list->head = link->next;
    if (list->head)
    {
        list->head->previous = NULL;
    }
    else
    {
        list->tail = NULL;
    }

    --list->count;
    return value;
}

static void *list_remove_tail(rdp_list_t *list)
{
    rdp_list_link_t *link = list->tail;
    void *value;

    if (!link)
    {
        return NULL;
    }

    value = link->value;
    list->tail = link->previous;
    if (list->tail)
    {
        list->tail->next = NULL;
    }
    else
    {
        list->head = NULL;
    }

    --list->count;
    return value;
}

void list_insert(rdp_list_t *list, rdp_list_link_t *link)
{
    rdp_list_link_t *current;

    // The original routine assumes compare is valid and does not inspect sorted.
    if (!list->head || list->compare(list->head->key, link->key) > 0)
    {
        list_add_head(list, link);
        return;
    }

    if (list->compare(list->tail->key, link->key) <= 0)
    {
        list_add_tail(list, link);
        return;
    }

    current = list->head->next;
    while (list->compare(current->key, link->key) <= 0)
    {
        current = current->next;
    }

    link->next = current;
    link->previous = current->previous;
    current->previous = link;
    link->previous->next = link;
    ++list->count;
}

rdp_list_link_t *list_find_link_by_key(rdp_list_t *list, const void *key)
{
    rdp_list_link_t *link = list->head;

    while (link)
    {
        int comparison = list->compare(link->key, key);
        if (comparison == 0)
        {
            break;
        }
        if (list->sorted && comparison > 0)
        {
            link = NULL;
            break;
        }
        link = link->next;
    }

    return link;
}

void *list_remove_by_link(rdp_list_t *list, rdp_list_link_t *link)
{
    if (!link->previous)
    {
        // The client removes the current head rather than proving link is that head.
        return list_remove_head(list);
    }

    if (!link->next)
    {
        // The corresponding tail path also assumes that the item belongs to this list.
        return list_remove_tail(list);
    }

    link->previous->next = link->next;
    link->next->previous = link->previous;
    --list->count;
    return link->value;
}
