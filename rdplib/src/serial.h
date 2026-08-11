// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Serial framing and counters shared by the platform backends.
#ifndef RDP_SERIAL_H
#define RDP_SERIAL_H

#include <stdint.h>

#include "container.h"
#include "rdplib_platform.h"

typedef struct serial_frame_header_t
{
    char magic[18];
    uint16_t local_port;
    uint16_t remote_port;
    uint16_t payload_bytes;
    uint16_t checksum;
} serial_frame_header_t;

typedef struct serial_statistics_t
{
    uint32_t reserved[4];
    uint32_t oversize_frame_count;
    uint32_t checksum_failure_count;
    uint32_t port_mismatch_count;
    uint32_t received_frame_bytes;
    uint32_t sync_discarded_bytes;
    uint32_t async_short_write_count;
    uint32_t async_complete_write_count;
    uint32_t immediate_complete_write_count;
    uint32_t write_error_count;
    uint32_t last_write_error;
    uint32_t immediate_short_write_count;
    uint32_t write_resource_error_count;
} serial_statistics_t;

typedef struct serial_rx_t
{
    uint8_t *buffer;
    uint32_t capacity;
    uint8_t *read_cursor;
    uint8_t *write_cursor;
    uint32_t used_bytes;
} serial_rx_t;

typedef struct serial_tx_buf_t
{
    rdp_list_link_t link;
    rdplib_platform_serial_async_t platform_async_state; // Native equivalent of the embedded 32 bit OVERLAPPED.
    uint32_t write_bytes;
    uint32_t write_start_time_ms;
} serial_tx_buf_t;

static inline uint8_t *serial_tx_data(serial_tx_buf_t *buffer)
{
    return (uint8_t *)buffer + sizeof(*buffer);
}

typedef struct rdp_serial_buffer_t
{
    const void *data;
    uint32_t bytes;
} rdp_serial_buffer_t;

typedef struct serial_t
{
    rdplib_platform_mutex_t lock;
    uint32_t next_receive_time_ms;
    intptr_t endpoint;
    uint16_t local_port;
    serial_rx_t receive_buffer;
    rdp_list_t pending_sends;
    uint32_t queued_send_bytes;
    serial_statistics_t statistics;
    uint32_t receive_sync_state;
} serial_t;

enum
{
    RDP_SERIAL_RECEIVE_CAPACITY = 8000,
    RDPLIB_PLATFORM_ERROR_NOT_ENOUGH_MEMORY = 8,
    RDPLIB_PLATFORM_ERROR_IO_INCOMPLETE = 996,
    RDPLIB_PLATFORM_ERROR_IO_PENDING = 997,
    RDPLIB_PLATFORM_ERROR_INVALID_USER_BUFFER = 1784
};

extern uint32_t g_rdp_serial_activity_time_ms;

static inline uint8_t *serial_frame_payload(serial_frame_header_t *frame)
{
    return (uint8_t *)(frame + 1);
}

