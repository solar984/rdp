// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "packet.h"

_Static_assert(_Generic((rdp_header_t *)0, struct _rdp_header_t *: 1, default: 0), "rdp_header_t typedef and tag");
_Static_assert(_Generic(((rdp_header_t *)0)->options, uint16_t: 1, default: 0), "rdp_header_t.options type");
_Static_assert(_Generic(((rdp_header_t *)0)->seqnum, uint16_t: 1, default: 0), "rdp_header_t.seqnum type");
_Static_assert(_Generic(&((rdp_header_t *)0)->ack, uint16_t **: 1, default: 0), "rdp_header_t.ack mutable pointer type");
_Static_assert(_Generic(((rdp_header_t *)0)->msgid, uint16_t: 1, default: 0), "rdp_header_t.msgid type");
_Static_assert(_Generic(((rdp_header_t *)0)->fragid, uint16_t: 1, default: 0), "rdp_header_t.fragid type");
_Static_assert(_Generic(((rdp_header_t *)0)->frag_number, uint16_t: 1, default: 0), "rdp_header_t.frag_number type");
_Static_assert(_Generic(((rdp_header_t *)0)->frag_total, uint16_t: 1, default: 0), "rdp_header_t.frag_total type");
_Static_assert(_Generic(((rdp_header_t *)0)->stream, uint8_t: 1, default: 0), "rdp_header_t.stream type");
_Static_assert(_Generic(((rdp_header_t *)0)->stream_seqnum, uint8_t: 1, default: 0), "rdp_header_t.stream_seqnum type");
_Static_assert(_Generic(((rdp_header_t *)0)->header_size, uint16_t: 1, default: 0), "rdp_header_t.header_size type");
_Static_assert(_Generic(((rdp_header_t *)0)->data_size, uint16_t: 1, default: 0), "rdp_header_t.data_size type");

_Static_assert(offsetof(rdp_header_t, options) == 0x00, "rdp_header_t.options layout");
_Static_assert(offsetof(rdp_header_t, seqnum) == 0x02, "rdp_header_t.seqnum layout");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(rdp_header_t, ack) == 0x04, "rdp_header_t.ack x86 layout");
_Static_assert(offsetof(rdp_header_t, msgid) == 0x08, "rdp_header_t.msgid x86 layout");
_Static_assert(offsetof(rdp_header_t, fragid) == 0x0A, "rdp_header_t.fragid x86 layout");
_Static_assert(offsetof(rdp_header_t, frag_number) == 0x0C, "rdp_header_t.frag_number x86 layout");
_Static_assert(offsetof(rdp_header_t, frag_total) == 0x0E, "rdp_header_t.frag_total x86 layout");
_Static_assert(offsetof(rdp_header_t, stream) == 0x10, "rdp_header_t.stream x86 layout");
_Static_assert(offsetof(rdp_header_t, stream_seqnum) == 0x11, "rdp_header_t.stream_seqnum x86 layout");
_Static_assert(offsetof(rdp_header_t, header_size) == 0x12, "rdp_header_t.header_size x86 layout");
_Static_assert(offsetof(rdp_header_t, data_size) == 0x14, "rdp_header_t.data_size x86 layout");
_Static_assert(sizeof(rdp_header_t) == 0x18, "rdp_header_t x86 size");
#elif UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(rdp_header_t, ack) == 0x08, "rdp_header_t.ack x64 layout");
_Static_assert(offsetof(rdp_header_t, msgid) == 0x10, "rdp_header_t.msgid x64 layout");
_Static_assert(offsetof(rdp_header_t, fragid) == 0x12, "rdp_header_t.fragid x64 layout");
_Static_assert(offsetof(rdp_header_t, frag_number) == 0x14, "rdp_header_t.frag_number x64 layout");
_Static_assert(offsetof(rdp_header_t, frag_total) == 0x16, "rdp_header_t.frag_total x64 layout");
_Static_assert(offsetof(rdp_header_t, stream) == 0x18, "rdp_header_t.stream x64 layout");
_Static_assert(offsetof(rdp_header_t, stream_seqnum) == 0x19, "rdp_header_t.stream_seqnum x64 layout");
_Static_assert(offsetof(rdp_header_t, header_size) == 0x1A, "rdp_header_t.header_size x64 layout");
_Static_assert(offsetof(rdp_header_t, data_size) == 0x1C, "rdp_header_t.data_size x64 layout");
_Static_assert(sizeof(rdp_header_t) == 0x20, "rdp_header_t x64 size");
#else
#error Unsupported pointer size
#endif

static void test_members_retain_independent_values(void)
{
    uint16_t ack_words[2] = {UINT16_C(0x1234), UINT16_C(0x5678)};
    rdp_header_t header;

    memset(&header, 0, sizeof(header));
    header.options = UINT16_C(0x0102);
    header.seqnum = UINT16_C(0x0304);
    header.ack = ack_words;
    header.msgid = UINT16_C(0x0506);
    header.fragid = UINT16_C(0x0708);
    header.frag_number = UINT16_C(0x090A);
    header.frag_total = UINT16_C(0x0B0C);
    header.stream = UINT8_C(0x0D);
    header.stream_seqnum = UINT8_C(0x0E);
    header.header_size = UINT16_C(0x0F10);
    header.data_size = UINT16_C(0x1112);

    assert(header.options == UINT16_C(0x0102));
    assert(header.seqnum == UINT16_C(0x0304));
    assert(header.ack == ack_words);
    assert(header.ack[0] == UINT16_C(0x1234));
    assert(header.ack[1] == UINT16_C(0x5678));
    assert(header.msgid == UINT16_C(0x0506));
    assert(header.fragid == UINT16_C(0x0708));
    assert(header.frag_number == UINT16_C(0x090A));
    assert(header.frag_total == UINT16_C(0x0B0C));
    assert(header.stream == UINT8_C(0x0D));
    assert(header.stream_seqnum == UINT8_C(0x0E));
    assert(header.header_size == UINT16_C(0x0F10));
    assert(header.data_size == UINT16_C(0x1112));
}

int main(void)
{
    test_members_retain_independent_values();
    return 0;
}
