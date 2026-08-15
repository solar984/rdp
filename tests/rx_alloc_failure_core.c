// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stdint.h>
#include <string.h>

#include "rdpstat.h"
#include "rx.h"

static uint32_t allocation_calls;
static uint32_t allocation_size;
static rdp_stat test_global_statistics;

void *rx_test_fast_malloc(uint32_t size)
{
    ++allocation_calls;
    allocation_size = size;
    return NULL;
}

_Static_assert(_Generic(&rx_test_fast_malloc, void *(*)(uint32_t): 1, default: 0), "RX allocation seam signature");

static void record_reliable_arrival_before_assembly(connection_t *c, rdp_header_t *header)
{
    uint32_t duplicate;

    assert(rx_validate_seqnum_arrival(c, header->seqnum) == RDP_RX_ACCEPT);
    connection_record_arrival(c, header, &duplicate);
    assert(duplicate == 0);
}

static void test_allocation_failure_preserves_upstream_bookkeeping(void)
{
    connection_t c;
    rdp_header_t header;
    uint64_t history_after_recording;
    uint32_t packets_after_recording;
    uint32_t bytes_after_recording;
    uint8_t payload;

    memset(&c, 0, sizeof(c));
    memset(&header, 0, sizeof(header));
    memset(&test_global_statistics, 0, sizeof(test_global_statistics));
    g_rdp_stat = &test_global_statistics;
    rx_init(&c);

    header.options = RDP_FLAG_MSGID;
    header.seqnum = 5;
    header.msgid = 42;
    header.header_size = 12;
    header.data_size = 1;
    payload = UINT8_C(0x5a);

    record_reliable_arrival_before_assembly(&c, &header);
    history_after_recording = c.rx_recent_seqnum_history;
    packets_after_recording = c.stat.packets_rx_in_sequence;
    bytes_after_recording = c.stat.bytes_rx_in_sequence;

    assert(c.rx_reassembly_pool.list.head == NULL);
    assert(c.rx_reassembly_pool.list.tail == NULL);
    assert(c.rx_reassembly_pool.list.size == 0);
    assert(rx_assemble(&c, &header, (char *)&payload) == NULL);

    assert(allocation_calls == 1);
    assert(allocation_size == (uint32_t)sizeof(msg_arrival_t) + header.data_size);
    assert(c.rx_reassembly_pool.list.head == NULL);
    assert(c.rx_reassembly_pool.list.tail == NULL);
    assert(c.rx_reassembly_pool.list.size == 0);

    // Assembly owns no rollback boundary: the sequence and reliable ID work
    // performed by the receive owner before allocation remains committed.
    assert(c.rx_highest_seqnum_received == header.seqnum);
    assert(c.rx_recent_seqnum_history == history_after_recording);
    assert(c.stat.packets_rx_in_sequence == packets_after_recording);
    assert(c.stat.bytes_rx_in_sequence == bytes_after_recording);
    assert(c.rx_syn_recvd == 1);
    assert(c.rx_syn_msgid == header.msgid);
    assert(c.rx_received_all_thru == header.msgid);
    assert(c.rx_highest_received == header.msgid);
    assert(c.rx_msgid_count == 1);

    rx_destroy(&c);
}

int main(void)
{
    test_allocation_failure_preserves_upstream_bookkeeping();
    return 0;
}
