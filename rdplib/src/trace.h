// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_TRACE_H
#define RDP_TRACE_H

#include <stdint.h>

#include "layout.h"

enum rdp_trace_constants
{
    RDP_TRACE_MAX_TTL = 30,
    RDP_TRACE_SWEEP_LIMIT = 3,
    RDP_TRACE_SAMPLE_CAPACITY = RDP_TRACE_MAX_TTL * RDP_TRACE_SWEEP_LIMIT
};

// A result slot for a UDP traceroute probe. The active trace allocation contains 90 records.
typedef struct trace_sample_t
{
    /* 0x00 */ uint32_t sent_time_ms;
    /* 0x04 */ uint32_t round_trip_time_ms; // 0 until an ICMP response identifies this probe.
    /* 0x08 */ uint32_t responder_ipv4;     // sockaddr_in::sin_addr in network byte order.
    /* 0x0C */ uint8_t ttl;
    /* 0x0D */ uint8_t icmp_type;
    /* 0x0E */ uint8_t icmp_code;
    /* 0x0F */ uint8_t reserved;
} trace_sample_t;

RDP_ASSERT_OFFSET(trace_sample_t, sent_time_ms, 0);
RDP_ASSERT_OFFSET(trace_sample_t, round_trip_time_ms, 4);
RDP_ASSERT_OFFSET(trace_sample_t, responder_ipv4, 8);
RDP_ASSERT_OFFSET(trace_sample_t, ttl, 12);
RDP_ASSERT_OFFSET(trace_sample_t, icmp_type, 13);
RDP_ASSERT_OFFSET(trace_sample_t, icmp_code, 14);
RDP_STATIC_ASSERT(sizeof(trace_sample_t) == 16, "trace_sample_t must be 16 bytes");
RDP_STATIC_ASSERT(sizeof(trace_sample_t) * RDP_TRACE_SAMPLE_CAPACITY == 0x5A0, "the active trace allocation must contain 90 samples");

struct connection_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Sends a traceroute probe and advances the TTL/sample cursor only when the
// backend reports exactly 1 byte sent. The returned value is the initial or
// restore TTL operation's status; a UDP send failure can still return 0.
//
// The original performs no sample pointer or index bounds checks. Trace start
// must own a 90 record allocation and event scheduling must stop at 3
// sweeps before this function is called.
int trace_send(struct connection_t *connection);

// Starts or restarts the optional traceroute collector under the original
// endpoint hash lock. Result codes are 0 success, 2 allocation failure,
// 6 null connection, 8 unavailable probe socket, and 9 restart too soon.
//
// If a completed snapshot already exists, the active array is freed before
// its replacement is allocated. Allocation failure can therefore leave the
// trace option enabled with a null active array. The completed snapshot is
// also never released by the client receive destructor; both bugs are
// intentionally retained and documented.
int trace_start(struct connection_t *connection);

// Formats caller supplied traceroute records as the standalone helper found
// only in EQMac Intel. PPC and TAKP Windows contain neither this body nor a
// call path to it; the transport's trace collection behavior is otherwise
// the same.
//
// output has no size argument and every append is unchecked in the original.
// It must be large enough for every line, resolved host name, and alternate
// responder. samples may be null only when sample_count is 0. When
// resolve_names is nonzero, a null or unsuccessful resolver falls back to a
// numeric IPv4 address.
int format_trace(char *output, const trace_sample_t *samples, uint32_t sample_count, int resolve_names);

#ifdef __cplusplus
}
#endif

#endif // RDP_TRACE_H
