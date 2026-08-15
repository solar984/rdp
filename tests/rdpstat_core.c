// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rdpstat.h"

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&rdp_stat_format, uint32_t (*)(rdp_stat *, char *, uint32_t): 1, default: 0), "rdp_stat_format signature");
#endif

static int storage_is_zeroed(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t offset;

    for (offset = 0; offset < size; ++offset)
    {
        if (bytes[offset] != 0)
        {
            return 0;
        }
    }

    return 1;
}

static void test_layout(void)
{
    rdp_stat statistics;

    assert(sizeof(statistics) == 0x870);
    assert(offsetof(rdp_stat, best_effort_packets_tx) == 0x000);
    assert(offsetof(rdp_stat, messages_acked) == 0x088);
    assert(offsetof(rdp_stat, connections_closed) == 0x178);
    assert(offsetof(rdp_stat, connections_dropped) == 0x180);
    assert(offsetof(rdp_stat, connections_dropped_unreachable) == 0x1C8);
    assert(ARRAY_COUNT(statistics.connections_dropped_unreachable) == 16);
    assert(offsetof(rdp_stat, connections_dropped_ttl_expired) == 0x250);
    assert(ARRAY_COUNT(statistics.connections_dropped_ttl_expired) == 2);
    assert(offsetof(rdp_stat, connections_dropped_parameter_problem) == 0x260);
    assert(ARRAY_COUNT(statistics.connections_dropped_parameter_problem) == 2);
    assert(offsetof(rdp_stat, icmp_received_with_connection) == 0x278);
    assert(offsetof(rdp_stat, icmp_received_without_connection) == 0x280);
    assert(offsetof(rdp_stat, icmp_unreachable) == 0x288);
    assert(ARRAY_COUNT(statistics.icmp_unreachable) == 16);
    assert(offsetof(rdp_stat, icmp_ttl_expired) == 0x310);
    assert(ARRAY_COUNT(statistics.icmp_ttl_expired) == 2);
    assert(offsetof(rdp_stat, icmp_parameter_problem) == 0x320);
    assert(ARRAY_COUNT(statistics.icmp_parameter_problem) == 2);
    assert(offsetof(rdp_stat, sendto_errno) == 0x358);
    assert(ARRAY_COUNT(statistics.sendto_errno) == 152);
    assert(offsetof(rdp_stat, sendto_errno_big) == 0x818);
    assert(offsetof(rdp_stat, rqd_last_interval) == 0x828);
    assert(offsetof(rdp_stat, rqd_bytes) == 0x850);
    assert(offsetof(rdp_stat, shipping_tx) == 0x858);
    assert(offsetof(rdp_stat, shipping_tx_not_ready) == 0x868);
}

static void test_storage_policy(void)
{
#ifdef RDP_DEAD_CODE
    assert(g_rdp_stat == &rdp_stat_struct);
    assert(storage_is_zeroed(&rdp_stat_struct, sizeof(rdp_stat_struct)));
#else
    rdp_stat statistics;

    assert(g_rdp_stat == NULL);
    memset(&statistics, 0, sizeof(statistics));
    g_rdp_stat = &statistics;
    assert(g_rdp_stat == &statistics);
    assert(storage_is_zeroed(g_rdp_stat, sizeof(*g_rdp_stat)));
    g_rdp_stat = NULL;
#endif
}

#if defined(RDP_DEAD_CODE) && defined(_WIN32)
static void test_format(void)
{
    static const char expected_zero[] =
        "0 packets sent (0 bytes)\n"
        " 0 best effort packets sent (0 bytes)\n"
        " 0 guaranteed packets sent (0 bytes)\n"
        " 0 guaranteed packets retransmitted (0 bytes)\n"
        " 0 packets with ack\n"
        "  0 ack-only packets (0 delayed)\n"
        "  0 packets with ack and data\n"
        " 0 packets without ack\n"
        "0 packets received (0 bytes)\n"
        " 0 best effort packets received (0 bytes)\n"
        " 0 guaranteed packets received (0 bytes)\n"
        " 0 duplicate packets received (0 bytes)\n"
        " 0 acks (for 0 messages)\n"
        "  0 ack only packets\n"
        "  0 ack and data packets\n"
        " 0 duplicate acks (0 bytes)\n"
        " 0 acks for unsent messages (invalid))\n"
        " 0 packets (0 bytes) received in-sequence\n"
        " 0 packets (0 bytes) received out-of-sequence\n"
        " 0 packets received after close\n"
        " 0 packets received after disconnect\n"
        " 0 sendto calls (0 failures, 0 consecutive)\n"
        " 0 shipping::tx() (0 useless, 0 not ready)\n";
    char buffer[65536];
    rdp_stat statistics;
    uint32_t size;

    memset(&statistics, 0, sizeof(statistics));
    size = rdp_stat_format(&statistics, buffer, 0);
    assert(size == strlen(buffer));
    assert(strcmp(buffer, expected_zero) == 0);

    statistics.discarded_bad_options = 5;
    statistics.connections_dropped_unreachable[7] = 7;
    statistics.icmp_unreachable[7] = 9;
    statistics.sendto_errno_big = 3;
    statistics.sendto_errno_big_last = ((uint64_t)70000 << 32) | 152u;
    size = rdp_stat_format(&statistics, buffer, 0);
    assert(size == strlen(buffer));
    assert(strstr(buffer, " 5 discarded because bad options\n") != NULL);
    assert(strstr(buffer, " 7 connections dropped: destination host unknown\n") != NULL);
    assert(strstr(buffer, " 7 connections dropped: source host isolated (obsolete)\n") != NULL);
    assert(strstr(buffer, " 9 icmp destination host unknown\n") != NULL);
    assert(strstr(buffer, " 9 icmp source host isolated (obsolete)\n") != NULL);
    assert(strstr(buffer, "  3 errno > 152 (last big errno==70000)\n") != NULL);
}
#endif

int main(void)
{
    test_layout();
    test_storage_policy();
#if defined(RDP_DEAD_CODE) && defined(_WIN32)
    test_format();
#endif
    return 0;
}
