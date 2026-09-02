// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "msg_arrival.h"
#include "packet.h"
#include "rdplib_constants.h"

_Static_assert(_Generic(&msg_arrival_init, void (*)(msg_arrival_t *, uint16_t): 1, default: 0), "msg_arrival_init signature");
_Static_assert(_Generic(&msg_arrival_prepare_for_sequencer, void (*)(msg_arrival_t *): 1, default: 0), "msg_arrival_prepare_for_sequencer signature");
_Static_assert(_Generic(&msg_arrival_prepare_for_rxq, void (*)(msg_arrival_t *): 1, default: 0), "msg_arrival_prepare_for_rxq signature");
_Static_assert(_Generic(&msg_arrival_assemble, uint32_t (*)(msg_arrival_t *, connection_t *, rdp_header_t *, char *): 1, default: 0), "msg_arrival_assemble signature");
_Static_assert(_Generic(&msg_arrival_init_disconnect_msg, void (*)(msg_arrival_t *, connection_t *): 1, default: 0), "msg_arrival_init_disconnect_msg signature");
_Static_assert(_Generic(&msg_arrival_validate_fragment_arrival, uint32_t (*)(msg_arrival_t *, rdp_header_t *): 1, default: 0), "msg_arrival_validate_fragment_arrival signature");
_Static_assert(_Generic(&msg_arrival_get_size, uint32_t (*)(const msg_arrival_t *): 1, default: 0), "msg_arrival_get_size signature");
_Static_assert(_Generic(&msg_arrival_get_data, char *(*)(const msg_arrival_t *): 1, default: 0), "msg_arrival_get_data signature");
_Static_assert(_Generic(&msg_arrival_get_sender, connection_t *(*)(const msg_arrival_t *): 1, default: 0), "msg_arrival_get_sender signature");
_Static_assert(_Generic(&msg_arrival_has_fin, uint32_t (*)(const msg_arrival_t *): 1, default: 0), "msg_arrival_has_fin signature");

#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&msg_arrival_get_sender_addr, struct sockaddr *(*)(msg_arrival_t *): 1, default: 0), "msg_arrival_get_sender_addr signature");
#endif

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

enum
{
    TEST_FRAGMENT_BYTES = 512,
    TEST_PAYLOAD_CAPACITY = 3 * TEST_FRAGMENT_BYTES
};

typedef union arrival_storage_t
{
    msg_arrival_t alignment;
    uint8_t bytes[sizeof(msg_arrival_t) + TEST_PAYLOAD_CAPACITY];
} arrival_storage_t;

_Static_assert(sizeof(((msg_arrival_t *)0)->from) == 16, "msg_arrival_t::from must remain one sockaddr");

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(msg_arrival_t) == 0x3C, "the recovered 32-bit msg_arrival_t prefix must remain 0x3c bytes");
_Static_assert(offsetof(msg_arrival_t, rxq_link) == 0x00, "msg_arrival_t::rxq_link moved");
_Static_assert(offsetof(msg_arrival_t, sender) == 0x10, "msg_arrival_t::sender moved");
_Static_assert(offsetof(msg_arrival_t, options) == 0x14, "msg_arrival_t::options moved");
_Static_assert(offsetof(msg_arrival_t, seqnum) == 0x16, "msg_arrival_t::seqnum moved");
_Static_assert(offsetof(msg_arrival_t, msgid) == 0x18, "msg_arrival_t::msgid moved");
_Static_assert(offsetof(msg_arrival_t, fragid) == 0x1A, "msg_arrival_t::fragid moved");
_Static_assert(offsetof(msg_arrival_t, frag_total) == 0x1C, "msg_arrival_t::frag_total moved");
_Static_assert(offsetof(msg_arrival_t, stream) == 0x1E, "msg_arrival_t::stream moved");
_Static_assert(offsetof(msg_arrival_t, stream_seqnum) == 0x1F, "msg_arrival_t::stream_seqnum moved");
_Static_assert(offsetof(msg_arrival_t, fragments_collected) == 0x20, "msg_arrival_t::fragments_collected moved");
_Static_assert(offsetof(msg_arrival_t, size) == 0x24, "msg_arrival_t::size moved");
_Static_assert(offsetof(msg_arrival_t, enqueue_time) == 0x28, "msg_arrival_t::enqueue_time moved");
_Static_assert(offsetof(msg_arrival_t, from) == 0x2C, "msg_arrival_t::from moved");
#endif

