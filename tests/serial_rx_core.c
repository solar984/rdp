// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rdplib_platform.h"
#include "serial_rx.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

enum
{
    TEST_RING_SIZE = 8,
    TEST_CANARY_SIZE = 8,
    TEST_ALLOCATION_SIZE = 64,
    TEST_MAX_READ_STEPS = 4,
    TEST_CANARY = 0xA5
};

typedef struct guarded_ring_t
{
    uint8_t before[TEST_CANARY_SIZE];
    char data[TEST_RING_SIZE];
    uint8_t after[TEST_CANARY_SIZE];
} guarded_ring_t;

typedef union test_allocation_t
{
    uint64_t alignment;
    uint8_t bytes[TEST_CANARY_SIZE + TEST_ALLOCATION_SIZE + TEST_CANARY_SIZE];
} test_allocation_t;

typedef enum read_mode_t
{
    READ_IMMEDIATE_SUCCESS,
    READ_IMMEDIATE_FAILURE_UNTOUCHED,
    READ_PENDING_SUCCESS,
    READ_PENDING_FAILURE
} read_mode_t;

typedef struct read_step_t
{
    read_mode_t mode;
    const void *data;
    uint32_t bytes_read;
    uint32_t expected_request;
    uint32_t initial_error;
    uint32_t completion_error;
} read_step_t;

static test_allocation_t test_allocation;
static size_t requested_allocation_size;
static uint32_t allocation_calls;
static uint32_t free_calls;
static int allocation_active;
static int allocation_should_fail;

static read_step_t read_steps[TEST_MAX_READ_STEPS];
static uint32_t read_step_count;
static uint32_t read_step_index;
static uint32_t read_calls;
static uint32_t completion_calls;
static uint32_t time_calls;
static uint32_t last_error_calls;
static intptr_t expected_endpoint;
static rdplib_platform_serial_async_t *pending_async_state;
static const read_step_t *pending_step;

_Static_assert(offsetof(serial_rx_t, head) == 0, "serial_rx_t::head moved");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(serial_rx_t, max_size) == 0x04, "serial_rx_t::max_size moved");
_Static_assert(offsetof(serial_rx_t, read_pos) == 0x08, "serial_rx_t::read_pos moved");
_Static_assert(offsetof(serial_rx_t, write_pos) == 0x0C, "serial_rx_t::write_pos moved");
_Static_assert(offsetof(serial_rx_t, size) == 0x10, "serial_rx_t::size moved");
_Static_assert(sizeof(serial_rx_t) == 0x14, "serial_rx_t no longer matches the recovered x86 layout");
#elif UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(serial_rx_t, max_size) == 0x08, "serial_rx_t::max_size moved");
_Static_assert(offsetof(serial_rx_t, read_pos) == 0x10, "serial_rx_t::read_pos moved");
_Static_assert(offsetof(serial_rx_t, write_pos) == 0x18, "serial_rx_t::write_pos moved");
_Static_assert(offsetof(serial_rx_t, size) == 0x20, "serial_rx_t::size moved");
_Static_assert(sizeof(serial_rx_t) == 0x28, "serial_rx_t native layout changed");
#else
#error Unsupported pointer width
#endif

_Static_assert(_Generic(&serial_rx_init, void (*)(serial_rx_t *): 1, default: 0), "serial_rx_init signature");
_Static_assert(_Generic(&serial_rx_create, uint32_t (*)(serial_rx_t *, uint32_t): 1, default: 0), "serial_rx_create signature");
_Static_assert(_Generic(&serial_rx_destroy, void (*)(serial_rx_t *): 1, default: 0), "serial_rx_destroy signature");
_Static_assert(_Generic(&serial_rx_fill, void (*)(serial_rx_t *, void *): 1, default: 0), "serial_rx_fill signature");
_Static_assert(_Generic(&serial_rx_read, void (*)(serial_rx_t *, void *, uint32_t): 1, default: 0), "serial_rx_read signature");
_Static_assert(_Generic(&serial_rx_peek, void (*)(serial_rx_t *, void *, uint32_t): 1, default: 0), "serial_rx_peek signature");
#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&serial_rx_clear, void (*)(serial_rx_t *): 1, default: 0), "serial_rx_clear signature");
_Static_assert(_Generic(&serial_rx_write, void (*)(serial_rx_t *, void *, uint32_t): 1, default: 0), "serial_rx_write signature");
_Static_assert(_Generic(&__dpf_release, void (*)(const char *, ...): 1, default: 0), "__dpf_release signature");
#endif

