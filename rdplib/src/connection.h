// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Recovered connection state using host pointers and a platform mutex.
#ifndef RDPLIB_CONNECTION_H
#define RDPLIB_CONNECTION_H

#include <stdint.h>

#include "container.h"
#include "event.h"
#include "rdplib_constants.h"
#include "rdplib_platform.h"
#include "rx.h"
#include "stats.h"
#include "tx.h"

struct rdp_t;

typedef struct connection_t
{
    uint32_t application_storage[3];
    struct rdp_t *owner;
    rdp_list_link_t connection_hash_link;
    uint32_t reference_count;
    rdp_pqueue_link_t event_link;
    rdp_timeout_data_t event_timeout;
    uint32_t event_type;
    rdplib_platform_mutex_t lock;
    uint32_t linger_active;
    uint32_t linger_deadline_ms;
    uint32_t locally_initiated;
    uint32_t reserved_receive_gate;
    uint32_t options;
    // Used by the normal build.  The source faithful scheduler retains its recovered 10000 ms literal.
    uint32_t rdplib_keepalive_interval_ms;
    // Normal rdplib packet drop state.  The source faithful build retains the layout but does not use it.
    int (*rdplib_packet_drop_callback)(void *context, rdplib_packet_drop_direction_t direction, const uint8_t *packet, uint32_t packet_bytes);
    void *rdplib_packet_drop_context;
    rdp_transmit_initialization_state_t transmit;
    rdp_receive_initialization_state_t receive;
} connection_t;

typedef enum rdp_connection_event_type_t
{
    RDP_CONNECTION_EVENT_NONE = 0,
    RDP_CONNECTION_EVENT_LINGER = 1,
    RDP_CONNECTION_EVENT_TRANSMIT = 2,
    RDP_CONNECTION_EVENT_KEEPALIVE = 3,
    RDP_CONNECTION_EVENT_TRACEROUTE = 4
} rdp_connection_event_type_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Set up the protocol state, lock, and owner links for a peer.
//
// The connection must be writable and this must run exactly once.  The link
// fields are not cleared because their lists overwrite them later, as they do
// in the clients.
void connection_init(connection_t *connection, struct rdp_t *owner, const uint8_t remote_address[16], uint32_t options);

// Create transmit state first and receive state second.  Both functions
// currently return 0, but the order and failure check match the clients.
int connection_create(connection_t *connection);

// Destroy transmit state, receive state, and then the lock.  The connection
// must already be out of the hash and event queue, with no temporary users.
void connection_destroy(connection_t *connection);

// Return the transport connected flag.  Linger and peer STOP are separate
// states and do not change this value.
int connection_connected(const connection_t *connection);

// Return the connection base, which is also the 3 word application
// storage prefix.  The game wrapper uses this to write 0x200 into word 0.
void *connection_app_ptr(connection_t *connection);

// Returns the connection owned 16 byte peer address used as its hash key.
uint8_t *connection_get_remote_addr(connection_t *connection);

// Copy disconnect information only when output_bytes is exactly 12.  A null
// output with that size keeps the original null write bug.
void connection_get_disconnect_info(const connection_t *connection, void *output, uint32_t output_bytes);

// Look the connection up again, copy its receive and transmit statistics, and
// release the temporary locked reference.
//
// The original code reads the supplied connection again after releasing that
// reference to select the serial stall value.  The caller must already own a
// separate reference or this can read a freed connection.
void connection_get_perf_stats(connection_t *connection, rdp_connection_perf_stats_t *statistics);

// Test the linger deadline against the current clock, including the owner's
// 10 ms early scheduling allowance.
int connection_linger_expired(const connection_t *connection);

// Replaces the bandwidth byte rate and returns its previous value.
uint32_t connection_set_max_data_rate(connection_t *connection, uint32_t bytes_per_second);

// Replaces the aggregate byte ceiling across blocked, ready, and sent queues.
void connection_set_send_buffer_size(connection_t *connection, uint32_t bytes);

// Choose the earliest transmit, delayed ACK, keepalive, traceroute, or linger
// deadline.  Ties keep that order.  The connection must be locked.
void connection_recalc_event_timeout(connection_t *connection, rdp_timeout_data_t *timeout);

// Run due connection events until the next deadline is after now_ms.  Linger
// is returned to the endpoint without running it so the endpoint can remove
// the connection from the hash and event queue.
//
// The connection must be locked.  Transmit, keepalive, and traceroute call the
// backend selected by the build.
void connection_event_process(connection_t *connection, uint32_t now_ms, rdp_timeout_data_t *timeout);

// Record a header that already passed parsing and validation.
//
// The connection must be locked and duplicate_reliable must be valid.  RESET
// returns immediately after abort.  STOP is applied before packet sequence and
// ACK state, matching the client receive order.
void connection_record_arrival(connection_t *connection, const _rdp_header_t *header, uint32_t *duplicate_reliable);

// Record a normalized ICMP response.  A traceroute response updates its
// sample.  A normal response updates the diagnostics, and destination or port
// unreachable aborts the connection.
//
// The connection must be locked.  An earlier traceroute sample may be updated
// more than once.  source_address must contain a complete sockaddr with its
// IPv4 address at byte 4.
void connection_handle_icmp(connection_t *connection, uint8_t type, uint8_t code, uint8_t trace_response, uint8_t trace_sample_index, const uint8_t source_address[16]);

// Decode and validate a connected datagram into an in memory header.
//
// The default build checks each selected field before reading it and checks
// the fragment shape before assembly.  RDPLIB_SOURCE_FAITHFUL keeps the
// recovered read order and assumptions.
rdp_rx_arrival_disposition_t connection_parse_and_validate_arrival(connection_t *connection, const uint8_t *packet, uint16_t packet_bytes, _rdp_header_t *header);

#ifdef __cplusplus
}
#endif

#endif /* RDPLIB_CONNECTION_H */
