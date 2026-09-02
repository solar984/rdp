// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "msg_outgoing.h"
#include "rdplib_constants.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

enum
{
    OUTGOING_TEST_DATA_CAPACITY = 32,
    OUTGOING_TEST_CANARY = 0xA5
};

typedef struct outgoing_fixture_t
{
    msg_outgoing_t outgoing;
    uint8_t data[OUTGOING_TEST_DATA_CAPACITY];
} outgoing_fixture_t;

_Static_assert(offsetof(msg_outgoing_t, txq_link) == 0, "msg_outgoing_t::txq_link moved");
_Static_assert(offsetof(msg_outgoing_t, time_first_sent) == sizeof(rdp_link_t), "msg_outgoing_t::time_first_sent moved");
_Static_assert(offsetof(msg_outgoing_t, time_last_sent) == sizeof(rdp_link_t) + 0x04, "msg_outgoing_t::time_last_sent moved");
_Static_assert(offsetof(msg_outgoing_t, options) == sizeof(rdp_link_t) + 0x08, "msg_outgoing_t::options moved");
_Static_assert(offsetof(msg_outgoing_t, msgid) == sizeof(rdp_link_t) + 0x0A, "msg_outgoing_t::msgid moved");
_Static_assert(offsetof(msg_outgoing_t, fragid) == sizeof(rdp_link_t) + 0x0C, "msg_outgoing_t::fragid moved");
_Static_assert(offsetof(msg_outgoing_t, frag_number) == sizeof(rdp_link_t) + 0x0E, "msg_outgoing_t::frag_number moved");
_Static_assert(offsetof(msg_outgoing_t, frag_total) == sizeof(rdp_link_t) + 0x10, "msg_outgoing_t::frag_total moved");
_Static_assert(offsetof(msg_outgoing_t, stream) == sizeof(rdp_link_t) + 0x12, "msg_outgoing_t::stream moved");
_Static_assert(offsetof(msg_outgoing_t, stream_seqnum) == sizeof(rdp_link_t) + 0x13, "msg_outgoing_t::stream_seqnum moved");
_Static_assert(offsetof(msg_outgoing_t, attempts) == sizeof(rdp_link_t) + 0x14, "msg_outgoing_t::attempts moved");
_Static_assert(offsetof(msg_outgoing_t, size) == sizeof(rdp_link_t) + 0x18, "msg_outgoing_t::size moved");
_Static_assert(offsetof(msg_outgoing_t, enqueue_time) == sizeof(rdp_link_t) + 0x1C, "msg_outgoing_t::enqueue_time moved");
_Static_assert(sizeof(msg_outgoing_t) == sizeof(rdp_link_t) + 0x20, "msg_outgoing_t native prefix size changed");
_Static_assert(offsetof(outgoing_fixture_t, data) == sizeof(msg_outgoing_t), "outgoing data no longer immediately follows its prefix");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(msg_outgoing_t) == 0x30, "msg_outgoing_t no longer matches the recovered 32-bit layout");
#endif
_Static_assert(_Generic(&msg_outgoing_init, void (*)(msg_outgoing_t *): 1, default: 0), "msg_outgoing_init signature");
_Static_assert(_Generic(&msg_outgoing_set_syn_bit, void (*)(msg_outgoing_t *): 1, default: 0), "msg_outgoing_set_syn_bit signature");
_Static_assert(_Generic(&msg_outgoing_set_time_last_sent, void (*)(msg_outgoing_t *, uint32_t): 1, default: 0), "msg_outgoing_set_time_last_sent signature");
_Static_assert(_Generic(&msg_outgoing_set_time_first_sent, void (*)(msg_outgoing_t *, uint32_t): 1, default: 0), "msg_outgoing_set_time_first_sent signature");
_Static_assert(_Generic(&msg_outgoing_get_data, char *(*)(msg_outgoing_t *): 1, default: 0), "msg_outgoing_get_data signature");
_Static_assert(_Generic(&msg_outgoing_append, void (*)(msg_outgoing_t *, const void *, uint32_t): 1, default: 0), "msg_outgoing_append signature");