static msg_arrival_t *storage_arrival(arrival_storage_t *storage)
{
    return (msg_arrival_t *)(void *)storage->bytes;
}

static uint8_t *arrival_payload(msg_arrival_t *arrival)
{
    return (uint8_t *)(void *)(arrival + 1);
}

static connection_t *test_sender(void)
{
    static uint32_t sender_storage;

    return (connection_t *)(void *)&sender_storage;
}

static rdp_header_t make_fragment_header(uint16_t options, uint16_t seqnum, uint16_t msgid, uint16_t fragid, uint16_t frag_number, uint16_t frag_total, uint8_t stream,
                                          uint8_t stream_seqnum, uint16_t data_size)
{
    rdp_header_t header;

    memset(&header, 0, sizeof(header));
    header.options = options;
    header.seqnum = seqnum;
    header.msgid = msgid;
    header.fragid = fragid;
    header.frag_number = frag_number;
    header.frag_total = frag_total;
    header.stream = stream;
    header.stream_seqnum = stream_seqnum;
    header.data_size = data_size;
    return header;
}

static void fill_pattern(uint8_t *bytes, size_t size, uint8_t seed)
{
    size_t index;

    for (index = 0; index < size; ++index)
    {
        bytes[index] = (uint8_t)(seed + (uint8_t)(index * 13u));
    }
}

static void test_selective_initialization_and_keys(void)
{
    arrival_storage_t storage;
    msg_arrival_t *arrival = storage_arrival(&storage);
    uint8_t next_before[sizeof(arrival->rxq_link.next)];
    uint8_t prev_before[sizeof(arrival->rxq_link.prev)];
    uint8_t from_before[sizeof(arrival->from)];

    memset(&storage, 0xA5, sizeof(storage));
    memcpy(next_before, &arrival->rxq_link.next, sizeof(next_before));
    memcpy(prev_before, &arrival->rxq_link.prev, sizeof(prev_before));
    memcpy(from_before, &arrival->from, sizeof(from_before));

    msg_arrival_init(arrival, UINT16_C(0x6B7C));

    assert(arrival->rxq_link.item == arrival);
    assert(arrival->rxq_link.key.p == &arrival->fragid);
    assert(arrival->sender == NULL);
    assert(arrival->fragid == UINT16_C(0x6B7C));
    assert(arrival->fragments_collected == 0);
    assert(memcmp(next_before, &arrival->rxq_link.next, sizeof(next_before)) == 0);
    assert(memcmp(prev_before, &arrival->rxq_link.prev, sizeof(prev_before)) == 0);
    assert(arrival->options == UINT16_C(0xA5A5));
    assert(arrival->seqnum == UINT16_C(0xA5A5));
    assert(arrival->msgid == UINT16_C(0xA5A5));
    assert(arrival->frag_total == UINT16_C(0xA5A5));
    assert(arrival->stream == UINT8_C(0xA5));
    assert(arrival->stream_seqnum == UINT8_C(0xA5));
    assert(arrival->size == UINT32_C(0xA5A5A5A5));
    assert(arrival->enqueue_time == UINT32_C(0xA5A5A5A5));
    assert(memcmp(from_before, &arrival->from, sizeof(from_before)) == 0);

    msg_arrival_prepare_for_sequencer(arrival);
    assert(arrival->rxq_link.key.p == &arrival->stream_seqnum);
    msg_arrival_prepare_for_rxq(arrival);
    assert(arrival->rxq_link.key.p == NULL);
}

