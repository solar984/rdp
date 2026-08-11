// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "container.h"

#include <stdlib.h>

static void pqueue_swap(rdp_pqueue_t *queue, uint32_t left, uint32_t right)
{
    rdp_pqueue_link_t *temporary = queue->items[left];
    queue->items[left] = queue->items[right];
    queue->items[right] = temporary;
    queue->items[left]->heap_index = left;
    queue->items[right]->heap_index = right;
}

static void pqueue_siftup(rdp_pqueue_t *queue, uint32_t index)
{
    while (index)
    {
        uint32_t parent = (index - 1u) >> 1;
        if (queue->compare(queue->items[index]->key, queue->items[parent]->key) >= 0)
        {
            break;
        }

        pqueue_swap(queue, index, parent);
        index = parent;
    }
}

int pqueue_create(rdp_pqueue_t *queue, uint32_t initial_capacity, rdp_container_compare_t compare)
{
    queue->count = 0;
    queue->capacity = initial_capacity;
    queue->growth = initial_capacity;
    queue->compare = compare;
    queue->items = (rdp_pqueue_link_t **)malloc((size_t)initial_capacity * sizeof(*queue->items));
    return queue->items ? 0 : 1;
}

void pqueue_destroy(rdp_pqueue_t *queue)
{
    if (queue->items)
    {
        free(queue->items);
        queue->items = NULL;
    }
}

int pqueue_insert(rdp_pqueue_t *queue, rdp_pqueue_link_t *link)
{
    uint32_t index;

    if (queue->count == queue->capacity)
    {
        uint32_t capacity = queue->capacity + queue->growth;
        rdp_pqueue_link_t **items = (rdp_pqueue_link_t **)realloc(queue->items, (size_t)capacity * sizeof(*queue->items));
        if (!items)
        {
            return 1;
        }

        queue->items = items;
        queue->capacity = capacity;
    }

    index = queue->count;
    queue->items[index] = link;
    link->heap_index = index;
    pqueue_siftup(queue, index);
    ++queue->count;
    return 0;
}

void pqueue_siftdown(rdp_pqueue_t *queue, uint32_t index)
{
    for (;;)
    {
        uint32_t left = index * 2u + 1u;
        uint32_t right = left + 1u;
        uint32_t smaller;

        if (left >= queue->count)
        {
            return;
        }

        if (right < queue->count && queue->compare(queue->items[left]->key, queue->items[right]->key) >= 0)
        {
            smaller = right; // Equal children select the right child in all 3 clients.
        }
        else
        {
            smaller = left;
        }

        if (queue->compare(queue->items[index]->key, queue->items[smaller]->key) <= 0)
        {
            return;
        }

        pqueue_swap(queue, index, smaller);
        index = smaller;
    }
}

void pqueue_resort_by_link(rdp_pqueue_t *queue, rdp_pqueue_link_t *link)
{
    uint32_t original_index = link->heap_index;
    pqueue_siftup(queue, original_index);

    // If the changed link moved up, its displaced parent now occupies the original slot.
    pqueue_siftdown(queue, original_index);
}

void *pqueue_remove_by_link(rdp_pqueue_t *queue, rdp_pqueue_link_t *link)
{
    uint32_t index = link->heap_index;
    void *value = queue->items[index]->value;

    --queue->count;
    if (queue->count != index)
    {
        queue->items[index] = queue->items[queue->count];
        queue->items[index]->heap_index = index;
        pqueue_siftdown(queue, index);
        pqueue_siftup(queue, index);
    }

    return value;
}

void *pqueue_remove_head(rdp_pqueue_t *queue)
{
    void *value;

    if (!queue->count)
    {
        return NULL;
    }

    value = queue->items[0]->value;
    --queue->count;
    if (queue->count)
    {
        queue->items[0] = queue->items[queue->count];
        queue->items[0]->heap_index = 0;
        pqueue_siftdown(queue, 0);
    }

    return value;
}

void *pqueue_peek_head(const rdp_pqueue_t *queue)
{
    return queue->count ? queue->items[0]->value : NULL;
}