#ifdef __cplusplus
extern "C"
{
#endif

// Multiplication intentionally wraps at 32 bits before division.
uint32_t serial_calculate_transmit_time_ms(uint32_t byte_count);
uint32_t serial_calculate_stall_time_ms(uint32_t current_time_ms, uint32_t write_start_time_ms, uint32_t write_bytes);

// A nonzero queued byte count requires a valid active write and consistent accounting.
uint32_t serial_calculate_time_empty_ms(uint32_t current_time_ms, uint32_t queued_send_bytes, uint32_t active_write_bytes);
int serial_pending_send_count_is_ready(uint32_t pending_send_count);

// The buffer must satisfy the clients' native uint16_t alignment assumption.
uint16_t serial_calculate_frame_checksum(const void *data, uint16_t byte_count);

// Initializes the source owned fields without creating the platform lock or
// receive allocation. next_receive_time_ms and local_port are
// untouched until their separate create/scheduler operations own them.
void serial_init(serial_t *serial);

// Creates the unsorted pending send list and lock, publishes local_port, and
// allocates the client's fixed 8000 byte receive buffer. Returns 2 only
// when that allocation fails; the partial lock/list ownership is unwound.
int serial_create(serial_t *serial, uint16_t local_port);

// Starts an asynchronous serial write and consumes buffer ownership. An
// immediate completion or error closes its event and frees it; ERROR_IO_PENDING
// publishes it on pending_sends. Returns 0, 1, or 5 exactly as the clients do.
//
// The caller must hold the serial lock. buffer must be a fast allocation with
// write_bytes readable bytes immediately after its prefix and a valid
// write event. Queue byte accounting and endpoint validity are unchecked.
int serial_tx_write(serial_t *serial, serial_tx_buf_t *buffer);

// Retires the completed prefix of the Windows asynchronous write queue. A
// successful exact/short completion updates its separate counter. Error
// IO_INCOMPLETE leaves the head and every later write in place; every other
// failure increments write_error_count and retires the head without storing
// the error code.
//
// The caller must hold the serial lock. pending_sends membership, link values,
// queued_send_bytes, write events, and platform asynchronous state are trusted.
void serial_retire_pending_writes_windows_locked(serial_t *serial, int wait);

// Destroys the common Mac serial owners. The 2 Mac builds do not inspect or
// release pending write records before destroying the non owning list.
void serial_destroy_mac(serial_t *serial);

// Performs the TAKP Windows blocking completion pass and then destroys the
// common serial owners. ERROR_IO_INCOMPLETE still stops the pass, after which
// list destruction does not release the remaining writes.
void serial_destroy_windows(serial_t *serial);

// Builds and submits a native endian serial frame using the Mac entry path.
// The caller's scatter entries and remote sockaddr are trusted and borrowed
// only for the duration of the call. Result codes are 0 success, 1 write
// error, 2 allocation failure, 3 event failure, and 5 write resource failure.
// The 32 bit payload sum and 16 bit frame length/checksum range are not
// validated; oversized input can wrap or describe only a prefix of the bytes
// actually copied and submitted.
int serial_send_mac(serial_t *serial, const rdp_serial_buffer_t *buffers, uint32_t buffer_count, const uint8_t remote_address[16]);

// Performs the nonblocking Windows completion prefix, then builds and submits
// the same frame as serial_send_mac.
int serial_send_windows(serial_t *serial, const rdp_serial_buffer_t *buffers, uint32_t buffer_count, const uint8_t remote_address[16]);

// Calls the Mac receive fill backend and parses at most a single complete serial
// frame. Both Mac clients compile the fill backend as a warning stub, but the
// common parser still runs against any bytes already present in the ring.
int serial_recv_from_mac(serial_t *serial, void *destination, uint32_t destination_capacity, uint8_t source_address[16]);

// Retires completed Windows writes, fills the circular input, and runs the
// same parser as serial_recv_from_mac.
int serial_recv_from_windows(serial_t *serial, void *destination, uint32_t destination_capacity, uint8_t source_address[16]);

// Fills the free spans of the Windows circular receive buffer. A pending read
// is waited synchronously; an immediate short read stops the pass, while an
// exact span read continues into a wrapped free span. The caller must hold
// the serial lock and supply a valid endpoint.
void serial_rx_fill_windows(serial_rx_t *receive, intptr_t endpoint);

void serial_rx_init(serial_rx_t *receive);

// Allocates the circular store. serial_rx_init must have run first because
// allocation failure leaves every field except buffer unchanged. The client
// returns 2, rather than 1, when allocation fails.
int serial_rx_create(serial_rx_t *receive, uint32_t capacity);

// Releases only the byte store and clears only buffer. The remaining cursor,
// capacity, and byte count fields retain their old values.
void serial_rx_destroy(serial_rx_t *receive);

// Copies exactly byte_count bytes without consuming them. The caller must
// prove that many bytes are buffered and must provide a valid destination.
void serial_rx_peek(const serial_rx_t *receive, void *destination, uint32_t byte_count);

// Consumes exactly byte_count bytes. A null destination discards them. The
// caller must prove availability; an empty buffer with a nonzero request can
// make the original loop stop making progress.
void serial_rx_read(serial_rx_t *receive, void *destination, uint32_t byte_count);

void serial_set_time_next_recv(serial_t *serial, uint32_t next_receive_time_ms);
uint32_t serial_get_time_next_recv(serial_t *serial);
int serial_tx_ready(serial_t *serial);

// Returns the estimated completion time for bytes waiting behind the active
// asynchronous write. A nonzero byte total requires a valid pending head and
// consistent byte accounting; the clients do not check either invariant.
uint32_t serial_get_time_empty(serial_t *serial);

// Returns time beyond 2 times the active write's expected duration, or 0.
uint32_t serial_get_stall_time(serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* RDP_SERIAL_H */