static void test_ordered_assembly(void)
{
    arrival_storage_t storage;
    msg_arrival_t *arrival = storage_arrival(&storage);
    connection_t *sender = test_sender();
    uint8_t first[TEST_FRAGMENT_BYTES];
    uint8_t last[11];
    rdp_header_t header;

    memset(&storage, 0, sizeof(storage));
    fill_pattern(first, sizeof(first), UINT8_C(0x17));
    fill_pattern(last, sizeof(last), UINT8_C(0xC1));
    msg_arrival_init(arrival, UINT16_C(0x3132));

    header = make_fragment_header(RDP_FLAG_FIN | RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, UINT16_C(0x7788), UINT16_C(0xFFFE), UINT16_C(0x3132), 0, 2, 7, 0x91, sizeof(first));
    assert(msg_arrival_assemble(arrival, sender, &header, (char *)first) == 0);
    assert(arrival->fragments_collected == 1);
    assert(arrival->options == header.options);
    assert(arrival->seqnum == header.seqnum);
    assert(arrival->msgid == header.msgid);
    assert(arrival->frag_total == header.frag_total);
    assert(arrival->stream == header.stream);
    assert(arrival->stream_seqnum == header.stream_seqnum);
    assert(arrival->sender == sender);
    assert(arrival->size == 0);
    assert(msg_arrival_get_data(arrival) == NULL);
    assert(memcmp(arrival_payload(arrival), first, sizeof(first)) == 0);

    header = make_fragment_header(0, UINT16_C(0x1111), UINT16_C(0xFFFF), UINT16_C(0x3132), 1, 2, 18, 0x22, sizeof(last));
    assert(msg_arrival_validate_fragment_arrival(arrival, &header) == 0);
    assert(msg_arrival_assemble(arrival, sender, &header, (char *)last) != 0);
    assert(arrival->fragments_collected == 2);
    assert(arrival->size == TEST_FRAGMENT_BYTES + sizeof(last));
    assert(msg_arrival_get_size(arrival) == TEST_FRAGMENT_BYTES + sizeof(last));
    assert((void *)msg_arrival_get_data(arrival) == (void *)arrival_payload(arrival));
    assert(memcmp(arrival_payload(arrival), first, sizeof(first)) == 0);
    assert(memcmp(arrival_payload(arrival) + TEST_FRAGMENT_BYTES, last, sizeof(last)) == 0);

    // Metadata belongs to fragment 0; later fragments only contribute payload and completion state.
    assert(arrival->options == (RDP_FLAG_FIN | RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED));
    assert(arrival->seqnum == UINT16_C(0x7788));
    assert(arrival->msgid == UINT16_C(0xFFFE));
    assert(arrival->stream == 7);
    assert(arrival->stream_seqnum == UINT8_C(0x91));
    assert(msg_arrival_get_sender(arrival) == sender);
    assert(msg_arrival_has_fin(arrival) != 0);
}

static void test_out_of_order_assembly_and_geometry(void)
{
    arrival_storage_t storage;
    msg_arrival_t *arrival = storage_arrival(&storage);
    connection_t *sender = test_sender();
    uint8_t first[TEST_FRAGMENT_BYTES];
    uint8_t last[7];
    rdp_header_t header;
    rdp_header_t conflicting;

    memset(&storage, 0, sizeof(storage));
    fill_pattern(first, sizeof(first), UINT8_C(0x29));
    fill_pattern(last, sizeof(last), UINT8_C(0xE3));
    msg_arrival_init(arrival, UINT16_C(0x5152));

    header = make_fragment_header(0, UINT16_C(0x2222), 0, UINT16_C(0x5152), 1, 2, 12, 0x45, sizeof(last));
    assert(msg_arrival_assemble(arrival, sender, &header, (char *)last) == 0);
    assert(arrival->sender == NULL);
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    assert(arrival->frag_total == 0);
    assert(arrival->msgid == 0);
#else
    assert(arrival->frag_total == 2);
    assert(arrival->msgid == UINT16_MAX);
#endif
    assert(arrival->fragments_collected == 1);
    assert(arrival->size == TEST_FRAGMENT_BYTES + sizeof(last));
    assert((void *)msg_arrival_get_data(arrival) == (void *)arrival_payload(arrival));
    assert(memcmp(arrival_payload(arrival) + TEST_FRAGMENT_BYTES, last, sizeof(last)) == 0);

    conflicting = make_fragment_header(RDP_FLAG_MSGID, UINT16_C(0x3333), UINT16_MAX, UINT16_C(0x5152), 0, 3, 4, 0x67, sizeof(first));
#ifdef RDPLIB_TEST_SOURCE_FAITHFUL
    // Before fragment 0, the historical sender null sentinel bypasses group validation. Do not assemble the conflicting geometry: the allocation was sized for 2 fragments.
    assert(msg_arrival_validate_fragment_arrival(arrival, &conflicting) == 0);
#else
    // Checked assembly binds the allocation geometry to the first observed fragment, so a later fragment cannot expand it.
    assert(msg_arrival_validate_fragment_arrival(arrival, &conflicting) == 2);
#endif

    header = make_fragment_header(RDP_FLAG_FIN | RDP_FLAG_MSGID, UINT16_C(0x4444), UINT16_MAX, UINT16_C(0x5152), 0, 2, 5, 0x89, sizeof(first));
    assert(msg_arrival_validate_fragment_arrival(arrival, &header) == 0);
    assert(msg_arrival_assemble(arrival, sender, &header, (char *)first) != 0);
    assert(arrival->sender == sender);
    assert(arrival->frag_total == 2);
    assert(arrival->fragments_collected == 2);
    assert(arrival->size == TEST_FRAGMENT_BYTES + sizeof(last));
    assert(arrival->options == (RDP_FLAG_FIN | RDP_FLAG_MSGID));
    assert(arrival->seqnum == UINT16_C(0x4444));
    assert(arrival->msgid == UINT16_MAX);
    assert(arrival->stream == 5);
    assert(arrival->stream_seqnum == UINT8_C(0x89));
    assert(memcmp(arrival_payload(arrival), first, sizeof(first)) == 0);
    assert(memcmp(arrival_payload(arrival) + TEST_FRAGMENT_BYTES, last, sizeof(last)) == 0);
}