static void initialize_fixture(outgoing_fixture_t *fixture, uint16_t options)
{
    memset(fixture, OUTGOING_TEST_CANARY, sizeof(*fixture));
    fixture->outgoing.options = options;
    fixture->outgoing.msgid = UINT16_C(0x1234);
    fixture->outgoing.fragid = UINT16_C(0x5678);
    fixture->outgoing.frag_number = UINT16_C(0x9ABC);
    fixture->outgoing.frag_total = UINT16_C(0xDEF0);
    fixture->outgoing.stream = UINT8_C(0x42);
    fixture->outgoing.stream_seqnum = UINT8_C(0xA7);
}

static void assert_canary(const uint8_t *bytes, size_t begin, size_t end)
{
    size_t index;

    for (index = begin; index < end; ++index)
    {
        assert(bytes[index] == OUTGOING_TEST_CANARY);
    }
}

static void check_serialization(uint16_t options, const uint8_t *expected, uint32_t expected_size)
{
    outgoing_fixture_t fixture;
#ifdef RDPLIB_DEBUG
    uint32_t before;
    uint32_t after;
#endif

    initialize_fixture(&fixture, options);
#ifdef RDPLIB_DEBUG
    before = time_get_ms();
#endif
    msg_outgoing_init(&fixture.outgoing);
#ifdef RDPLIB_DEBUG
    after = time_get_ms();
#endif

    assert(fixture.outgoing.size == expected_size);
    assert(fixture.outgoing.attempts == 0);
#ifdef RDPLIB_DEBUG
    assert((int32_t)(fixture.outgoing.enqueue_time - before) >= 0 && (int32_t)(after - fixture.outgoing.enqueue_time) >= 0);
#else
    assert(fixture.outgoing.enqueue_time == 0);
#endif
    assert(msg_outgoing_get_data(&fixture.outgoing) == (char *)fixture.data);
    assert(memcmp(fixture.data, expected, expected_size) == 0);
    assert_canary(fixture.data, expected_size, sizeof(fixture.data));
}

static void test_selective_initialization(void)
{
    outgoing_fixture_t fixture;
    rdp_link_t next;
    rdp_link_t prev;
#ifdef RDPLIB_DEBUG
    uint32_t before;
    uint32_t after;
#endif
    const size_t padding_begin = offsetof(msg_outgoing_t, attempts) + sizeof(fixture.outgoing.attempts);
    const size_t padding_end = offsetof(msg_outgoing_t, size);

    initialize_fixture(&fixture, RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED);
    fixture.outgoing.txq_link.next = &next;
    fixture.outgoing.txq_link.prev = &prev;
    fixture.outgoing.txq_link.item = &next;
    fixture.outgoing.txq_link.key.p = &prev;
    fixture.outgoing.time_first_sent = UINT32_C(0x11223344);
    fixture.outgoing.time_last_sent = UINT32_C(0x55667788);
    fixture.outgoing.attempts = UINT16_C(0xFFFF);
    fixture.outgoing.size = UINT32_C(0xCCCCCCCC);
    fixture.outgoing.enqueue_time = UINT32_C(0xDDDDDDDD);

#ifdef RDPLIB_DEBUG
    before = time_get_ms();
#endif
    msg_outgoing_init(&fixture.outgoing);
#ifdef RDPLIB_DEBUG
    after = time_get_ms();
#endif

    assert(fixture.outgoing.txq_link.next == &next);
    assert(fixture.outgoing.txq_link.prev == &prev);
    assert(fixture.outgoing.txq_link.item == &fixture.outgoing);
    assert(fixture.outgoing.txq_link.key.p == NULL);
    assert(fixture.outgoing.time_first_sent == UINT32_C(0x11223344));
    assert(fixture.outgoing.time_last_sent == UINT32_C(0x55667788));
    assert(fixture.outgoing.options == (RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED));
    assert(fixture.outgoing.msgid == UINT16_C(0x1234));
    assert(fixture.outgoing.fragid == UINT16_C(0x5678));
    assert(fixture.outgoing.frag_number == UINT16_C(0x9ABC));
    assert(fixture.outgoing.frag_total == UINT16_C(0xDEF0));
    assert(fixture.outgoing.stream == UINT8_C(0x42));
    assert(fixture.outgoing.stream_seqnum == UINT8_C(0xA7));
    assert(fixture.outgoing.attempts == 0);
    assert(fixture.outgoing.size == 4);
#ifdef RDPLIB_DEBUG
    assert((int32_t)(fixture.outgoing.enqueue_time - before) >= 0 && (int32_t)(after - fixture.outgoing.enqueue_time) >= 0);
#else
    assert(fixture.outgoing.enqueue_time == 0);
#endif
    assert_canary((const uint8_t *)&fixture.outgoing, padding_begin, padding_end);
}

