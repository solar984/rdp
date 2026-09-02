// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rdp.h"

static uint32_t allocation_calls;
static uint32_t free_calls;
static int fail_next_allocation;
static uint32_t close_calls;
static connection_t *closed_raw;
static uint32_t closed_linger;
static uint32_t rdp_destroy_calls;
static rdp_t *destroyed_rdp;
static int destroyed_rdp_wait;
static connection_t *rdp_destroy_expected_connection[3];
static uint32_t rdp_destroy_expected_connection_count;
static uint32_t rdp_destroy_expected_free_calls;

void *facade_owner_test_malloc(size_t size);
void facade_owner_test_free(void *allocation);
uint32_t facade_owner_test_connection_close(connection_t *raw, uint32_t linger_time, uint32_t *all_acked, uevent_t *all_acked_event);
void facade_owner_test_rdp_destroy(rdp_t *rdp, int wait);

#define rdplib_platform_malloc facade_owner_test_malloc
#define rdplib_platform_free facade_owner_test_free
#define connection_close facade_owner_test_connection_close
#define rdp_destroy facade_owner_test_rdp_destroy
#include "../rdplib/src/rdplib.c"
#undef rdp_destroy
#undef connection_close
#undef rdplib_platform_free
#undef rdplib_platform_malloc

void *facade_owner_test_malloc(size_t size)
{
    ++allocation_calls;
    if (fail_next_allocation)
    {
        fail_next_allocation = 0;
        return NULL;
    }
    return malloc(size);
}

void facade_owner_test_free(void *allocation)
{
    ++free_calls;
    free(allocation);
}

uint32_t facade_owner_test_connection_close(connection_t *raw, uint32_t linger_time, uint32_t *all_acked, uevent_t *all_acked_event)
{
    assert(raw != NULL);
    assert(connection_app_ptr(raw)[0] == NULL);
    assert(all_acked == NULL);
    assert(all_acked_event == NULL);
    ++close_calls;
    closed_raw = raw;
    closed_linger = linger_time;
    return 0;
}

void facade_owner_test_rdp_destroy(rdp_t *rdp, int wait)
{
    uint32_t index;

    assert(free_calls == rdp_destroy_expected_free_calls);
    for (index = 0; index < rdp_destroy_expected_connection_count; ++index)
    {
        assert(connection_app_ptr(rdp_destroy_expected_connection[index])[0] == NULL);
    }
    ++rdp_destroy_calls;
    destroyed_rdp = rdp;
    destroyed_rdp_wait = wait;
}

static void reset_seams(void)
{
    allocation_calls = 0;
    free_calls = 0;
    fail_next_allocation = 0;
    close_calls = 0;
    closed_raw = NULL;
    closed_linger = 0;
    rdp_destroy_calls = 0;
    destroyed_rdp = NULL;
    destroyed_rdp_wait = 0;
    memset(rdp_destroy_expected_connection, 0, sizeof(rdp_destroy_expected_connection));
    rdp_destroy_expected_connection_count = 0;
    rdp_destroy_expected_free_calls = 0;
}

static void initialize_raw_connection(connection_t *raw, int initialize_lock)
{
    memset(raw, 0, sizeof(*raw));
    if (initialize_lock)
    {
        umutex_create(&raw->cn_lock);
    }
}

static void destroy_application_handle(rdplib_endpoint_t *endpoint, rdplib_connection_t *connection)
{
    assert(connection->accept_next == NULL);
    assert(endpoint->application_connection_count != 0);

    (void)rdplib_connection_detach_raw(connection);
    --endpoint->application_connection_count;
    connection->application_owned = 0;
    connection->endpoint = NULL;
    rdplib_connection_destroy_handle(connection);
}

static void test_allocation_failure_does_not_publish_handle(void)
{
    rdplib_endpoint_t endpoint;
    connection_t raw;

    reset_seams();
    memset(&endpoint, 0, sizeof(endpoint));
    initialize_raw_connection(&raw, 0);
    fail_next_allocation = 1;

    assert(rdplib_create_connection_handle(&endpoint, &raw, 1) == NULL);
    assert(allocation_calls == 1);
    assert(free_calls == 0);
    assert(connection_app_ptr(&raw)[0] == NULL);
    assert(endpoint.accept_head == NULL);
    assert(endpoint.accept_tail == NULL);
    assert(endpoint.application_connection_count == 0);
}

static void test_raw_association_routes_arrival(void)
{
    rdplib_runtime_t runtime;
    rdplib_endpoint_t endpoint;
    rdplib_connection_t *connection;
    rdplib_message_t *message;
    msg_arrival_t *arrival;
    connection_t raw;

    reset_seams();
    memset(&runtime, 0, sizeof(runtime));
    memset(&endpoint, 0, sizeof(endpoint));
    initialize_raw_connection(&raw, 0);
    rdplib_platform_mutex_prepare(&runtime.lock);
    rdplib_platform_mutex_init(&runtime.lock);
    endpoint.runtime = &runtime;
    fast_malloc_init(4096);

    connection = rdplib_create_connection_handle(&endpoint, &raw, 0);
    assert(connection != NULL);
    assert(connection_app_ptr(&raw)[0] == connection);
    assert(rdplib_connection_from_raw(&raw) == connection);

    arrival = (msg_arrival_t *)fast_malloc(sizeof(*arrival));
    assert(arrival != NULL);
    memset(arrival, 0, sizeof(*arrival));
    arrival->sender = &raw;
    arrival->size = 1;

    assert(rdplib_process_arrival(&endpoint, arrival) == RDPLIB_OK);
    assert(connection->message_head != NULL);

    message = rdplib_connection_pop_message(connection);
    assert(message != NULL);
    rdplib_message_release(message);
    assert(runtime.outstanding_messages == 0);

    destroy_application_handle(&endpoint, connection);
    assert(connection_app_ptr(&raw)[0] == NULL);
    assert(endpoint.application_connection_count == 0);
    assert(allocation_calls == 2);
    assert(free_calls == 2);
    fast_malloc_destroy();
    rdplib_platform_mutex_destroy(&runtime.lock);
}