static void test_fragment_validation(void)
{
    arrival_storage_t storage;
    msg_arrival_t *arrival = storage_arrival(&storage);
    rdp_header_t header;

    memset(&storage, 0, sizeof(storage));
    msg_arrival_init(arrival, UINT16_C(0x8182));
    arrival->sender = test_sender();
    arrival->msgid = UINT16_MAX;
    arrival->frag_total = 3;
    arrival->fragments_collected = 1;

    header = make_fragment_header(0, 0, 0, UINT16_C(0x8182), 1, 3, 0, 0, 1);
    assert(msg_arrival_validate_fragment_arrival(arrival, &header) == 0);
    header.msgid = 1;
    assert(msg_arrival_validate_fragment_arrival(arrival, &header) == 2);
    header.msgid = 0;
    header.frag_total = 4;
    assert(msg_arrival_validate_fragment_arrival(arrival, &header) == 2);

    // Before any fragment is collected there is no message ID base to validate.
    arrival->sender = NULL;
    arrival->fragments_collected = 0;
    header.frag_total = 3;
    header.msgid = UINT16_C(0x7777);
    assert(msg_arrival_validate_fragment_arrival(arrival, &header) == 0);
}

static void test_disconnect_initialization(void)
{
    arrival_storage_t storage;
    msg_arrival_t *arrival = storage_arrival(&storage);
    connection_t *sender = test_sender();
    uint8_t expected[sizeof(msg_arrival_t)];

    memset(&storage, 0xA5, sizeof(storage));
    memset(expected, 0, sizeof(expected));
    memcpy(expected + offsetof(msg_arrival_t, rxq_link) + offsetof(rdp_link_t, item), &arrival, sizeof(arrival));
    memcpy(expected + offsetof(msg_arrival_t, sender), &sender, sizeof(sender));

    msg_arrival_init_disconnect_msg(arrival, sender);

    assert(memcmp(arrival, expected, sizeof(expected)) == 0);
    assert(arrival->rxq_link.item == arrival);
    assert(arrival->rxq_link.key.p == NULL);
    assert(msg_arrival_get_sender(arrival) == sender);
    assert(msg_arrival_get_size(arrival) == 0);
    assert(msg_arrival_get_data(arrival) == NULL);
    assert(msg_arrival_has_fin(arrival) == 0);
}

static void test_getters(void)
{
    arrival_storage_t storage;
    msg_arrival_t *arrival = storage_arrival(&storage);

    memset(&storage, 0, sizeof(storage));
    arrival->size = 9;
    arrival->sender = test_sender();
    arrival->options = RDP_FLAG_FIN;

    assert(msg_arrival_get_size(arrival) == 9);
    assert((void *)msg_arrival_get_data(arrival) == (void *)arrival_payload(arrival));
    assert(msg_arrival_get_sender(arrival) == test_sender());
    assert(msg_arrival_has_fin(arrival) != 0);

#ifdef RDP_DEAD_CODE
    assert(msg_arrival_get_sender_addr(arrival) == NULL);
    arrival->sender = NULL;
    assert(msg_arrival_get_sender_addr(arrival) == &arrival->from);
#endif
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_selective_initialization_and_keys();
    test_ordered_assembly();
    test_out_of_order_assembly_and_geometry();
    test_fragment_validation();
    test_disconnect_initialization();
    test_getters();
    return 0;
}
