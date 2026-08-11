// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Source form of the original connection hash using host pointers and locks.
#ifndef RDPLIB_CONNHASH_H
#define RDPLIB_CONNHASH_H

#include <stdint.h>

#include "container.h"
#include "rdplib_platform.h"

struct connection_t;

typedef struct rdp_connhash_bucket_t
{
    rdplib_platform_mutex_t lock;
    rdp_list_t connections;
} rdp_connhash_bucket_t;

typedef struct _connhash_t
{
    uint16_t bucket_count;
    rdp_connhash_bucket_t *buckets;
} rdp_connhash_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Clears the hash without allocating storage.
void connhash_init(rdp_connhash_t *hash);

// Recovered name: present in the PPC and Intel symbols.
// This compares the address family and its endpoint bytes.
// Equal unsupported families return an uninitialized value
// in the source faithful build.
//
// rdplib deviation: endpoint uses the Windows or POSIX 2 byte sockaddr
// family.  The Mac source used its native sa_len layout.
int sockaddr_cmp(const void *left, const void *right);

// Recovered name: present in the PPC symbols.  Intel inlines the same code and
// the stripped Windows function matches it.  The hash uses native sockaddr
// fields.  bucket_count must be a nonzero power of 2.
uint16_t connhash_hash(rdp_connhash_t *hash, const uint8_t endpoint[16]);

// Allocate 2^(hash_bits - 1) buckets.  The default build checks that hash_bits
// is in [1, 16] and checks the allocation.  RDPLIB_SOURCE_FAITHFUL keeps the
// recovered allocation order.
int connhash_create(rdp_connhash_t *hash, uint16_t hash_bits);

// Destroy the bucket locks and storage.  All connections must already be
// removed because the intrusive lists do not free their values.
void connhash_destroy(rdp_connhash_t *hash);

// Return a locked connection with a temporary reference.  Release both with
// rdp_unlock or the matching unlock and subref calls.  endpoint must contain a
// complete platform sockaddr.
struct connection_t *connhash_lock(rdp_connhash_t *hash, const uint8_t endpoint[16]);

// Insert a unique endpoint and create its hash reference.  Inserting a
// duplicate is unchecked and breaks the removal logic.
void connhash_insert(rdp_connhash_t *hash, struct connection_t *connection);

// Drop a reference.  When it reaches 0, remove the hash entry and return
// the connection for destruction.  Otherwise return NULL.  The caller must
// supply a live member and own the reference being dropped.
struct connection_t *connhash_subref(rdp_connhash_t *hash, struct connection_t *connection);

#ifdef __cplusplus
}
#endif

#endif /* RDPLIB_CONNHASH_H */