static void test_begin_close_detaches_before_raw_close(void)
{
    rdplib_endpoint_t endpoint;
    rdplib_connection_t *connection;
    connection_t raw;

    reset_seams();
    memset(&endpoint, 0, sizeof(endpoint));
    initialize_raw_connection(&raw, 1);

    connection = rdplib_create_connection_handle(&endpoint, &raw, 0);
    assert(connection != NULL);
    assert(rdplib_connection_begin_close(connection, 321) == 0);
    assert(close_calls == 1);
    assert(closed_raw == &raw);
    assert(closed_linger == 321);
    assert(connection->raw == NULL);
    assert(connection_app_ptr(&raw)[0] == NULL);

    rdplib_connection_release(connection);
    assert(close_calls == 1);
    assert(endpoint.application_connection_count == 0);
    assert(free_calls == 1);
    umutex_destroy(&raw.cn_lock);
}

static void test_terminal_release_detaches_before_raw_close(void)
{
    rdplib_endpoint_t endpoint;
    rdplib_connection_t *connection;
    connection_t raw;

    reset_seams();
    memset(&endpoint, 0, sizeof(endpoint));
    initialize_raw_connection(&raw, 1);

    connection = rdplib_create_connection_handle(&endpoint, &raw, 0);
    assert(connection != NULL);
    connection->peer_fin = 1;
    rdplib_connection_release(connection);

    assert(close_calls == 1);
    assert(closed_raw == &raw);
    assert(closed_linger == 0);
    assert(connection_app_ptr(&raw)[0] == NULL);
    assert(endpoint.application_connection_count == 0);
    assert(free_calls == 1);
    umutex_destroy(&raw.cn_lock);
}

static void test_endpoint_destroy_detaches_pending_accepts(void)
{
    rdplib_runtime_t runtime;
    rdplib_endpoint_t *endpoint;
    rdp_t raw_endpoint;
    connection_t raw_connection[3];
    rdplib_connection_t *connection[3];
    uint32_t index;

    reset_seams();
    memset(&runtime, 0, sizeof(runtime));
    memset(&raw_endpoint, 0, sizeof(raw_endpoint));
    rdplib_platform_mutex_prepare(&runtime.lock);
    rdplib_platform_mutex_init(&runtime.lock);
    runtime.endpoint_count = 1;

    endpoint = (rdplib_endpoint_t *)facade_owner_test_malloc(sizeof(*endpoint));
    assert(endpoint != NULL);
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->runtime = &runtime;
    endpoint->raw = &raw_endpoint;

    for (index = 0; index < 3; ++index)
    {
        initialize_raw_connection(&raw_connection[index], 0);
        connection[index] = rdplib_create_connection_handle(endpoint, &raw_connection[index], 1);
        assert(connection[index] != NULL);
        assert(connection_app_ptr(&raw_connection[index])[0] == connection[index]);
        rdp_destroy_expected_connection[index] = &raw_connection[index];
    }
    rdp_destroy_expected_connection_count = 3;
    rdp_destroy_expected_free_calls = 3;

    assert(endpoint->accept_head == connection[0]);
    assert(endpoint->accept_tail == connection[2]);
    assert(rdplib_endpoint_accept(endpoint) == connection[0]);
    assert(connection[0]->accept_next == NULL);
    assert(connection[0]->application_owned);
    assert(endpoint->application_connection_count == 1);
    assert(endpoint->accept_head == connection[1]);
    assert(endpoint->accept_tail == connection[2]);
    assert(rdplib_endpoint_destroy(endpoint) == RDPLIB_ERROR_BUSY);
    assert(rdp_destroy_calls == 0);

    destroy_application_handle(endpoint, connection[0]);
    assert(endpoint->application_connection_count == 0);
    assert(connection_app_ptr(&raw_connection[0])[0] == NULL);

    assert(rdplib_endpoint_destroy(endpoint) == RDPLIB_OK);
    assert(rdp_destroy_calls == 1);
    assert(destroyed_rdp == &raw_endpoint);
    assert(destroyed_rdp_wait == 1);
    assert(runtime.endpoint_count == 0);
    assert(allocation_calls == 4);
    assert(free_calls == 4);
    for (index = 0; index < 3; ++index)
    {
        assert(connection_app_ptr(&raw_connection[index])[0] == NULL);
    }
    rdplib_platform_mutex_destroy(&runtime.lock);
}

int main(void)
{
    test_allocation_failure_does_not_publish_handle();
    test_raw_association_routes_arrival();
    test_begin_close_detaches_before_raw_close();
    test_terminal_release_detaches_before_raw_close();
    test_endpoint_destroy_detaches_pending_accepts();
    return 0;
}