static int bytes_are_zero(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0; index < size; ++index)
    {
        if (bytes[index] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void assert_bytes_equal(const void *data, uint8_t value, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0; index < size; ++index)
    {
        assert(bytes[index] == value);
    }
}

static void assert_ring_canaries(const guarded_ring_t *storage)
{
    assert_bytes_equal(storage->before, TEST_CANARY, sizeof(storage->before));
    assert_bytes_equal(storage->after, TEST_CANARY, sizeof(storage->after));
}

static void initialize_ring(serial_rx_t *serial_rx, guarded_ring_t *storage)
{
    memset(storage, TEST_CANARY, sizeof(*storage));
    memset(storage->data, 0, sizeof(storage->data));
    serial_rx_init(serial_rx);
    serial_rx->head = storage->data;
    serial_rx->max_size = (uint32_t)sizeof(storage->data);
    serial_rx->read_pos = serial_rx->head;
    serial_rx->write_pos = serial_rx->head;
}

static void reset_allocation_stub(void)
{
    assert(!allocation_active);
    requested_allocation_size = 0;
    allocation_calls = 0;
    free_calls = 0;
    allocation_should_fail = 0;
}

void *rdplib_platform_malloc(size_t size)
{
    uint8_t *allocation = test_allocation.bytes + TEST_CANARY_SIZE;

    ++allocation_calls;
    requested_allocation_size = size;
    assert(!allocation_active);
    if (allocation_should_fail)
    {
        return NULL;
    }

    assert(size <= TEST_ALLOCATION_SIZE);
    memset(test_allocation.bytes, TEST_CANARY, sizeof(test_allocation.bytes));
    memset(allocation, 0xCC, size);
    allocation_active = 1;
    return allocation;
}

void rdplib_platform_free(void *allocation)
{
    assert(allocation_active);
    assert(allocation == test_allocation.bytes + TEST_CANARY_SIZE);
    assert_bytes_equal(test_allocation.bytes, TEST_CANARY, TEST_CANARY_SIZE);
    assert_bytes_equal(test_allocation.bytes + TEST_CANARY_SIZE + requested_allocation_size, TEST_CANARY, TEST_CANARY_SIZE);
    allocation_active = 0;
    ++free_calls;
}

uint32_t rdplib_platform_last_system_error(void)
{
    ++last_error_calls;
    return 0x12345678u;
}

uint32_t time_get_ms(void)
{
    ++time_calls;
    return 0x89ABCDEFu;
}

static void reset_read_script(void *endpoint)
{
    memset(read_steps, 0, sizeof(read_steps));
    read_step_count = 0;
    read_step_index = 0;
    read_calls = 0;
    completion_calls = 0;
    time_calls = 0;
    last_error_calls = 0;
    expected_endpoint = (intptr_t)endpoint;
    pending_async_state = NULL;
    pending_step = NULL;
}

static void add_read_step(read_mode_t mode, const void *data, uint32_t bytes_read, uint32_t expected_request, uint32_t initial_error, uint32_t completion_error)
{
    read_step_t *step;

    assert(read_step_count < TEST_MAX_READ_STEPS);
    step = &read_steps[read_step_count++];
    step->mode = mode;
    step->data = data;
    step->bytes_read = bytes_read;
    step->expected_request = expected_request;
    step->initial_error = initial_error;
    step->completion_error = completion_error;
}

static void assert_read_script_complete(void)
{
    assert(read_step_index == read_step_count);
    assert(pending_step == NULL);
    assert(pending_async_state == NULL);
}

int rdplib_platform_serial_read(intptr_t endpoint, rdplib_platform_serial_async_t *async_state, void *data, uint32_t bytes, uint32_t *bytes_read, uint32_t *error_code)
{
    const read_step_t *step;

    assert(endpoint == expected_endpoint);
    assert(read_step_index < read_step_count);
    assert(pending_step == NULL);
    assert(bytes_are_zero(async_state, sizeof(*async_state)));
    step = &read_steps[read_step_index++];
    assert(bytes == step->expected_request);
    assert(step->bytes_read <= bytes);
    ++read_calls;

    if (step->mode == READ_IMMEDIATE_SUCCESS)
    {
        if (step->bytes_read)
        {
            assert(step->data != NULL);
            memcpy(data, step->data, step->bytes_read);
        }
        *bytes_read = step->bytes_read;
        *error_code = 0;
        return 1;
    }
    if (step->mode == READ_IMMEDIATE_FAILURE_UNTOUCHED)
    {
        // Reproduce the historical failure contract: ReadFile may fail
        // without writing lpNumberOfBytesRead. Source faithful tests use the
        // maintained adapter contract, which always returns a defined zero.
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
        *bytes_read = 0;
#endif
        *error_code = step->initial_error;
        return 0;
    }

    assert(step->initial_error == 997u);
    if (step->mode == READ_PENDING_SUCCESS && step->bytes_read)
    {
        assert(step->data != NULL);
        memcpy(data, step->data, step->bytes_read);
    }
    pending_async_state = async_state;
    pending_step = step;
    *bytes_read = 0;
    *error_code = step->initial_error;
    return 0;
}

int rdplib_platform_serial_get_read_result(intptr_t endpoint, rdplib_platform_serial_async_t *async_state, int wait, uint32_t *bytes_read, uint32_t *error_code)
{
    const read_step_t *step = pending_step;

    assert(endpoint == expected_endpoint);
    assert(step != NULL);
    assert(async_state == pending_async_state);
    assert(wait == 1);
    ++completion_calls;
    pending_step = NULL;
    pending_async_state = NULL;
    *bytes_read = step->bytes_read;
    *error_code = step->completion_error;
    return step->mode == READ_PENDING_SUCCESS;
}

static void test_init(void)
{
    serial_rx_t serial_rx;

    memset(&serial_rx, 0xA5, sizeof(serial_rx));
    serial_rx_init(&serial_rx);
    assert(bytes_are_zero(&serial_rx, sizeof(serial_rx)));
}

static void test_create_and_destroy(void)
{
    serial_rx_t serial_rx;
    char *allocated;

    reset_allocation_stub();
    serial_rx_init(&serial_rx);
    serial_rx.size = 7;
    assert(serial_rx_create(&serial_rx, 23) == 0);
    allocated = test_allocation.bytes + TEST_CANARY_SIZE;
    assert(allocation_calls == 1);
    assert(requested_allocation_size == 23);
    assert(serial_rx.head == allocated);
    assert(serial_rx.max_size == 23);
    assert(serial_rx.read_pos == allocated);
    assert(serial_rx.write_pos == allocated);
    assert(serial_rx.size == 7);

    serial_rx_destroy(&serial_rx);
    assert(free_calls == 1);
    assert(!allocation_active);
    assert(serial_rx.head == NULL);
    assert(serial_rx.max_size == 23);
    assert(serial_rx.read_pos == allocated);
    assert(serial_rx.write_pos == allocated);
    assert(serial_rx.size == 7);
}

static void test_create_failure_is_selective(void)
{
    serial_rx_t serial_rx;
    char read_token;
    char write_token;

    reset_allocation_stub();
    allocation_should_fail = 1;
    serial_rx_init(&serial_rx);
    serial_rx.max_size = 91;
    serial_rx.read_pos = &read_token;
    serial_rx.write_pos = &write_token;
    serial_rx.size = 37;

    assert(serial_rx_create(&serial_rx, 17) == 2);
    assert(allocation_calls == 1);
    assert(requested_allocation_size == 17);
    assert(!allocation_active);
    assert(serial_rx.head == NULL);
    assert(serial_rx.max_size == 91);
    assert(serial_rx.read_pos == &read_token);
    assert(serial_rx.write_pos == &write_token);
    assert(serial_rx.size == 37);

    serial_rx_destroy(&serial_rx);
    assert(free_calls == 0);
}

static void test_linear_peek_read_and_discard(void)
{
    static const char contents[] = {'x', 'a', 'b', 'c', 'd', 'e', 'y', 'z'};
    serial_rx_t serial_rx;
    serial_rx_t state_before_peek;
    guarded_ring_t storage;
    char output[5];

    initialize_ring(&serial_rx, &storage);
    memcpy(storage.data, contents, sizeof(contents));
    serial_rx.read_pos = serial_rx.head + 1;
    serial_rx.write_pos = serial_rx.head + 6;
    serial_rx.size = 5;

    state_before_peek = serial_rx;
    memset(output, 0, sizeof(output));
    serial_rx_peek(&serial_rx, output, (uint32_t)sizeof(output));
    assert(memcmp(output, "abcde", sizeof(output)) == 0);
    assert(memcmp(&serial_rx, &state_before_peek, sizeof(serial_rx)) == 0);

    memset(output, 0, sizeof(output));
    serial_rx_read(&serial_rx, output, 3);
    assert(memcmp(output, "abc", 3) == 0);
    assert(serial_rx.head == storage.data);
    assert(serial_rx.max_size == TEST_RING_SIZE);
    assert(serial_rx.read_pos == serial_rx.head + 4);
    assert(serial_rx.write_pos == serial_rx.head + 6);
    assert(serial_rx.size == 2);

    serial_rx_read(&serial_rx, NULL, 2);
    assert(serial_rx.read_pos == serial_rx.write_pos);
    assert(serial_rx.size == 0);
    serial_rx_read(&serial_rx, NULL, 0);
    assert(serial_rx.read_pos == serial_rx.write_pos);
    assert(serial_rx.size == 0);
    assert_ring_canaries(&storage);
}

static void test_wrapped_peek_read_and_discard(void)
{
    serial_rx_t serial_rx;
    serial_rx_t state_before_peek;
    guarded_ring_t storage;
    char output[5];

    initialize_ring(&serial_rx, &storage);
    storage.data[6] = 'a';
    storage.data[7] = 'b';
    storage.data[0] = 'c';
    storage.data[1] = 'd';
    storage.data[2] = 'e';
    serial_rx.read_pos = serial_rx.head + 6;
    serial_rx.write_pos = serial_rx.head + 3;
    serial_rx.size = 5;

    state_before_peek = serial_rx;
    serial_rx_peek(&serial_rx, output, (uint32_t)sizeof(output));
    assert(memcmp(output, "abcde", sizeof(output)) == 0);
    assert(memcmp(&serial_rx, &state_before_peek, sizeof(serial_rx)) == 0);

    serial_rx_read(&serial_rx, output, 4);
    assert(memcmp(output, "abcd", 4) == 0);
    assert(serial_rx.read_pos == serial_rx.head + 2);
    assert(serial_rx.write_pos == serial_rx.head + 3);
    assert(serial_rx.size == 1);
    serial_rx_read(&serial_rx, NULL, 1);
    assert(serial_rx.read_pos == serial_rx.write_pos);
    assert(serial_rx.size == 0);
    assert_ring_canaries(&storage);
}

static void test_full_ring_peek_and_read(void)
{
    static const char expected[] = {'5', '6', '7', '0', '1', '2', '3', '4'};
    serial_rx_t serial_rx;
    serial_rx_t state_before_peek;
    guarded_ring_t storage;
    char output[TEST_RING_SIZE];
    uint32_t index;

    initialize_ring(&serial_rx, &storage);
    for (index = 0; index < TEST_RING_SIZE; ++index)
    {
        storage.data[index] = (char)('0' + index);
    }
    serial_rx.read_pos = serial_rx.head + 5;
    serial_rx.write_pos = serial_rx.read_pos;
    serial_rx.size = TEST_RING_SIZE;

    state_before_peek = serial_rx;
    serial_rx_peek(&serial_rx, output, (uint32_t)sizeof(output));
    assert(memcmp(output, expected, sizeof(output)) == 0);
    assert(memcmp(&serial_rx, &state_before_peek, sizeof(serial_rx)) == 0);

    serial_rx_read(&serial_rx, output, (uint32_t)sizeof(output));
    assert(memcmp(output, expected, sizeof(output)) == 0);
    assert(serial_rx.read_pos == serial_rx.write_pos);
    assert(serial_rx.read_pos == serial_rx.head + 5);
    assert(serial_rx.size == 0);
    assert_ring_canaries(&storage);
}

#ifdef RDP_DEAD_CODE
static void test_dead_clear_and_write(void)
{
    static char first[] = {'a', 'b', 'c', 'd', 'e'};
    static char second[] = {'f', 'g', 'h', 'i', 'j', 'k'};
    serial_rx_t serial_rx;
    guarded_ring_t storage;
    char output[TEST_RING_SIZE];
    void (*historical_logger)(const char *, ...) = __dpf_release;

    assert(historical_logger != NULL);
    initialize_ring(&serial_rx, &storage);
    serial_rx.read_pos = serial_rx.head + 3;
    serial_rx.write_pos = serial_rx.head + 7;
    serial_rx.size = 4;
    memset(storage.data, 'x', sizeof(storage.data));
    serial_rx_clear(&serial_rx);
    assert(serial_rx.read_pos == serial_rx.head);
    assert(serial_rx.write_pos == serial_rx.head);
    assert(serial_rx.size == 0);
    assert_bytes_equal(storage.data, 'x', sizeof(storage.data));

    serial_rx_write(&serial_rx, first, (uint32_t)sizeof(first));
    assert(serial_rx.read_pos == serial_rx.head);
    assert(serial_rx.write_pos == serial_rx.head + 5);
    assert(serial_rx.size == 5);
    serial_rx_read(&serial_rx, output, 3);
    assert(memcmp(output, "abc", 3) == 0);
    assert(serial_rx.read_pos == serial_rx.head + 3);
    assert(serial_rx.write_pos == serial_rx.head + 5);
    assert(serial_rx.size == 2);

    serial_rx_write(&serial_rx, second, (uint32_t)sizeof(second));
    assert(serial_rx.read_pos == serial_rx.write_pos);
    assert(serial_rx.read_pos == serial_rx.head + 3);
    assert(serial_rx.size == TEST_RING_SIZE);
    serial_rx_peek(&serial_rx, output, (uint32_t)sizeof(output));
    assert(memcmp(output, "defghijk", sizeof(output)) == 0);
    assert_ring_canaries(&storage);
}
#endif

static void test_fill_full_ring_does_not_read(void)
{
    int endpoint_token;
    serial_rx_t serial_rx;
    serial_rx_t state_before_fill;
    guarded_ring_t storage;

    initialize_ring(&serial_rx, &storage);
    serial_rx.read_pos = serial_rx.head + 4;
    serial_rx.write_pos = serial_rx.read_pos;
    serial_rx.size = TEST_RING_SIZE;
    state_before_fill = serial_rx;
    reset_read_script(&endpoint_token);

    serial_rx_fill(&serial_rx, &endpoint_token);
    assert(read_calls == 0);
    assert(completion_calls == 0);
    assert(time_calls == 0);
    assert(last_error_calls == 0);
    assert(memcmp(&serial_rx, &state_before_fill, sizeof(serial_rx)) == 0);
    assert_read_script_complete();
    assert_ring_canaries(&storage);
}

static void test_fill_immediate_short_read(void)
{
    static const char input[] = {'a', 'b', 'c'};
    int endpoint_token;
    serial_rx_t serial_rx;
    guarded_ring_t storage;

    initialize_ring(&serial_rx, &storage);
    reset_read_script(&endpoint_token);
    add_read_step(READ_IMMEDIATE_SUCCESS, input, (uint32_t)sizeof(input), TEST_RING_SIZE, 0, 0);

    serial_rx_fill(&serial_rx, &endpoint_token);
    assert(read_calls == 1);
    assert(completion_calls == 0);
    assert(time_calls == 0);
    assert(last_error_calls == 0);
    assert(memcmp(storage.data, input, sizeof(input)) == 0);
    assert(serial_rx.read_pos == serial_rx.head);
    assert(serial_rx.write_pos == serial_rx.head + 3);
    assert(serial_rx.size == 3);
    assert_read_script_complete();
    assert_ring_canaries(&storage);
}

static void test_fill_exact_wrapped_spans_to_full(void)
{
    static const char tail[] = {'x', 'y'};
    static const char head[] = {'p', 'q', 'r'};
    int endpoint_token;
    serial_rx_t serial_rx;
    guarded_ring_t storage;

    initialize_ring(&serial_rx, &storage);
    storage.data[3] = 'a';
    storage.data[4] = 'b';
    storage.data[5] = 'c';
    serial_rx.read_pos = serial_rx.head + 3;
    serial_rx.write_pos = serial_rx.head + 6;
    serial_rx.size = 3;
    reset_read_script(&endpoint_token);
    add_read_step(READ_IMMEDIATE_SUCCESS, tail, (uint32_t)sizeof(tail), 2, 0, 0);
    add_read_step(READ_IMMEDIATE_SUCCESS, head, (uint32_t)sizeof(head), 3, 0, 0);

    serial_rx_fill(&serial_rx, &endpoint_token);
    assert(read_calls == 2);
    assert(completion_calls == 0);
    assert(time_calls == 0);
    assert(last_error_calls == 0);
    assert(memcmp(storage.data, head, sizeof(head)) == 0);
    assert(memcmp(storage.data + 3, "abc", 3) == 0);
    assert(memcmp(storage.data + 6, tail, sizeof(tail)) == 0);
    assert(serial_rx.read_pos == serial_rx.head + 3);
    assert(serial_rx.write_pos == serial_rx.read_pos);
    assert(serial_rx.size == TEST_RING_SIZE);
    assert_read_script_complete();
    assert_ring_canaries(&storage);
}

static void test_fill_pending_success(void)
{
    static const char input[] = {'p', 'e', 'n', 'd'};
    int endpoint_token;
    serial_rx_t serial_rx;
    guarded_ring_t storage;

    initialize_ring(&serial_rx, &storage);
    reset_read_script(&endpoint_token);
    add_read_step(READ_PENDING_SUCCESS, input, (uint32_t)sizeof(input), TEST_RING_SIZE, 997, 0);

    serial_rx_fill(&serial_rx, &endpoint_token);
    assert(read_calls == 1);
    assert(completion_calls == 1);
    assert(time_calls == 0);
    assert(last_error_calls == 0);
    assert(memcmp(storage.data, input, sizeof(input)) == 0);
    assert(serial_rx.read_pos == serial_rx.head);
    assert(serial_rx.write_pos == serial_rx.head + 4);
    assert(serial_rx.size == 4);
    assert_read_script_complete();
    assert_ring_canaries(&storage);
}

static void test_fill_pending_failure(void)
{
    int endpoint_token;
    serial_rx_t serial_rx;
    serial_rx_t state_before_fill;
    guarded_ring_t storage;

    initialize_ring(&serial_rx, &storage);
    state_before_fill = serial_rx;
    reset_read_script(&endpoint_token);
    add_read_step(READ_PENDING_FAILURE, NULL, 5, TEST_RING_SIZE, 997, 31);

    serial_rx_fill(&serial_rx, &endpoint_token);
    assert(read_calls == 1);
    assert(completion_calls == 1);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(time_calls == 1);
    assert(last_error_calls == 1);
#else
    assert(time_calls == 0);
    assert(last_error_calls == 0);
#endif
    assert(memcmp(&serial_rx, &state_before_fill, sizeof(serial_rx)) == 0);
    assert_read_script_complete();
    assert_ring_canaries(&storage);
}

static void test_fill_immediate_failure(void)
{
    int endpoint_token;
    serial_rx_t serial_rx;
    serial_rx_t state_before_fill;
    guarded_ring_t storage;

    initialize_ring(&serial_rx, &storage);
    state_before_fill = serial_rx;
    reset_read_script(&endpoint_token);
    add_read_step(READ_IMMEDIATE_FAILURE_UNTOUCHED, NULL, 0, TEST_RING_SIZE, 5, 0);

    serial_rx_fill(&serial_rx, &endpoint_token);
    assert(read_calls == 1);
    assert(completion_calls == 0);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(time_calls == 1);
    assert(last_error_calls == 1);
#else
    assert(time_calls == 0);
    assert(last_error_calls == 0);
#endif
    assert(memcmp(&serial_rx, &state_before_fill, sizeof(serial_rx)) == 0);
    assert_read_script_complete();
    assert_ring_canaries(&storage);
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_init();
    test_create_and_destroy();
    test_create_failure_is_selective();
    test_linear_peek_read_and_discard();
    test_wrapped_peek_read_and_discard();
    test_full_ring_peek_and_read();
#ifdef RDP_DEAD_CODE
    test_dead_clear_and_write();
#endif
    test_fill_full_ring_does_not_read();
    test_fill_immediate_short_read();
    test_fill_exact_wrapped_spans_to_full();
    test_fill_pending_success();
    test_fill_pending_failure();
    test_fill_immediate_failure();
    return 0;
}
