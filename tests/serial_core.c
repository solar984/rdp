// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "serial_tx.h"
#include "tx_bufq.h"

#include "serial.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

enum
{
    TEST_COOKIE_SIZE = 18,
    TEST_HEADER_SIZE = 8,
    TEST_MAX_PAYLOAD = 536,
    TEST_INVALID_ARGUMENT = 6,
    TEST_CAPACITY_EXCEEDED = 18,
    TEST_ERROR_NOT_ENOUGH_MEMORY = 8,
    TEST_ERROR_IO_INCOMPLETE = 996,
    TEST_ERROR_IO_PENDING = 997,
    TEST_ERROR_INVALID_USER_BUFFER = 1784,
    TEST_FRAME_CAPACITY = TEST_COOKIE_SIZE + TEST_HEADER_SIZE + TEST_MAX_PAYLOAD + 1
};

typedef enum write_mode_t
{
    WRITE_IMMEDIATE_FULL,
    WRITE_IMMEDIATE_SHORT,
    WRITE_PENDING,
    WRITE_RESOURCE_INVALID_BUFFER,
    WRITE_RESOURCE_NO_MEMORY,
    WRITE_FATAL
} write_mode_t;

typedef enum completion_mode_t
{
    COMPLETION_FULL,
    COMPLETION_SHORT,
    COMPLETION_INCOMPLETE,
    COMPLETION_ERROR
} completion_mode_t;

typedef union frame_storage_t
{
    uint64_t alignment;
    uint8_t bytes[TEST_FRAME_CAPACITY];
} frame_storage_t;

static const char test_cookie[TEST_COOKIE_SIZE] = "\n\rwho's yo daddy?";
static write_mode_t write_mode;
static completion_mode_t completion_mode;
static uint32_t current_time_ms;
static uint32_t time_get_calls;
static uint32_t write_calls;
static uint32_t completion_calls;
static int last_completion_wait;
static uint32_t close_calls;
static uint32_t fast_allocations;
static uint32_t fast_frees;
static uint32_t pending_write_size;
static int fail_receive_allocation;
static int fail_event_creation;
static int fail_fast_allocation;
static intptr_t next_event = 100;
static uint8_t captured_frame[TEST_FRAME_CAPACITY];
static uint32_t captured_frame_size;

_Static_assert(sizeof(sockaddr_com) == 0x10, "sockaddr_com size");
_Static_assert(offsetof(sockaddr_com, scom_family) == 0x00, "sockaddr_com::scom_family moved");
_Static_assert(offsetof(sockaddr_com, scom_port) == 0x02, "sockaddr_com::scom_port moved");
_Static_assert(offsetof(sockaddr_com, scom_zero) == 0x04, "sockaddr_com::scom_zero moved");

_Static_assert(sizeof(serial_stat_t) == 0x40, "serial_stat_t size");
_Static_assert(offsetof(serial_stat_t, time_since_retire) == 0x00, "serial_stat_t::time_since_retire moved");
_Static_assert(offsetof(serial_stat_t, tx_operations) == 0x04, "serial_stat_t::tx_operations moved");
_Static_assert(offsetof(serial_stat_t, tx_bytes) == 0x08, "serial_stat_t::tx_bytes moved");
_Static_assert(offsetof(serial_stat_t, tx_age) == 0x0c, "serial_stat_t::tx_age moved");
_Static_assert(offsetof(serial_stat_t, bad_header_size) == 0x10, "serial_stat_t::bad_header_size moved");
_Static_assert(offsetof(serial_stat_t, bad_checksum) == 0x14, "serial_stat_t::bad_checksum moved");
_Static_assert(offsetof(serial_stat_t, wrong_rdp) == 0x18, "serial_stat_t::wrong_rdp moved");
_Static_assert(offsetof(serial_stat_t, bytes_accepted) == 0x1c, "serial_stat_t::bytes_accepted moved");
_Static_assert(offsetof(serial_stat_t, bytes_discarded) == 0x20, "serial_stat_t::bytes_discarded moved");
_Static_assert(offsetof(serial_stat_t, incomplete_writes) == 0x24, "serial_stat_t::incomplete_writes moved");
_Static_assert(offsetof(serial_stat_t, complete_writes) == 0x28, "serial_stat_t::complete_writes moved");
_Static_assert(offsetof(serial_stat_t, instant_writes) == 0x2c, "serial_stat_t::instant_writes moved");
_Static_assert(offsetof(serial_stat_t, failed_writes) == 0x30, "serial_stat_t::failed_writes moved");
_Static_assert(offsetof(serial_stat_t, fail_error) == 0x34, "serial_stat_t::fail_error moved");
_Static_assert(offsetof(serial_stat_t, partial_writes) == 0x38, "serial_stat_t::partial_writes moved");
_Static_assert(offsetof(serial_stat_t, try_again) == 0x3c, "serial_stat_t::try_again moved");

_Static_assert(offsetof(serial_tx_buf_t, link) == 0, "serial_tx_buf_t::link moved");
_Static_assert(offsetof(serial_tx_buf_t, o) == sizeof(rdp_link_t), "serial_tx_buf_t::o moved");
_Static_assert(offsetof(serial_tx_buf_t, write_size) == sizeof(rdp_link_t) + sizeof(rdplib_platform_serial_async_t), "serial_tx_buf_t::write_size moved");
_Static_assert(offsetof(serial_tx_buf_t, start_time) == offsetof(serial_tx_buf_t, write_size) + sizeof(uint32_t), "serial_tx_buf_t::start_time moved");
_Static_assert(offsetof(tx_bufq_t, list) == 0, "tx_bufq_t::list moved");
_Static_assert(offsetof(tx_bufq_t, bytes) == sizeof(list_t), "tx_bufq_t::bytes moved");

