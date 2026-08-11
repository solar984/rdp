// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Raw interface to the recovered RDP implementation.
//
// This exposes the recovered structures, functions, global state, and
// platform replacements directly.  Use rdplib.h unless the program needs to
// manage the original interface itself.
#ifndef RDPLIB_RAW_H
#define RDPLIB_RAW_H

#include <stdint.h>

#include "application_receive.h"
#include "bandwidth.h"
#include "bitarray.h"
#include "connection.h"
#include "connhash.h"
#include "container.h"
#include "event.h"
#include "eventq.h"
#include "fast.h"
#include "framing.h"
#include "layout.h"
#include "net_error.h"
#include "packet.h"
#include "queue.h"
#include "rdplib_constants.h"
#include "rdplib_platform.h"
#include "rx.h"
#include "serial.h"
#include "stats.h"
#include "timeout.h"
#include "trace.h"
#include "tx.h"
#include "usend.h"

typedef struct rdp_t
{
    uint32_t sockets_initialized;
    intptr_t ipv4_socket;
    intptr_t icmp_receive_socket;
    intptr_t icmp_probe_socket;
    intptr_t ipx_socket;
    uint8_t ipv4_address[16];
    uint8_t probe_address[16];
    uint8_t ipx_address[16];
    uint32_t probe_socket_default_ttl;
    uint32_t ipx_broadcast_enabled;
    uint32_t ipv4_broadcast_enabled;
    rdp_connhash_t connections;
    rdp_eventq_t events;
    uint32_t io_wakeup_pending;
    uint32_t join_io_thread_on_destroy;
    uint32_t io_thread_running;
    void *io_thread;
    rdp_application_receive_t application_receive;
    uint32_t input_bytes;
    uint32_t duplicate_input_bytes;
    uint32_t rate_sample_time_ms;
    uint32_t input_bytes_per_second;
    uint32_t duplicate_input_bytes_per_second;
    uint32_t use_encryption;
    uint32_t use_crc;
    serial_t serial;
} rdp_t;

typedef enum rdp_io_source_t
{
    RDP_IO_SOURCE_NONE = 0,
    RDP_IO_SOURCE_IPV4 = 1,
    RDP_IO_SOURCE_LEGACY = 2,
    RDP_IO_SOURCE_ICMP = 4
} rdp_io_source_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Initialize the RDP fields set by the original rdp_init.  This does not touch
// the thread latches, input rate, framing options, or serial scheduler.  Those
// fields are set later in all 3 clients.
void rdp_init(rdp_t *owner);

// Creates the owner, serial and shared queues, selected data and diagnostic
// services, framing policy, and I/O thread in original source order. The
// source faithful build creates the recovered raw ICMP and traceroute sockets;
// checked Windows and Linux builds request errors through the UDP socket.
//
// Low flag bits 1/2/4 mean attempt both, require IPv4, and require legacy
// family. Bits 31 and 30 enable encryption and CRC. Expected connection count
// exactly 1 passes hash size argument 1; every other value passes 12.
// connhash_create expands those arguments to 1 or 2048 actual buckets.
// Output is cleared before allocation and is published only after the I/O
// thread starts.
//
// Like the clients, failure after allocation unconditionally calls
// rdp_destroy_internal; platform primitives must tolerate
// destroying initialized but not created subobjects on early failure.
int rdp_create(rdp_t **output, uint16_t local_port, uint32_t expected_connections, uint32_t flags);

// Allocates, initializes, creates, and publishes a connection for the
// supplied endpoint and option word. connection_init initializes both
// protocol halves; its tx_init call selects the owner's matching data and
// traceroute sockets and creates the random initial message ID. The new
// connection is then inserted in the event queue and endpoint hash.
//
// Under the required unique endpoint precondition, success returns the
// connection locked with a temporary reference for the caller in addition
// to its hash membership reference. Only 0 and
// RDP_CONNECTION_FEATURE_KEEPALIVE are accepted. The event heap insertion
// result and duplicate endpoint condition retain the clients' unchecked
// behavior. In particular, the post insert lookup result is ignored; a
// duplicate can lock an older member while output names the new allocation.
int rdp_connection_create_internal(rdp_t *owner, connection_t **output, const uint8_t endpoint[16], uint32_t options);