static void test_byte_exact_options(void)
{
    static const uint8_t empty[] = {0};
    static const uint8_t msgid[] = {0x12, 0x34};
    static const uint8_t sequenced[] = {0x42};
    static const uint8_t msgid_sequenced[] = {0x12, 0x34, 0x42, 0xA7};
    static const uint8_t fragmented[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    static const uint8_t all_fields[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x42, 0xA7};

    check_serialization(0, empty, 0);
    check_serialization(RDP_FLAG_MSGID, msgid, (uint32_t)sizeof(msgid));
    check_serialization(RDP_FLAG_SEQUENCED, sequenced, (uint32_t)sizeof(sequenced));
    check_serialization(RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED, msgid_sequenced, (uint32_t)sizeof(msgid_sequenced));
    check_serialization(RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT, fragmented, (uint32_t)sizeof(fragmented));
    check_serialization(RDP_FLAG_MSGID | RDP_FLAG_FRAGMENT | RDP_FLAG_SEQUENCED, all_fields, (uint32_t)sizeof(all_fields));
}

static void test_option_and_time_helpers(void)
{
    outgoing_fixture_t fixture;

    initialize_fixture(&fixture, RDP_FLAG_FIN | RDP_FLAG_MSGID);
    fixture.outgoing.time_first_sent = 7;
    fixture.outgoing.time_last_sent = 8;
    fixture.outgoing.attempts = 9;

    msg_outgoing_set_syn_bit(&fixture.outgoing);
    assert(fixture.outgoing.options == (RDP_FLAG_FIN | RDP_FLAG_MSGID | RDP_FLAG_SYN));
    msg_outgoing_set_syn_bit(&fixture.outgoing);
    assert(fixture.outgoing.options == (RDP_FLAG_FIN | RDP_FLAG_MSGID | RDP_FLAG_SYN));

    msg_outgoing_set_time_first_sent(&fixture.outgoing, UINT32_C(0x10203040));
    assert(fixture.outgoing.time_first_sent == UINT32_C(0x10203040));
    assert(fixture.outgoing.time_last_sent == UINT32_C(0x10203040));
    assert(fixture.outgoing.attempts == 10);

    msg_outgoing_set_time_last_sent(&fixture.outgoing, UINT32_C(0x50607080));
    assert(fixture.outgoing.time_first_sent == UINT32_C(0x10203040));
    assert(fixture.outgoing.time_last_sent == UINT32_C(0x50607080));
    assert(fixture.outgoing.attempts == 11);

    fixture.outgoing.attempts = UINT16_MAX;
    msg_outgoing_set_time_last_sent(&fixture.outgoing, 1);
    assert(fixture.outgoing.attempts == 0);
}

static void test_append_and_canaries(void)
{
    static const uint8_t header[] = {0x12, 0x34, 0x42, 0xA7};
    static const uint8_t first[] = {0x10, 0x20, 0x30};
    static const uint8_t second[] = {0x40, 0x50, 0x60, 0x70};
    static const uint8_t expected[] = {0x12, 0x34, 0x42, 0xA7, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    outgoing_fixture_t fixture;

    initialize_fixture(&fixture, RDP_FLAG_MSGID | RDP_FLAG_SEQUENCED);
    msg_outgoing_init(&fixture.outgoing);
    assert(memcmp(fixture.data, header, sizeof(header)) == 0);

    msg_outgoing_append(&fixture.outgoing, first, 0);
    assert(fixture.outgoing.size == sizeof(header));
    msg_outgoing_append(&fixture.outgoing, first, (uint32_t)sizeof(first));
    msg_outgoing_append(&fixture.outgoing, second, (uint32_t)sizeof(second));
    assert(fixture.outgoing.size == sizeof(expected));
    assert(memcmp(fixture.data, expected, sizeof(expected)) == 0);
    assert_canary(fixture.data, sizeof(expected), sizeof(fixture.data));
}

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    test_selective_initialization();
    test_byte_exact_options();
    test_option_and_time_helpers();
    test_append_and_canaries();
    return 0;
}