#if defined(_WIN32) && UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(serial_tx_buf_t) == 0x2c, "serial_tx_buf_t x86 size");
_Static_assert(offsetof(serial_tx_buf_t, o) == 0x10, "serial_tx_buf_t::o x86 offset");
_Static_assert(offsetof(serial_tx_buf_t, write_size) == 0x24, "serial_tx_buf_t::write_size x86 offset");
_Static_assert(offsetof(serial_tx_buf_t, start_time) == 0x28, "serial_tx_buf_t::start_time x86 offset");
_Static_assert(sizeof(tx_bufq_t) == 0x18, "tx_bufq_t x86 size");
_Static_assert(sizeof(serial_t) == 0x94 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t x86 size");
_Static_assert(offsetof(serial_t, time_next_recv) == 0x18 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::time_next_recv x86 offset");
_Static_assert(offsetof(serial_t, file) == 0x1c + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::file x86 offset");
_Static_assert(offsetof(serial_t, local_port) == 0x20 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::local_port x86 offset");
_Static_assert(offsetof(serial_t, serial_rx) == 0x24 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::serial_rx x86 offset");
_Static_assert(offsetof(serial_t, tx_in_progress) == 0x38 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::tx_in_progress x86 offset");
_Static_assert(offsetof(serial_t, stats) == 0x50 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::stats x86 offset");
_Static_assert(offsetof(serial_t, rx_state) == 0x90 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::rx_state x86 offset");
#elif defined(_WIN32) && UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(serial_tx_buf_t) == 0x48, "serial_tx_buf_t Win64 size");
_Static_assert(offsetof(serial_tx_buf_t, o) == 0x20, "serial_tx_buf_t::o Win64 offset");
_Static_assert(offsetof(serial_tx_buf_t, write_size) == 0x40, "serial_tx_buf_t::write_size Win64 offset");
_Static_assert(offsetof(serial_tx_buf_t, start_time) == 0x44, "serial_tx_buf_t::start_time Win64 offset");
_Static_assert(sizeof(tx_bufq_t) == 0x30, "tx_bufq_t Win64 size");
_Static_assert(sizeof(serial_t) == 0xe0 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t Win64 native size");
_Static_assert(offsetof(serial_t, time_next_recv) == 0x28 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::time_next_recv Win64 offset");
_Static_assert(offsetof(serial_t, file) == 0x30 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::file Win64 offset");
_Static_assert(offsetof(serial_t, local_port) == 0x38 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::local_port Win64 offset");
_Static_assert(offsetof(serial_t, serial_rx) == 0x40 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::serial_rx Win64 offset");
_Static_assert(offsetof(serial_t, tx_in_progress) == 0x68 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::tx_in_progress Win64 offset");
_Static_assert(offsetof(serial_t, stats) == 0x98 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::stats Win64 offset");
_Static_assert(offsetof(serial_t, rx_state) == 0xd8 + RDP_WIN32_UMUTEX_OWNER_BYTES, "serial_t::rx_state Win64 offset");
#endif

_Static_assert(_Generic(&internet_checksum, uint16_t (*)(uint16_t *, uint16_t): 1, default: 0), "internet_checksum signature");
_Static_assert(_Generic(&serial_init, void (*)(serial_t *): 1, default: 0), "serial_init signature");
_Static_assert(_Generic(&serial_create, uint32_t (*)(serial_t *, uint16_t): 1, default: 0), "serial_create signature");
_Static_assert(_Generic(&serial_destroy, void (*)(serial_t *): 1, default: 0), "serial_destroy signature");
_Static_assert(_Generic(&serial_recv_from, int32_t (*)(serial_t *, char *, uint32_t, sockaddr_com *): 1, default: 0), "serial_recv_from signature");
_Static_assert(_Generic(&serial_tx_complete, void (*)(serial_tx_buf_t *): 1, default: 0), "serial_tx_complete signature");
_Static_assert(_Generic(&serial_tx_write, uint32_t (*)(serial_t *, serial_tx_buf_t *): 1, default: 0), "serial_tx_write signature");
_Static_assert(_Generic(&serial_send, uint32_t (*)(serial_t *, iov_t *, uint32_t, sockaddr_com *): 1, default: 0), "serial_send signature");
_Static_assert(_Generic(&serial_retire_overlapped_writes, void (*)(serial_t *, int): 1, default: 0), "serial_retire_overlapped_writes signature");
_Static_assert(_Generic(&serial_set_time_next_recv, void (*)(serial_t *, uint32_t): 1, default: 0), "serial_set_time_next_recv signature");
_Static_assert(_Generic(&serial_get_time_next_recv, uint32_t (*)(serial_t *): 1, default: 0), "serial_get_time_next_recv signature");
_Static_assert(_Generic(&serial_tx_ready, uint32_t (*)(serial_t *): 1, default: 0), "serial_tx_ready signature");
_Static_assert(_Generic(&serial_get_time_empty, uint32_t (*)(serial_t *): 1, default: 0), "serial_get_time_empty signature");
_Static_assert(_Generic(&serial_get_stall_time, uint32_t (*)(serial_t *): 1, default: 0), "serial_get_stall_time signature");
_Static_assert(_Generic(&tx_bufq_init, void (*)(tx_bufq_t *): 1, default: 0), "tx_bufq_init signature");
_Static_assert(_Generic(&tx_bufq_create, void (*)(tx_bufq_t *): 1, default: 0), "tx_bufq_create signature");
_Static_assert(_Generic(&tx_bufq_destroy, void (*)(tx_bufq_t *): 1, default: 0), "tx_bufq_destroy signature");
_Static_assert(_Generic(&tx_bufq_remove_head, serial_tx_buf_t *(*)(tx_bufq_t *): 1, default: 0), "tx_bufq_remove_head signature");
_Static_assert(_Generic(&tx_bufq_peek_head, serial_tx_buf_t *(*)(tx_bufq_t *): 1, default: 0), "tx_bufq_peek_head signature");
_Static_assert(_Generic(&tx_bufq_add_tail, void (*)(tx_bufq_t *, serial_tx_buf_t *): 1, default: 0), "tx_bufq_add_tail signature");
_Static_assert(_Generic(&tx_bufq_get_bytes, uint32_t (*)(tx_bufq_t *): 1, default: 0), "tx_bufq_get_bytes signature");
_Static_assert(_Generic(&tx_bufq_get_size, uint32_t (*)(tx_bufq_t *): 1, default: 0), "tx_bufq_get_size signature");
#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&serial_port_create, int32_t (*)(const char *): 1, default: 0), "serial_port_create signature");
_Static_assert(_Generic(&serial_get_stats, void (*)(serial_t *, serial_stat_t *): 1, default: 0), "serial_get_stats signature");
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

static uint16_t load_u16(const void *data)
{
    uint16_t value;

    memcpy(&value, data, sizeof(value));
    return value;
}

static void store_u16(void *data, uint16_t value)
{
    memcpy(data, &value, sizeof(value));
}

static void reset_io(void)
{
    write_mode = WRITE_IMMEDIATE_FULL;
    completion_mode = COMPLETION_FULL;
    write_calls = 0;
    completion_calls = 0;
    last_completion_wait = -1;
    close_calls = 0;
    pending_write_size = 0;
    fail_event_creation = 0;
    captured_frame_size = 0;
    memset(captured_frame, 0, sizeof(captured_frame));
}

void rdplib_platform_mutex_prepare(rdplib_platform_mutex_t *mutex)
{
    mutex->initialized = 0;
}

void rdplib_platform_mutex_init(rdplib_platform_mutex_t *mutex)
{
    mutex->initialized = 1;
}

void rdplib_platform_mutex_destroy(rdplib_platform_mutex_t *mutex)
{
    mutex->initialized = 0;
}

void rdplib_platform_mutex_lock(rdplib_platform_mutex_t *mutex)
{
    assert(mutex->initialized);
}

void rdplib_platform_mutex_unlock(rdplib_platform_mutex_t *mutex)
{
    assert(mutex->initialized);
}

uint32_t time_get_ms(void)
{
    ++time_get_calls;
    return current_time_ms;
}

#ifdef RDPLIB_DEBUG
void dpf(uint32_t filter, const char *fmt, ...)
{
    (void)filter;
    (void)fmt;
}
#endif

uint32_t rdplib_platform_last_system_error(void)
{
    return 0;
}

intptr_t rdplib_platform_serial_create_event(void)
{
    return fail_event_creation ? 0 : next_event++;
}

int rdplib_platform_serial_close_event(intptr_t event)
{
    assert(event != 0);
    ++close_calls;
    return 1;
}

int rdplib_platform_serial_write(intptr_t endpoint, rdplib_platform_serial_async_t *async_state, const void *data, uint32_t bytes, uint32_t *bytes_written, uint32_t *error_code)
{
    (void)async_state;
    assert(endpoint != -1);
    assert(bytes <= sizeof(captured_frame));
    memcpy(captured_frame, data, bytes);
    captured_frame_size = bytes;
    pending_write_size = bytes;
    ++write_calls;

    switch (write_mode)
    {
    case WRITE_IMMEDIATE_FULL:
        *bytes_written = bytes;
        *error_code = 0;
        return 1;
    case WRITE_IMMEDIATE_SHORT:
        *bytes_written = bytes ? bytes - 1 : 0;
        *error_code = 0;
        return 1;
    case WRITE_PENDING:
        *bytes_written = 0;
        *error_code = TEST_ERROR_IO_PENDING;
        return 0;
    case WRITE_RESOURCE_INVALID_BUFFER:
        *bytes_written = 0;
        *error_code = TEST_ERROR_INVALID_USER_BUFFER;
        return 0;
    case WRITE_RESOURCE_NO_MEMORY:
        *bytes_written = 0;
        *error_code = TEST_ERROR_NOT_ENOUGH_MEMORY;
        return 0;
    default:
        *bytes_written = 0;
        *error_code = 123;
        return 0;
    }
}

int rdplib_platform_serial_get_write_result(intptr_t endpoint, rdplib_platform_serial_async_t *async_state, int wait, uint32_t *bytes_written, uint32_t *error_code)
{
    (void)async_state;
    assert(endpoint != -1);
    ++completion_calls;
    last_completion_wait = wait;

    switch (completion_mode)
    {
    case COMPLETION_FULL:
        *bytes_written = pending_write_size;
        *error_code = 0;
        return 1;
    case COMPLETION_SHORT:
        *bytes_written = pending_write_size ? pending_write_size - 1 : 0;
        *error_code = 0;
        return 1;
    case COMPLETION_INCOMPLETE:
        *bytes_written = 0;
        *error_code = TEST_ERROR_IO_INCOMPLETE;
        return 0;
    default:
        *bytes_written = 0;
        *error_code = 321;
        return 0;
    }
}

void *fast_malloc(uint32_t size)
{
    void *allocation;

    if (fail_fast_allocation)
    {
        return NULL;
    }
    allocation = malloc(size);

    if (allocation)
    {
        ++fast_allocations;
    }
    return allocation;
}

void fast_free(void *ptr)
{
    assert(ptr != NULL);
    ++fast_frees;
    free(ptr);
}

void serial_rx_init(serial_rx_t *serial_rx)
{
    memset(serial_rx, 0, sizeof(*serial_rx));
}

uint32_t serial_rx_create(serial_rx_t *serial_rx, uint32_t size)
{
    serial_rx->head = fail_receive_allocation ? NULL : (char *)malloc(size);
    if (!serial_rx->head)
    {
        return 2;
    }
    serial_rx->max_size = size;
    serial_rx->read_pos = serial_rx->head;
    serial_rx->write_pos = serial_rx->head;
    return 0;
}

void serial_rx_destroy(serial_rx_t *serial_rx)
{
    free(serial_rx->head);
    serial_rx->head = NULL;
}

void serial_rx_fill(serial_rx_t *serial_rx, void *file)
{
    (void)serial_rx;
    (void)file;
}

void serial_rx_read(serial_rx_t *serial_rx, void *buffer, uint32_t size)
{
    uint8_t *output = (uint8_t *)buffer;

    assert(size <= serial_rx->size);
    while (size != 0)
    {
        uint32_t span = (uint32_t)((serial_rx->head + serial_rx->max_size) - serial_rx->read_pos);

        if (span > size)
        {
            span = size;
        }
        if (output)
        {
            memcpy(output, serial_rx->read_pos, span);
            output += span;
        }
        serial_rx->read_pos += span;
        if (serial_rx->read_pos == serial_rx->head + serial_rx->max_size)
        {
            serial_rx->read_pos = serial_rx->head;
        }
        serial_rx->size -= span;
        size -= span;
    }
}

void serial_rx_peek(serial_rx_t *serial_rx, void *buffer, uint32_t size)
{
    char *saved_read = serial_rx->read_pos;
    uint32_t saved_size = serial_rx->size;

    serial_rx_read(serial_rx, buffer, size);
    serial_rx->read_pos = saved_read;
    serial_rx->size = saved_size;
}

static void initialize_serial(serial_t *serial, uint16_t port)
{
    memset(serial, 0xA5, sizeof(*serial));
    serial->time_next_recv = UINT32_C(0x11223344);
    serial->local_port = UINT16_C(0x5566);
    serial_init(serial);
    assert(serial_create(serial, port) == 0);
    serial->file = (void *)(intptr_t)7;
}

static serial_tx_buf_t *allocate_tx(uint32_t write_size)
{
    serial_tx_buf_t *tx_buf = (serial_tx_buf_t *)fast_malloc((uint32_t)sizeof(*tx_buf) + write_size);

    assert(tx_buf != NULL);
    memset(tx_buf, 0, sizeof(*tx_buf) + write_size);
    rdplib_platform_serial_async_set_event(&tx_buf->o, next_event++);
    tx_buf->write_size = write_size;
    memset(tx_buf + 1, 0x7c, write_size);
    return tx_buf;
}

static uint32_t make_frame(frame_storage_t *frame, uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint16_t payload_size)
{
    uint8_t *header = frame->bytes + TEST_COOKIE_SIZE;
    uint32_t total = TEST_COOKIE_SIZE + TEST_HEADER_SIZE + payload_size;

    assert(payload_size <= TEST_MAX_PAYLOAD);
    memcpy(frame->bytes, test_cookie, TEST_COOKIE_SIZE);
    store_u16(header + 0, src_port);
    store_u16(header + 2, dst_port);
    store_u16(header + 4, payload_size);
    store_u16(header + 6, 0);
    if (payload_size)
    {
        memcpy(header + TEST_HEADER_SIZE, payload, payload_size);
    }
    store_u16(header + 6, internet_checksum((uint16_t *)header, (uint16_t)(TEST_HEADER_SIZE + payload_size)));
    assert(internet_checksum((uint16_t *)header, (uint16_t)(TEST_HEADER_SIZE + payload_size)) == 0);
    return total;
}

static void feed_rx(serial_t *serial, const void *data, uint32_t size)
{
    assert(size <= serial->serial_rx.max_size);
    assert(serial->serial_rx.size == 0);
    memcpy(serial->serial_rx.head, data, size);
    serial->serial_rx.read_pos = serial->serial_rx.head;
    serial->serial_rx.write_pos = serial->serial_rx.head + size;
    serial->serial_rx.size = size;
}

static int tx_bufq_test_keycmp(const void *key_1, const void *key_2)
{
    return key_1 != key_2;
}

static void test_tx_bufq_helpers(void)
{
    struct
    {
        uint32_t before;
        tx_bufq_t queue;
        uint32_t after;
    } guarded;
    tx_bufq_t *queue;
    serial_tx_buf_t first;
    serial_tx_buf_t second;

    memset(&guarded, 0xA5, sizeof(guarded));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    guarded.before = UINT32_C(0x13579bdf);
    guarded.after = UINT32_C(0x2468ace0);
    queue = &guarded.queue;

    tx_bufq_init(queue);
    assert(queue->list.head == NULL && queue->list.tail == NULL && queue->list.size == 0 && queue->list.sorted == 0 && queue->list.keycmp == NULL && queue->bytes == 0);
    assert(guarded.before == UINT32_C(0x13579bdf) && guarded.after == UINT32_C(0x2468ace0));

    queue->list.head = &first.link;
    queue->list.tail = &second.link;
    queue->list.size = 2;
    queue->list.sorted = 1;
    queue->list.keycmp = tx_bufq_test_keycmp;
    queue->bytes = UINT32_C(0x11223344);
    tx_bufq_create(queue);
    assert(queue->list.head == &first.link && queue->list.tail == &second.link && queue->list.size == 2 && queue->bytes == UINT32_C(0x11223344));
    assert(queue->list.sorted == 0 && queue->list.keycmp == NULL);
    assert(guarded.before == UINT32_C(0x13579bdf) && guarded.after == UINT32_C(0x2468ace0));

    tx_bufq_init(queue);
    tx_bufq_create(queue);
    assert(tx_bufq_peek_head(queue) == NULL);
    assert(tx_bufq_get_bytes(queue) == 0);
    assert(tx_bufq_get_size(queue) == 0);

    first.link.item = &first;
    second.link.item = &second;
    first.write_size = 11;
    second.write_size = 17;
    first.start_time = UINT32_C(0x55667788);
    second.start_time = UINT32_C(0x99aabbcc);
    tx_bufq_add_tail(queue, &first);
    tx_bufq_add_tail(queue, &second);
    assert(tx_bufq_peek_head(queue) == &first);
    assert(tx_bufq_get_bytes(queue) == 28);
    assert(tx_bufq_get_size(queue) == 2);
    assert(first.start_time == UINT32_C(0x55667788) && second.start_time == UINT32_C(0x99aabbcc));
    assert(tx_bufq_remove_head(queue) == &first);
    assert(tx_bufq_get_bytes(queue) == 17);
    assert(tx_bufq_remove_head(queue) == &second);
    assert(tx_bufq_get_bytes(queue) == 0);
    assert(tx_bufq_get_size(queue) == 0);

    first.write_size = UINT32_MAX - 4u;
    second.write_size = 5;
    tx_bufq_add_tail(queue, &first);
    assert(tx_bufq_get_bytes(queue) == UINT32_MAX - 4u);
    tx_bufq_add_tail(queue, &second);
    assert(tx_bufq_get_bytes(queue) == 0);
    assert(tx_bufq_remove_head(queue) == &first && tx_bufq_get_bytes(queue) == 5);
    assert(tx_bufq_remove_head(queue) == &second && tx_bufq_get_bytes(queue) == 0);

    queue->bytes = UINT32_C(0x11223344);
    tx_bufq_destroy(queue);
    assert(queue->bytes == UINT32_C(0x11223344));
    assert(queue->list.head == NULL && queue->list.tail == NULL && queue->list.size == 0);
    assert(guarded.before == UINT32_C(0x13579bdf) && guarded.after == UINT32_C(0x2468ace0));
}

static void test_checksum(void)
{
    uint16_t words[5] = {UINT16_C(0x1234), UINT16_C(0xabcd), UINT16_C(0x0102), UINT16_C(0x0003), 0};
    uint16_t first_word;
    uint32_t odd_sum;
    union
    {
        uint16_t alignment;
        uint8_t bytes[3];
    } odd = {0};

    words[4] = internet_checksum(words, 8);
    assert(internet_checksum(words, 10) == 0);
    assert(internet_checksum(words, 0) == UINT16_MAX);
    odd.bytes[0] = 0x34;
    odd.bytes[1] = 0x12;
    odd.bytes[2] = 0x56;
    memcpy(&first_word, odd.bytes, sizeof(first_word));
    odd_sum = first_word + odd.bytes[2];
    odd_sum = (odd_sum & UINT32_C(0xffff)) + (odd_sum >> 16);
    odd_sum += odd_sum >> 16;
    assert(internet_checksum((uint16_t *)odd.bytes, 3) == (uint16_t)~odd_sum);
}

static void test_init_create_destroy(void)
{
    serial_t serial;

    memset(&serial, 0xA5, sizeof(serial));
    serial.time_next_recv = UINT32_C(0x11223344);
    serial.local_port = UINT16_C(0x5566);
    serial_init(&serial);
    assert((intptr_t)serial.file == -1);
    assert(serial.rx_state == 0);
    assert(serial.time_next_recv == UINT32_C(0x11223344));
    assert(serial.local_port == UINT16_C(0x5566));
    assert(serial.tx_in_progress.list.head == NULL && serial.tx_in_progress.bytes == 0);
    assert(bytes_are_zero(&serial.serial_rx, sizeof(serial.serial_rx)));
    assert(bytes_are_zero(&serial.stats, sizeof(serial.stats)));

    fail_receive_allocation = 0;
    assert(serial_create(&serial, UINT16_C(0x2244)) == 0);
#ifdef _WIN32
#ifdef RDPLIB_DEBUG
    assert(serial.lock.owner == NULL);
#endif
#else
    assert(serial.lock.platform.initialized);
#ifdef RDPLIB_DEBUG
    assert(!serial.lock.owned);
#endif
#endif
    assert(serial.local_port == UINT16_C(0x2244));
    assert(serial.serial_rx.head != NULL && serial.serial_rx.max_size == 8000);
    serial_set_time_next_recv(&serial, UINT32_C(0xaabbccdd));
    assert(serial_get_time_next_recv(&serial) == UINT32_C(0xaabbccdd));
    serial_destroy(&serial);
#ifdef _WIN32
#ifdef RDPLIB_DEBUG
    assert(serial.lock.owner == NULL);
#endif
#else
    assert(!serial.lock.platform.initialized);
#ifdef RDPLIB_DEBUG
    assert(!serial.lock.owned);
#endif
#endif
    assert(serial.serial_rx.head == NULL);

    serial_init(&serial);
    fail_receive_allocation = 1;
    assert(serial_create(&serial, 9) == 2);
#ifdef _WIN32
#ifdef RDPLIB_DEBUG
    assert(serial.lock.owner == NULL);
#endif
#else
    assert(!serial.lock.platform.initialized);
#ifdef RDPLIB_DEBUG
    assert(!serial.lock.owned);
#endif
#endif
    assert(serial.serial_rx.head == NULL);
    fail_receive_allocation = 0;
}

static void assert_captured_frame(uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint16_t payload_size)
{
    const uint8_t *header = captured_frame + TEST_COOKIE_SIZE;

    assert(captured_frame_size == TEST_COOKIE_SIZE + TEST_HEADER_SIZE + payload_size);
    assert(memcmp(captured_frame, test_cookie, TEST_COOKIE_SIZE) == 0);
    assert(load_u16(header + 0) == src_port);
    assert(load_u16(header + 2) == dst_port);
    assert(load_u16(header + 4) == payload_size);
    assert(internet_checksum((uint16_t *)header, (uint16_t)(TEST_HEADER_SIZE + payload_size)) == 0);
    assert(memcmp(header + TEST_HEADER_SIZE, payload, payload_size) == 0);
}

static void test_send_and_boundaries(void)
{
    serial_t serial;
    sockaddr_com destination;
    uint8_t first[] = {1, 2, 3};
    uint8_t second[] = {4, 5};
    uint8_t boundary[TEST_MAX_PAYLOAD + 1];
    iov_t iov[2];
#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    uint32_t allocations_before;
    uint32_t frees_before;
#endif

    initialize_serial(&serial, UINT16_C(0x1234));
    memset(&destination, 0, sizeof(destination));
    destination.scom_family = 69;
    destination.scom_port = (int16_t)UINT16_C(0x5678);
    reset_io();
    iov[0].data = first;
    iov[0].size = sizeof(first);
    iov[1].data = second;
    iov[1].size = sizeof(second);
    assert(serial_send(&serial, iov, 2, &destination) == 0);
    assert(write_calls == 1 && close_calls == 1);
    {
        const uint8_t expected[] = {1, 2, 3, 4, 5};
        assert_captured_frame(UINT16_C(0x1234), UINT16_C(0x5678), expected, sizeof(expected));
    }

    reset_io();
    assert(serial_send(&serial, NULL, 0, &destination) == 0);
    assert_captured_frame(UINT16_C(0x1234), UINT16_C(0x5678), boundary, 0);

    reset_io();
    fail_event_creation = 1;
    assert(serial_send(&serial, iov, 2, &destination) == 3);
    assert(write_calls == 0 && close_calls == 0);

    reset_io();
    fail_fast_allocation = 1;
    assert(serial_send(&serial, iov, 2, &destination) == 2);
    assert(write_calls == 0 && close_calls == 0);
    fail_fast_allocation = 0;

    memset(boundary, 0x5a, sizeof(boundary));
    iov[0].data = boundary;
    iov[0].size = TEST_MAX_PAYLOAD - 1;
    reset_io();
    assert(serial_send(&serial, iov, 1, &destination) == 0);
    assert_captured_frame(UINT16_C(0x1234), UINT16_C(0x5678), boundary, TEST_MAX_PAYLOAD - 1);

    iov[0].size = TEST_MAX_PAYLOAD;
    reset_io();
    assert(serial_send(&serial, iov, 1, &destination) == 0);
    assert_captured_frame(UINT16_C(0x1234), UINT16_C(0x5678), boundary, TEST_MAX_PAYLOAD);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    iov[0].data = NULL;
    iov[0].size = 0;
    reset_io();
    assert(serial_send(&serial, iov, 1, &destination) == 0);
    assert_captured_frame(UINT16_C(0x1234), UINT16_C(0x5678), boundary, 0);

    iov[0].data = boundary;
    iov[0].size = TEST_MAX_PAYLOAD + 1;
    reset_io();
    allocations_before = fast_allocations;
    frees_before = fast_frees;
    assert(serial_send(&serial, iov, 1, &destination) == TEST_CAPACITY_EXCEEDED);
    assert(write_calls == 0 && fast_allocations == allocations_before && fast_frees == frees_before);
    assert(tx_bufq_get_size(&serial.tx_in_progress) == 0 && tx_bufq_get_bytes(&serial.tx_in_progress) == 0);
    assert(serial_send(&serial, NULL, 1, &destination) == TEST_INVALID_ARGUMENT);
    assert(serial_send(&serial, iov, 0, NULL) == TEST_INVALID_ARGUMENT);
    iov[0].data = NULL;
    iov[0].size = 1;
    assert(serial_send(&serial, iov, 1, &destination) == TEST_INVALID_ARGUMENT);
#endif

    serial_destroy(&serial);
}

static void test_receive_boundaries_resync_and_checksum(void)
{
    serial_t serial;
    sockaddr_com source;
    frame_storage_t frame;
    uint8_t payload[TEST_MAX_PAYLOAD];
    uint8_t output[TEST_MAX_PAYLOAD];
    uint32_t frame_size;

    memset(payload, 0x6b, sizeof(payload));
    initialize_serial(&serial, UINT16_C(0x3344));

    frame_size = make_frame(&frame, UINT16_C(0x7788), UINT16_C(0x3344), payload, TEST_MAX_PAYLOAD);
    feed_rx(&serial, frame.bytes, frame_size);
    memset(output, 0, sizeof(output));
    memset(&source, 0xA5, sizeof(source));
    assert(serial_recv_from(&serial, (char *)output, sizeof(output), &source) == TEST_MAX_PAYLOAD);
    assert(memcmp(output, payload, sizeof(payload)) == 0);
    assert(source.scom_family == 69 && (uint16_t)source.scom_port == UINT16_C(0x7788));
    assert(bytes_are_zero(source.scom_zero, sizeof(source.scom_zero)));
    assert(serial.stats.bytes_accepted == frame_size);
    assert(serial.rx_state == 0 && serial.serial_rx.size == 0);

    frame_size = make_frame(&frame, UINT16_C(0x7788), 0, payload, 3);
    feed_rx(&serial, frame.bytes, frame_size);
    assert(serial_recv_from(&serial, (char *)output, sizeof(output), &source) == 3);
    assert(memcmp(output, payload, 3) == 0);

    frame_size = make_frame(&frame, UINT16_C(0x7788), UINT16_C(0x3344), payload, 0);
    feed_rx(&serial, frame.bytes, frame_size);
    assert(serial_recv_from(&serial, (char *)output, 0, &source) == 0);
    assert(source.scom_family == 69 && (uint16_t)source.scom_port == UINT16_C(0x7788));

    memcpy(frame.bytes, test_cookie, TEST_COOKIE_SIZE);
    store_u16(frame.bytes + TEST_COOKIE_SIZE + 0, 7);
    store_u16(frame.bytes + TEST_COOKIE_SIZE + 2, UINT16_C(0x3344));
    store_u16(frame.bytes + TEST_COOKIE_SIZE + 4, TEST_MAX_PAYLOAD + 1);
    store_u16(frame.bytes + TEST_COOKIE_SIZE + 6, 0);
    feed_rx(&serial, frame.bytes, TEST_COOKIE_SIZE + TEST_HEADER_SIZE);
    assert(serial_recv_from(&serial, (char *)output, sizeof(output), &source) == -1);
    assert(serial.stats.bad_header_size == 1 && serial.rx_state == 0);
    serial_rx_read(&serial.serial_rx, NULL, serial.serial_rx.size);

    frame_size = make_frame(&frame, 8, UINT16_C(0x9999), payload, 0);
    feed_rx(&serial, frame.bytes, frame_size);
    assert(serial_recv_from(&serial, (char *)output, sizeof(output), &source) == -1);
    assert(serial.stats.wrong_rdp == 1 && serial.rx_state == 0);
    serial_rx_read(&serial.serial_rx, NULL, serial.serial_rx.size);

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    serial.stats.bytes_accepted = 0;
    frame_size = make_frame(&frame, 4, UINT16_C(0x3344), payload, 4);
    feed_rx(&serial, frame.bytes, frame_size);
    memset(output, 0xA5, sizeof(output));
    assert(serial_recv_from(&serial, (char *)output, 3, &source) == -1);
    assert(serial.serial_rx.size == 0 && serial.rx_state == 0);
    assert(serial.stats.bytes_accepted == 0);
    assert(output[0] == 0xA5);

    frame_size = make_frame(&frame, 4, UINT16_C(0x3344), payload, 4);
    feed_rx(&serial, frame.bytes, frame_size);
    assert(serial_recv_from(&serial, (char *)output, sizeof(output), NULL) == -1);
    assert(serial.serial_rx.size == 0 && serial.rx_state == 0);
    assert(serial.stats.bytes_accepted == 0);

    frame_size = make_frame(&frame, 4, UINT16_C(0x3344), payload, 4);
    feed_rx(&serial, frame.bytes, frame_size);
    serial.rx_state = 37;
    assert(serial_recv_from(&serial, (char *)output, sizeof(output), &source) == 4);
    assert(serial.serial_rx.size == 0 && serial.rx_state == 0);
#endif

    serial.stats.bytes_discarded = 0;
    frame_size = make_frame(&frame, 5, UINT16_C(0x3344), payload, 5);
    memmove(frame.bytes + 1, frame.bytes, frame_size);
    frame.bytes[0] = 0xee;
    feed_rx(&serial, frame.bytes, frame_size + 1);
    assert(serial_recv_from(&serial, (char *)output, sizeof(output), &source) == 5);
    assert(serial.stats.bytes_discarded == 1);
    assert(memcmp(output, payload, 5) == 0);

    frame_size = make_frame(&frame, 6, UINT16_C(0x3344), payload, 6);
    frame.bytes[TEST_COOKIE_SIZE + TEST_HEADER_SIZE] ^= 0x80;
    feed_rx(&serial, frame.bytes, frame_size);
    assert(serial_recv_from(&serial, (char *)output, sizeof(output), &source) == -1);
    assert(serial.stats.bad_checksum == 1);
    assert(serial.rx_state == 0);

    serial_destroy(&serial);
}

static void test_write_and_retire_paths(void)
{
    serial_t serial;
    serial_tx_buf_t *tx_buf;
    uint32_t alloc_before;
    uint32_t free_before;

    initialize_serial(&serial, 1);
    reset_io();
    alloc_before = fast_allocations;
    free_before = fast_frees;
    tx_buf = allocate_tx(9);
    write_mode = WRITE_IMMEDIATE_FULL;
    assert(serial_tx_write(&serial, tx_buf) == 0);
    assert(serial.stats.instant_writes == 1 && fast_allocations == alloc_before + 1 && fast_frees == free_before + 1 && close_calls == 1);

    tx_buf = allocate_tx(9);
    write_mode = WRITE_IMMEDIATE_SHORT;
    assert(serial_tx_write(&serial, tx_buf) == 0);
    assert(serial.stats.partial_writes == 1);

    current_time_ms = 50;
    tx_buf = allocate_tx(12);
    write_mode = WRITE_PENDING;
    assert(serial_tx_write(&serial, tx_buf) == 0);
    assert(tx_bufq_peek_head(&serial.tx_in_progress) == tx_buf);
    assert(tx_bufq_get_bytes(&serial.tx_in_progress) == 12 && tx_buf->start_time == 50);
    completion_mode = COMPLETION_FULL;
    serial_retire_overlapped_writes(&serial, 0);
    assert(tx_bufq_get_size(&serial.tx_in_progress) == 0 && serial.stats.complete_writes == 1);

    tx_buf = allocate_tx(12);
    write_mode = WRITE_PENDING;
    assert(serial_tx_write(&serial, tx_buf) == 0);
    completion_mode = COMPLETION_SHORT;
    serial_retire_overlapped_writes(&serial, 0);
    assert(tx_bufq_get_size(&serial.tx_in_progress) == 0 && serial.stats.incomplete_writes == 1);

    tx_buf = allocate_tx(13);
    write_mode = WRITE_PENDING;
    assert(serial_tx_write(&serial, tx_buf) == 0);
    completion_mode = COMPLETION_INCOMPLETE;
    serial_retire_overlapped_writes(&serial, 0);
    assert(tx_bufq_peek_head(&serial.tx_in_progress) == tx_buf);
    completion_mode = COMPLETION_ERROR;
    serial_retire_overlapped_writes(&serial, 0);
    assert(tx_bufq_get_size(&serial.tx_in_progress) == 0 && serial.stats.failed_writes == 1);

    tx_buf = allocate_tx(7);
    write_mode = WRITE_RESOURCE_INVALID_BUFFER;
    assert(serial_tx_write(&serial, tx_buf) == 5);
    tx_buf = allocate_tx(7);
    write_mode = WRITE_RESOURCE_NO_MEMORY;
    assert(serial_tx_write(&serial, tx_buf) == 5);
    assert(serial.stats.try_again == 2);

    tx_buf = allocate_tx(7);
    write_mode = WRITE_FATAL;
    assert(serial_tx_write(&serial, tx_buf) == 1);
    assert(serial.stats.failed_writes == 2 && serial.stats.fail_error == 123 && (intptr_t)serial.file == -1);
    serial.file = (void *)(intptr_t)7;
    serial_destroy(&serial);

#ifdef _WIN32
    initialize_serial(&serial, 2);
    reset_io();
    tx_buf = allocate_tx(15);
    write_mode = WRITE_PENDING;
    assert(serial_tx_write(&serial, tx_buf) == 0);
    completion_mode = COMPLETION_FULL;
    serial_destroy(&serial);
    assert(completion_calls == 1 && last_completion_wait == 1 && close_calls == 1);
#endif
}

static void test_ready_time_empty_and_stall(void)
{
    serial_t serial;
    serial_tx_buf_t first;
    serial_tx_buf_t second;
    uint32_t calls_before;

    initialize_serial(&serial, 1);
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    first.link.item = &first;
    second.link.item = &second;
    first.write_size = 1000;
    first.start_time = 70;
    second.write_size = 2000;
    second.start_time = 80;

    assert(serial_tx_ready(&serial) == 1);
    tx_bufq_add_tail(&serial.tx_in_progress, &first);
    assert(serial_tx_ready(&serial) == 1);
    tx_bufq_add_tail(&serial.tx_in_progress, &second);
    assert(serial_tx_ready(&serial) == 0);

    current_time_ms = 100;
    time_get_calls = 0;
    assert(serial_get_time_empty(&serial) == 118);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(time_get_calls == 2);
#else
    assert(time_get_calls == 1);
#endif
    assert(serial_get_stall_time(&serial) == 12);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(time_get_calls == 3);
#else
    assert(time_get_calls == 2);
#endif

    serial.tx_in_progress.bytes = first.write_size + UINT32_C(0x20000000);
    current_time_ms = 123;
    assert(serial_get_time_empty(&serial) == 123u + ((UINT32_C(0x20000000) * UINT32_C(9000)) / UINT32_C(1000000)));
    serial.tx_in_progress.bytes = first.write_size + second.write_size;
    assert(tx_bufq_remove_head(&serial.tx_in_progress) == &first);
    assert(tx_bufq_remove_head(&serial.tx_in_progress) == &second);

    calls_before = time_get_calls;
    serial_retire_overlapped_writes(&serial, 0);
#if defined(RDPLIB_TEST_SOURCE_FAITHFUL) || defined(RDP_DEAD_CODE)
    assert(time_get_calls == calls_before + 1);
#else
    assert(time_get_calls == calls_before);
#endif

#ifndef RDPLIB_TEST_SOURCE_FAITHFUL
    serial.tx_in_progress.bytes = 1;
    current_time_ms = UINT32_C(0xaabbccdd);
    assert(serial_get_time_empty(&serial) == UINT32_C(0xaabbccdd));
    serial.tx_in_progress.bytes = 0;
#endif
    serial_destroy(&serial);
}

#ifdef RDP_DEAD_CODE
static void test_dead_stats_snapshot(void)
{
    serial_t serial;
    serial_stat_t stats;
    serial_tx_buf_t *tx_buf;

    initialize_serial(&serial, 1);
    current_time_ms = 80;
    serial_retire_overlapped_writes(&serial, 0);
    current_time_ms = 90;
    write_mode = WRITE_PENDING;
    tx_buf = allocate_tx(21);
    assert(serial_tx_write(&serial, tx_buf) == 0);
    current_time_ms = 105;
    memset(&stats, 0, sizeof(stats));
    serial_get_stats(&serial, &stats);
    assert(stats.time_since_retire == 25);
    assert(stats.tx_operations == 1 && stats.tx_bytes == 21 && stats.tx_age == 15);
    completion_mode = COMPLETION_FULL;
    serial_retire_overlapped_writes(&serial, 0);
    serial_destroy(&serial);
}
#endif

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_tx_bufq_helpers();
    test_checksum();
    test_init_create_destroy();
    test_send_and_boundaries();
    test_receive_boundaries_resync_and_checksum();
    test_write_and_retire_paths();
    test_ready_time_empty_and_stall();
#ifdef RDP_DEAD_CODE
    test_dead_stats_snapshot();
#endif
    assert(fast_allocations == fast_frees);
    return 0;
}