// Resolves an IPv4 peer, counts the outgoing attempt, creates and publishes
// a connection with option 0 or 1, and marks it locally initiated.
// The returned pointer remains owned by the endpoint hash; this function
// releases the creator's temporary locked reference before publishing it.
// It does not send SYN, resort the event heap, or wake the I/O thread.
//
// host, output, owner, and the global statistics pointer are unchecked. IPv4
// numeric value 255.255.255.255 follows the clients' inet_addr failure path
// and is passed to hostname lookup. Duplicate endpoints retain the internal
// creator's mismatched lock/output hazard.
int rdp_connect(rdp_t *owner, connection_t **output, const char *host, uint16_t port, uint32_t options);

// Processes a received socket or serial datagram through the complete
// recovered owner path. A 4 byte datagram from the owner's local endpoint
// returns its platform wake token; all normal paths return 0.
//
// The body preserves the clients' unchecked contracts: selected variable header
// fields are read before the final length check, arrival allocations are not
// checked before initialization, and the dormant reserved receive gate exits
// without releasing the temporary hash reference. packet must therefore
// address enough readable storage for every flag selected field.
uint32_t rdp_handle_data_recv(rdp_t *owner, uint8_t *packet, int32_t packet_bytes, const uint8_t source_address[16]);

// Publishes a `0xFFFF` connectionless payload to the application receive
// queue.
//
// The original immediately initializes an unchecked fast_malloc result.
// owner, payload, and source_address must be valid, and payload must address
// payload_bytes readable bytes.
void rdp_handle_connectionless(rdp_t *owner, const uint8_t *payload, uint32_t payload_bytes, const uint8_t source_address[16]);

// Parses a successful raw IPv4 ICMP receive, identifies the quoted RDP or
// traceroute endpoint, applies diagnostics, publishes a resulting disconnect,
// repairs event order, and releases the temporary connection reference.
// UINT32_MAX is the failed receive sentinel and is ignored without accounting.
void rdp_handle_icmp_recv(rdp_t *owner, const uint8_t *packet, uint32_t packet_bytes, const uint8_t source_address[16]);

// Applies an already decoded ICMP report to the exact remote endpoint.  The
// recovered raw packet parser and checked platform adapters share this tail.
void rdp_handle_reported_icmp(rdp_t *owner, const uint8_t remote_address[16], uint8_t type, uint8_t code, uint8_t trace_response, uint8_t trace_sample,
                              const uint8_t source_address[16]);

// Runs the recovered socket/serial wait, receive, accounting, connection
// event, and final owner cleanup loop. The running latch is tested only at
// the top of an outer pass; a stop wake therefore completes the receive and
// event half of the pass that was already entered.
//
// The selected platform receive routine returns -1 for socket failure. The
// clients pass that value through rdp_handle_data_recv once before testing the
// loop sentinel; the handler's initial signed length check rejects it. In the
// checked Windows build, an attributable UDP port unreachable uses a separate
// negative result which enters the shared ICMP handler instead.
// The serial only sleep path also retains the clients' uninitialized wait
// result hazard. Both are intentionally catalogued source faithful contracts.
uint32_t rdp_io_thread(rdp_t *owner);

// Recalculates a connection's next event and repairs its owner heap only
// when the 8 byte timeout key changed. A wake request sends at most 1
// coalesced loopback token when the changed connection becomes the heap head.
// The wake gate is set even when the selected platform backend has no usable
// socket or its send fails.
//
// The caller must hold the connection lock. The connection event link must be
// a live heap member.
void rdp_resort(connection_t *connection, int wake);

