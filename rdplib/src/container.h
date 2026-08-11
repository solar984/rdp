// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Intrusive list and priority queue containers used throughout RDP.
//
// _pqueue_t is the original type name proven by PPC mangling. The list type
// names remain descriptive because no retained symbol exposes their original
// tags. These are source level records with host pointers, not client ABI
// overlays. Their unchecked membership contracts match the clients.
#ifndef RDP_CONTAINER_H
#define RDP_CONTAINER_H

#include <stdint.h>

typedef int (*rdp_container_compare_t)(const void *left, const void *right);

typedef struct rdp_list_link_t
{
    struct rdp_list_link_t *next;     // Removal does not clear either membership pointer.
    struct rdp_list_link_t *previous; // Callers must supply a live member link when removing.
    void *value;                      // Object that owns this intrusive link.
    const void *key;                  // Value passed to the optional comparator.
} rdp_list_link_t;

typedef struct rdp_list_t
{
    rdp_list_link_t *head;
    rdp_list_link_t *tail;
    uint32_t count;
    rdp_container_compare_t compare;
    int sorted; // Allows lookup to stop after passing the requested key.
} rdp_list_t;

typedef struct rdp_pqueue_link_t
{
    uint32_t heap_index; // Trusted by removal and left stale after removal.
    void *value;
    const void *key;
} rdp_pqueue_link_t;

typedef struct _pqueue_t
{
    rdp_pqueue_link_t **items;
    uint32_t count;
    uint32_t capacity;
    uint32_t growth; // Fixed slot increment; initialized to the starting capacity.
    rdp_container_compare_t compare;
} rdp_pqueue_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Wrap aware sequence key comparators used by the sorted receive lists.
int uint8_cmp(const void *left, const void *right);
int uint16_cmp(const void *left, const void *right);

void list_init(rdp_list_t *list);

// Sets only the sorted latch and comparator. The source requires list_init or zeroed storage first.
void list_create(rdp_list_t *list, int sorted, rdp_container_compare_t compare);

// Intentionally does nothing; the intrusive list never owns its linked values.
void list_destroy(rdp_list_t *list);
void list_add_head(rdp_list_t *list, rdp_list_link_t *link);
void list_add_tail(rdp_list_t *list, rdp_list_link_t *link);
void *list_remove_head(rdp_list_t *list);
void list_insert(rdp_list_t *list, rdp_list_link_t *link);
rdp_list_link_t *list_find_link_by_key(rdp_list_t *list, const void *key);

// Removes the supplied live member and returns its value. Membership is not validated.
void *list_remove_by_link(rdp_list_t *list, rdp_list_link_t *link);

// pqueue is a min-heap structure
int pqueue_create(rdp_pqueue_t *queue, uint32_t initial_capacity, rdp_container_compare_t compare);
void pqueue_destroy(rdp_pqueue_t *queue);
int pqueue_insert(rdp_pqueue_t *queue, rdp_pqueue_link_t *link);
void pqueue_siftdown(rdp_pqueue_t *queue, uint32_t index);
void pqueue_resort_by_link(rdp_pqueue_t *queue, rdp_pqueue_link_t *link);

// Removes the supplied live member and returns its value. The stored heap index is trusted.
void *pqueue_remove_by_link(rdp_pqueue_t *queue, rdp_pqueue_link_t *link);
void *pqueue_remove_head(rdp_pqueue_t *queue);
void *pqueue_peek_head(const rdp_pqueue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* RDP_CONTAINER_H */
