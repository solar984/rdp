// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "connhash.h"

#include <string.h>

#include "connection.h"

enum
{
    RDP_AF_INET = 2,
    RDP_AF_FAMILY6 = 6,
    RDP_AF_LEGACY_45 = 0x45
};

void connhash_init(rdp_connhash_t *hash)
{
    hash->bucket_count = 0;
    hash->buckets = 0;
}

// Recovered name: sockaddr_cmp survives in the PPC and Intel symbols.
// rdplib deviation: this standalone build uses the native Windows/POSIX 2 byte sockaddr family layout; the Mac source used its native sa_len layout.
int sockaddr_cmp(const void *left_pointer, const void *right_pointer)
{
    const uint8_t *left = (const uint8_t *)left_pointer;
    const uint8_t *right = (const uint8_t *)right_pointer;
    uint16_t left_family;
    uint16_t right_family;
#ifdef RDPLIB_SOURCE_FAITHFUL
    int result;
#endif

    memcpy(&left_family, left, sizeof(left_family));
    memcpy(&right_family, right, sizeof(right_family));
    if (left_family != right_family)
    {
        return left_family < right_family ? -1 : 1;
    }

    switch (left_family)
    {
    case RDP_AF_INET:
        return memcmp(left, right, 8);
    case RDP_AF_FAMILY6:
        return memcmp(left, right, 14);
    case RDP_AF_LEGACY_45:
        return memcmp(left, right, 4);
    default:
#ifdef RDPLIB_SOURCE_FAITHFUL
        // The source returns an uninitialized local for equal unknown families.
        return result;
#else
        return 0;
#endif
    }
}

// Recovered name: connhash_hash survives in the PPC symbols.
uint16_t connhash_hash(rdp_connhash_t *hash, const uint8_t endpoint[16])
{
    uint16_t family;
    uint16_t port;
    uint16_t final_word;
    uint32_t address;
    uint32_t index;

    memcpy(&family, endpoint, sizeof(family));
    if (family == RDP_AF_INET)
    {
        uint32_t mixed;

        memcpy(&port, endpoint + 2, sizeof(port));
        memcpy(&address, endpoint + 4, sizeof(address));
        mixed = port ^ address ^ (address >> 12) ^ (address >> 24);
        return (uint16_t)(mixed & (uint16_t)(hash->bucket_count - 1));
    }

    if (family == RDP_AF_FAMILY6)
    {
        int32_t mixed;

        memcpy(&final_word, endpoint + 12, sizeof(final_word));
        mixed = final_word;
        for (index = 2; index < 12; ++index)
        {
            mixed += (int8_t)endpoint[index];
        }
        return (uint16_t)((uint16_t)mixed & (uint16_t)(hash->bucket_count - 1));
    }

    return 0;
}

int connhash_create(rdp_connhash_t *hash, uint16_t hash_bits)
{
    uint32_t index;
    size_t allocation_bytes;

#ifdef RDPLIB_SOURCE_FAITHFUL
    hash->bucket_count = (uint16_t)(UINT32_C(1) << (hash_bits - 1u));
    allocation_bytes = (size_t)hash->bucket_count * sizeof(*hash->buckets);
    hash->buckets = (rdp_connhash_bucket_t *)rdplib_platform_malloc(allocation_bytes);

    // This intentionally precedes the null check. All 3 clients do the
    // same, so allocation failure faults instead of returning cleanly.
    memset(hash->buckets, 0, allocation_bytes);

    if (hash->buckets)
    {
        for (index = 0; index < hash->bucket_count; ++index)
        {
            list_init(&hash->buckets[index].connections);
            list_create(&hash->buckets[index].connections, 1, sockaddr_cmp);
            rdplib_platform_mutex_init(&hash->buckets[index].lock);
        }
    }

    return hash->buckets == NULL;
#else
    uint16_t bucket_count;
    rdp_connhash_bucket_t *buckets;

    if (!hash || hash_bits < 1 || hash_bits > 16)
    {
        return 1;
    }

    bucket_count = (uint16_t)(UINT32_C(1) << (hash_bits - 1u));
    allocation_bytes = (size_t)bucket_count * sizeof(*buckets);
    buckets = (rdp_connhash_bucket_t *)rdplib_platform_malloc(allocation_bytes);
    if (!buckets)
    {
        return 1;
    }

    memset(buckets, 0, allocation_bytes);
    for (index = 0; index < bucket_count; ++index)
    {
        list_init(&buckets[index].connections);
        list_create(&buckets[index].connections, 1, sockaddr_cmp);
        rdplib_platform_mutex_init(&buckets[index].lock);
    }
    hash->bucket_count = bucket_count;
    hash->buckets = buckets;
    return 0;
#endif
}

void connhash_destroy(rdp_connhash_t *hash)
{
    uint32_t index;

    if (!hash->buckets)
    {
        return;
    }

    for (index = 0; index < hash->bucket_count; ++index)
    {
        list_destroy(&hash->buckets[index].connections);
        rdplib_platform_mutex_destroy(&hash->buckets[index].lock);
    }

    rdplib_platform_free(hash->buckets);
    hash->buckets = NULL;
}

connection_t *connhash_lock(rdp_connhash_t *hash, const uint8_t endpoint[16])
{
    rdp_connhash_bucket_t *bucket = &hash->buckets[connhash_hash(hash, endpoint)];
    rdp_list_link_t *link;
    connection_t *connection = NULL;

    rdplib_platform_mutex_lock(&bucket->lock);
    link = list_find_link_by_key(&bucket->connections, endpoint);
    if (link)
    {
        connection = (connection_t *)link->value;
        if (connection)
        {
            ++connection->reference_count;
        }
    }
    rdplib_platform_mutex_unlock(&bucket->lock);

    if (connection)
    {
        rdplib_platform_mutex_lock(&connection->lock);
    }

    return connection;
}

void connhash_insert(rdp_connhash_t *hash, connection_t *connection)
{
    rdp_connhash_bucket_t *bucket = &hash->buckets[connhash_hash(hash, connection->transmit.remote_address)];

    rdplib_platform_mutex_lock(&bucket->lock);
    connection->reference_count = 1;
    list_insert(&bucket->connections, &connection->connection_hash_link);
    rdplib_platform_mutex_unlock(&bucket->lock);
}

connection_t *connhash_subref(rdp_connhash_t *hash, connection_t *connection)
{
    rdp_connhash_bucket_t *bucket = &hash->buckets[connhash_hash(hash, connection->transmit.remote_address)];
    connection_t *removed = NULL;

    rdplib_platform_mutex_lock(&bucket->lock);
    --connection->reference_count;
    if (connection->reference_count == 0)
    {
        rdp_list_link_t *link = list_find_link_by_key(&bucket->connections, connection->transmit.remote_address);
        if (link)
        {
            removed = (connection_t *)list_remove_by_link(&bucket->connections, link);
        }
    }
    rdplib_platform_mutex_unlock(&bucket->lock);

    return removed;
}
