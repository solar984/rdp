// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "connection.h"
#include "connhash.h"

enum
{
    TEST_AF_INET = 2,
    TEST_AF_IPX = 6,
    TEST_AF_COMPORT = 69
};

_Static_assert(_Generic(&connhash_init, void (*)(connhash_t *): 1, default: 0), "connhash_init signature");
_Static_assert(_Generic(&sockaddr_cmp, int (*)(const void *, const void *): 1, default: 0), "sockaddr_cmp signature");
_Static_assert(_Generic(&connhash_create, uint32_t (*)(connhash_t *, uint16_t): 1, default: 0), "connhash_create signature");
_Static_assert(_Generic(&connhash_destroy, void (*)(connhash_t *): 1, default: 0), "connhash_destroy signature");
_Static_assert(_Generic(&connhash_lock, connection_t *(*)(connhash_t *, struct sockaddr *): 1, default: 0), "connhash_lock signature");
_Static_assert(_Generic(&connhash_insert, void (*)(connhash_t *, connection_t *): 1, default: 0), "connhash_insert signature");
_Static_assert(_Generic(&connhash_subref, connection_t *(*)(connhash_t *, connection_t *): 1, default: 0), "connhash_subref signature");
_Static_assert(_Generic(&connhash_unlock, connection_t *(*)(connhash_t *, connection_t *): 1, default: 0), "connhash_unlock signature");

#if defined(_WIN32) && !defined(_WIN64)
_Static_assert(offsetof(hashbin_t, list) == 0x18 + RDP_WIN32_UMUTEX_OWNER_BYTES, "hashbin_t::list x86 offset");
_Static_assert(sizeof(hashbin_t) == 0x2C + RDP_WIN32_UMUTEX_OWNER_BYTES, "hashbin_t x86 size");
_Static_assert(offsetof(connhash_t, table) == 0x04, "connhash_t::table x86 offset");
_Static_assert(sizeof(connhash_t) == 0x08, "connhash_t x86 size");
#endif

static struct sockaddr *as_sockaddr(uint8_t endpoint[16])
{
    return (struct sockaddr *)(void *)endpoint;
}