// Sends an application buffer through the vector implementation.
int connection_send(connection_t *connection, const void *data, uint32_t bytes, uint32_t stream, uint32_t flags);

// Converts an application vector to an unreliable message or consecutive
// 512 byte reliable fragments, adds each record through normal
// send checks, and performs a wake enabled owner resort on success.
//
// The default build validates vectors, streams, arithmetic,
// allocation, and projected reliable history use before queueing anything.
// RDPLIB_SOURCE_FAITHFUL retains the recovered caller preconditions.
//
// connection must still be a uniquely published endpoint member. The body
// ignores the pointer returned by its endpoint lookup and later unlocks the
// supplied pointer, preserving the clients' duplicate membership hazard.
int connection_sendv(connection_t *connection, const rdp_buffer_t *buffers, uint32_t buffer_count, uint32_t stream, uint32_t flags);

// Removes this connection's shared application arrivals, then either retires
// endpoint ownership immediately or starts/re arms timed linger and queues
// a reliable FIN.
//
// result and completion_event are optional and borrowed. When close is still
// pending, a timed call installs both pointers in the connection until ACK,
// peer STOP, abort, or destruction completes them. Repeating a pending timed
// close overwrites an earlier pair without signalling it, matching the
// original unchecked single waiter convention. A 0 timeout never queues FIN.
int connection_close(connection_t *connection, uint32_t timeout_ms, int *result, rdplib_platform_event_t *completion_event);

// Creates a temporary completion event, starts close, and waits indefinitely
// for protocol completion or connection destruction. timeout_ms controls
// linger only; it is not a wait timeout. Returns 1 only if event creation
// fails, otherwise 0. result receives the eventual clean/failed value.
int connection_close_wait(connection_t *connection, uint32_t timeout_ms, int *result);

// Performs the owner only teardown reached after the I/O loop stops. It
// removes each event head and its hash membership, destroys every connection,
// drains both application queues, closes the 4 network endpoints, and
// releases the remaining owner resources in client order.
//
// No temporary connection references, publishers, consumers, or concurrent
// users may remain. Each event member must still own exactly its hash
// membership reference. Violating those source preconditions can dereference
// null or stale storage, as it can in the clients. When
// join_io_thread_on_destroy is 0 this function also destroys the thread
// record and frees owner; otherwise those 2 releases belong to the waiter.
void rdp_destroy_internal(rdp_t *owner);

// Stops a running owner and selects who releases its thread record and owner
// allocation. A 0 wait argument returns after the wake; the I/O thread's
// rdp_destroy_internal call owns final release. A nonzero argument waits for
// that cleanup, destroys the thread record, and frees the owner here.
//
// The stop wake carries native uint32 value 1. Event resort wakes carry
// 0; the receive loop uses either only as a local drain terminator. The platform wait
// must not return before the I/O thread has completed rdp_destroy_internal.
// Repeated calls after io_thread_running leaves exactly 1 are no ops.
void rdp_destroy(rdp_t *owner, int wait_for_io_thread);

// Drops the hash membership reference. The caller normally still owns a
// locked temporary reference, so final destruction is deferred to rdp_unlock.
connection_t *rdp_connection_mark_for_delete(rdp_t *owner, connection_t *connection);

// Releases the connection lock and temporary hash reference. A final release
// removes the still live event link, destroys the connection, and frees it.
//
// The connection must have been returned locked by connhash_lock, and its
// event link must remain a live member until hash ownership reaches 0.
void rdp_unlock(connection_t *connection);

uint32_t rdp_get_input_rate(const rdp_t *owner);
int rdp_serial_tx_ready(rdp_t *owner);
uint32_t rdp_serial_get_time_empty(rdp_t *owner);
uint32_t rdp_get_serial_stall_time(rdp_t *owner);

#ifdef __cplusplus
}
#endif

#endif /* RDPLIB_RAW_H */