static void store_native_u16(uint8_t *destination, uint16_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static void store_native_u32(uint8_t *destination, uint32_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static void make_ipv4_endpoint(uint8_t endpoint[16], uint16_t port, uint32_t address)
{
    memset(endpoint, 0, 16);
    store_native_u16(endpoint, TEST_AF_INET);
    store_native_u16(endpoint + 2, port);
    store_native_u32(endpoint + 4, address);
}

static uint16_t expected_ipv4_bin(const connhash_t *ch, uint16_t port, uint32_t address)
{
    uint32_t sum = port ^ address ^ (address >> 12) ^ (address >> 24);

    return (uint16_t)(sum & (uint16_t)(ch->table_size - 1));
}

static uint16_t expected_ipx_bin(const connhash_t *ch, const uint8_t endpoint[16])
{
    uint16_t final_word;
    int32_t sum;
    uint32_t index;

    memcpy(&final_word, endpoint + 12, sizeof(final_word));
    sum = final_word;
    for (index = 2; index < 12; ++index)
    {
        sum += (int8_t)endpoint[index];
    }
    return (uint16_t)((uint16_t)sum & (uint16_t)(ch->table_size - 1));
}

static void initialize_connection(connection_t *connection, const uint8_t endpoint[16])
{
    memset(connection, 0, sizeof(*connection));
    memcpy(&connection->tx_remote_addr, endpoint, 16);
    connection->cn_addr_map_link.item = connection;
    connection->cn_addr_map_link.key.p = &connection->tx_remote_addr;
    umutex_create(&connection->cn_lock);
}

static void destroy_connection(connection_t *connection)
{
    umutex_destroy(&connection->cn_lock);
}

static void test_sockaddr_comparison(void)
{
    uint8_t left[16] = {0};
    uint8_t right[16] = {0};

    make_ipv4_endpoint(left, UINT16_C(0x1234), UINT32_C(0x89ABCDEF));
    memcpy(right, left, sizeof(right));
    assert(sockaddr_cmp(left, right) == 0);

    right[15] = 0xFF;
    assert(sockaddr_cmp(left, right) == 0);
    right[4] ^= 1;
    assert(sockaddr_cmp(left, right) != 0);

    memset(left, 0, sizeof(left));
    store_native_u16(left, TEST_AF_IPX);
    for (uint32_t index = 2; index < 14; ++index)
    {
        left[index] = (uint8_t)(index * 9u);
    }
    memcpy(right, left, sizeof(right));
    right[15] = 1;
    assert(sockaddr_cmp(left, right) == 0);
    right[10] ^= 1;
    assert(sockaddr_cmp(left, right) != 0);

    memset(left, 0, sizeof(left));
    store_native_u16(left, TEST_AF_COMPORT);
    left[2] = 7;
    left[3] = 9;
    memcpy(right, left, sizeof(right));
    right[10] = 1;
    assert(sockaddr_cmp(left, right) == 0);
    right[3] ^= 1;
    assert(sockaddr_cmp(left, right) != 0);

    store_native_u16(left, TEST_AF_INET);
    store_native_u16(right, TEST_AF_IPX);
    assert(sockaddr_cmp(left, right) < 0);
    assert(sockaddr_cmp(right, left) > 0);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    store_native_u16(left, UINT16_C(123));
    store_native_u16(right, UINT16_C(123));
    right[15] = 1;
    assert(sockaddr_cmp(left, right) < 0);
    assert(sockaddr_cmp(right, left) > 0);
#endif
}

static void test_create_and_ipv4_reference_lifecycle(void)
{
    const uint16_t first_port = UINT16_C(0x1234);
    const uint16_t colliding_port = UINT16_C(0x1230);
    const uint16_t other_port = UINT16_C(0x1235);
    const uint32_t address = UINT32_C(0x89ABCDEF);
    connhash_t ch;
    connection_t first;
    connection_t colliding;
    connection_t other;
    connection_t *locked;
    uint8_t first_endpoint[16];
    uint8_t colliding_endpoint[16];
    uint8_t other_endpoint[16];
    uint8_t missing_endpoint[16];
    uint16_t collision_bin;
    uint16_t other_bin;
    uint16_t bin;

    connhash_init(&ch);
    assert(ch.table_size == 0);
    assert(ch.table == NULL);
    assert(connhash_create(&ch, 3) == 0);
    assert(ch.table_size == 4);
    assert(ch.table != NULL);
    for (bin = 0; bin < ch.table_size; ++bin)
    {
        assert(ch.table[bin].list.head == NULL);
        assert(ch.table[bin].list.tail == NULL);
        assert(ch.table[bin].list.size == 0);
        assert(ch.table[bin].list.sorted == 1);
        assert(ch.table[bin].list.keycmp == sockaddr_cmp);
    }

    make_ipv4_endpoint(first_endpoint, first_port, address);
    make_ipv4_endpoint(colliding_endpoint, colliding_port, address);
    make_ipv4_endpoint(other_endpoint, other_port, address);
    make_ipv4_endpoint(missing_endpoint, UINT16_C(0x1238), address);
    collision_bin = expected_ipv4_bin(&ch, first_port, address);
    other_bin = expected_ipv4_bin(&ch, other_port, address);
    assert(collision_bin == expected_ipv4_bin(&ch, colliding_port, address));
    assert(collision_bin == expected_ipv4_bin(&ch, UINT16_C(0x1238), address));
    assert(collision_bin != other_bin);

    initialize_connection(&first, first_endpoint);
    initialize_connection(&colliding, colliding_endpoint);
    initialize_connection(&other, other_endpoint);

    connhash_insert(&ch, &first);
    connhash_insert(&ch, &colliding);
    connhash_insert(&ch, &other);
    assert(first.cn_ref_count == 1);
    assert(colliding.cn_ref_count == 1);
    assert(other.cn_ref_count == 1);
    assert(ch.table[collision_bin].list.size == 2);
    assert(ch.table[collision_bin].list.head->item == &colliding);
    assert(ch.table[collision_bin].list.tail->item == &first);
    assert(ch.table[other_bin].list.size == 1);
    assert(ch.table[other_bin].list.head->item == &other);
    assert(connhash_lock(&ch, as_sockaddr(missing_endpoint)) == NULL);

    locked = connhash_lock(&ch, as_sockaddr(colliding_endpoint));
    assert(locked == &colliding);
    assert(colliding.cn_ref_count == 2);
    assert(connhash_unlock(&ch, locked) == NULL);
    assert(colliding.cn_ref_count == 1);

    locked = connhash_lock(&ch, as_sockaddr(first_endpoint));
    assert(locked == &first);
    assert(first.cn_ref_count == 2);
    assert(connhash_subref(&ch, &first) == NULL);
    assert(first.cn_ref_count == 1);
    assert(connhash_unlock(&ch, locked) == &first);
    assert(first.cn_ref_count == 0);
    assert(ch.table[collision_bin].list.size == 1);
    assert(connhash_lock(&ch, as_sockaddr(first_endpoint)) == NULL);

    assert(connhash_subref(&ch, &colliding) == &colliding);
    assert(colliding.cn_ref_count == 0);
    assert(ch.table[collision_bin].list.size == 0);
    assert(connhash_lock(&ch, as_sockaddr(colliding_endpoint)) == NULL);

    assert(connhash_subref(&ch, &other) == &other);
    assert(other.cn_ref_count == 0);
    assert(ch.table[other_bin].list.size == 0);
    assert(connhash_lock(&ch, as_sockaddr(other_endpoint)) == NULL);

    connhash_destroy(&ch);
    assert(ch.table_size == 4);
    assert(ch.table == NULL);
    connhash_init(&ch);
    assert(ch.table_size == 0);
    assert(ch.table == NULL);
    destroy_connection(&first);
    destroy_connection(&colliding);
    destroy_connection(&other);
}

static void test_ipx_and_comport_bucket_selection(void)
{
    connhash_t ch;
    connection_t ipx;
    connection_t comport;
    connection_t *locked;
    uint8_t ipx_endpoint[16] = {0};
    uint8_t comport_endpoint[16] = {0};
    uint16_t ipx_bin;
    uint32_t index;

    store_native_u16(ipx_endpoint, TEST_AF_IPX);
    for (index = 2; index < 12; ++index)
    {
        ipx_endpoint[index] = (uint8_t)(index == 7 ? 0xFE : index * 9u);
    }
    store_native_u16(ipx_endpoint + 12, UINT16_C(0x4321));
    store_native_u16(comport_endpoint, TEST_AF_COMPORT);
    comport_endpoint[2] = 7;
    comport_endpoint[3] = 9;

    connhash_init(&ch);
    assert(connhash_create(&ch, 8) == 0);
    assert(ch.table_size == 128);
    ipx_bin = expected_ipx_bin(&ch, ipx_endpoint);
    assert(ipx_bin != 0);

    initialize_connection(&ipx, ipx_endpoint);
    initialize_connection(&comport, comport_endpoint);
    connhash_insert(&ch, &ipx);
    connhash_insert(&ch, &comport);
    assert(ch.table[ipx_bin].list.size == 1);
    assert(ch.table[ipx_bin].list.head->item == &ipx);
    assert(ch.table[0].list.size == 1);
    assert(ch.table[0].list.head->item == &comport);

    locked = connhash_lock(&ch, as_sockaddr(ipx_endpoint));
    assert(locked == &ipx);
    assert(connhash_unlock(&ch, locked) == NULL);
    locked = connhash_lock(&ch, as_sockaddr(comport_endpoint));
    assert(locked == &comport);
    assert(connhash_unlock(&ch, locked) == NULL);

    assert(connhash_subref(&ch, &ipx) == &ipx);
    assert(connhash_subref(&ch, &comport) == &comport);
    connhash_destroy(&ch);
    destroy_connection(&ipx);
    destroy_connection(&comport);
}

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
static void test_checked_table_exponents(void)
{
    connhash_t ch;

    assert(connhash_create(NULL, 3) != 0);
    connhash_init(&ch);
    assert(connhash_create(&ch, 12) == 0);
    assert(ch.table_size == 2048);
    assert(ch.table != NULL);
    connhash_destroy(&ch);
    connhash_init(&ch);
    assert(connhash_create(&ch, 0) != 0);
    assert(ch.table_size == 0);
    assert(ch.table == NULL);
    assert(connhash_create(&ch, 13) != 0);
    assert(ch.table_size == 0);
    assert(ch.table == NULL);
}
#endif

int main(void)
{
    test_sockaddr_comparison();
    test_create_and_ipv4_reference_lifecycle();
    test_ipx_and_comport_bucket_selection();
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    test_checked_table_exponents();
#endif
    return 0;
}
