// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <netdb.h>
#include <sys/socket.h>
#endif

#include "test_assert.h"
#include "trace.h"

_Static_assert(_Generic(&format_trace, uint32_t (*)(char *, trace_probe_t *, uint32_t, uint32_t): 1, default: 0), "format_trace signature");
_Static_assert(_Generic((Ptrace_probe_t)0, trace_probe_t *: 1, default: 0), "Ptrace_probe_t type");
_Static_assert(offsetof(trace_probe_t, time_sent) == 0x00, "trace_probe_t::time_sent offset");
_Static_assert(offsetof(trace_probe_t, reply_time) == 0x04, "trace_probe_t::reply_time offset");
_Static_assert(offsetof(trace_probe_t, icmp_from) == 0x08, "trace_probe_t::icmp_from offset");
_Static_assert(offsetof(trace_probe_t, ttl) == 0x0C, "trace_probe_t::ttl offset");
_Static_assert(offsetof(trace_probe_t, icmp_type) == 0x0D, "trace_probe_t::icmp_type offset");
_Static_assert(offsetof(trace_probe_t, icmp_code) == 0x0E, "trace_probe_t::icmp_code offset");
_Static_assert(sizeof(trace_probe_t) == 0x10, "trace_probe_t size");

#ifdef _WIN32
#define TRACE_TEST_NETCALL WSAAPI
#else
#define TRACE_TEST_NETCALL
#endif

static uint32_t lookup_calls;
static uint32_t numeric_calls;
static uint32_t last_lookup_length;
static int last_lookup_family;
static struct in_addr last_lookup_address;
static int lookup_mode;
static struct hostent lookup_result;
static char lookup_name[320];

#ifdef _WIN32
struct hostent *TRACE_TEST_NETCALL trace_test_gethostbyaddr(const char *address, int length, int family)
#else
struct hostent *trace_test_gethostbyaddr(const void *address, socklen_t length, int family)
#endif
{
    ++lookup_calls;
    last_lookup_length = (uint32_t)length;
    last_lookup_family = family;
    memcpy(&last_lookup_address, address, sizeof(last_lookup_address));
    if (lookup_mode == 0)
    {
        return NULL;
    }

    memset(&lookup_result, 0, sizeof(lookup_result));
    lookup_result.h_name = lookup_mode == 1 ? lookup_name : NULL;
    return &lookup_result;
}

char *TRACE_TEST_NETCALL trace_test_inet_ntoa(struct in_addr address)
{
    static char numeric[16];
    const uint8_t *octets = (const uint8_t *)&address.s_addr;

    ++numeric_calls;
    sprintf(numeric, "%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
    return numeric;
}

static struct in_addr make_address(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    const uint8_t octets[4] = {a, b, c, d};
    struct in_addr address;

    memcpy(&address.s_addr, octets, sizeof(octets));
    return address;
}

static void reset_resolver(void)
{
    lookup_calls = 0;
    numeric_calls = 0;
    last_lookup_length = 0;
    last_lookup_family = 0;
    memset(&last_lookup_address, 0, sizeof(last_lookup_address));
    lookup_mode = 0;
}

static void test_layout_and_empty_trace(void)
{
    char buffer[256];
    char expected[256];
    char *position;
    uint32_t i;
    uint32_t size;

    position = expected;
    for (i = 1; i <= 30; ++i)
    {
        position += sprintf(position, " %2u \n", i);
    }

    memset(buffer, 0xA5, sizeof(buffer));
    size = format_trace(buffer, NULL, 0, 0);
    assert(size == 150);
    assert(size == (uint32_t)strlen(buffer));
    assert(strcmp(buffer, expected) == 0);
}

static void test_ttl_zero_leaves_buffer_untouched(void)
{
    char buffer[8];
    trace_probe_t probe;

    memset(buffer, 0x5A, sizeof(buffer));
    memset(&probe, 0, sizeof(probe));
    probe.icmp_type = 3;
    probe.icmp_code = 3;
    assert(format_trace(buffer, &probe, 1, 0) == 0);
    assert((uint8_t)buffer[0] == 0x5A);
}

static void test_formatting_and_route_cap(void)
{
    char buffer[2048];
    char expected[2048];
    char *position;
    trace_probe_t probes[10];
    struct in_addr first = make_address(1, 2, 3, 4);
    struct in_addr second = make_address(5, 6, 7, 8);
    uint32_t size;

    memset(probes, 0, sizeof(probes));
    probes[0].ttl = 1;
    probes[1] = (trace_probe_t){0, 7, first, 1, 11, 0};
    probes[2] = (trace_probe_t){0, 8, first, 1, 3, 0};
    probes[3].ttl = 1;
    probes[4] = (trace_probe_t){0, 9, first, 1, 3, 1};
    probes[5] = (trace_probe_t){0, 10, second, 1, 3, 6};
    probes[6] = (trace_probe_t){0, 11, second, 2, 3, 7};
    probes[7] = (trace_probe_t){0, 12, second, 2, 2, 9};
    probes[8] = (trace_probe_t){0, 13, second, 2, 3, 3};
    probes[9] = (trace_probe_t){0, 14, first, 3, 3, 0};

    position = expected;
    position += sprintf(position, " %2u ", 1u);
    position += sprintf(position, "   * ");
    position += sprintf(position, "%36s ", "1.2.3.4");
    position += sprintf(position, "%4u%s ", 7u, "");
    position += sprintf(position, "%4u%s ", 8u, "N");
    position += sprintf(position, "   * ");
    position += sprintf(position, "%4u%s ", 9u, "H");
    position += sprintf(position, "\n    ");
    position += sprintf(position, "%36s ", "5.6.7.8");
    position += sprintf(position, "%4u%s ", 10u, "NU");
    position += sprintf(position, "\n");
    position += sprintf(position, " %2u ", 2u);
    position += sprintf(position, "%36s ", "5.6.7.8");
    position += sprintf(position, "%4u%s ", 11u, "HU");
    position += sprintf(position, "%u ICMP[%u][%u] ", 12u, 2u, 9u);
    position += sprintf(position, "%4u%s ", 13u, "");
    position += sprintf(position, "\n");

    reset_resolver();
    size = format_trace(buffer, probes, 10, 0);
    assert(size == (uint32_t)strlen(expected));
    assert(size == (uint32_t)strlen(buffer));
    assert(strcmp(buffer, expected) == 0);
    assert(lookup_calls == 0);
    assert(numeric_calls == 3);
}

static void test_lookup_and_fallback(void)
{
    char buffer[1024];
    char expected[1024];
    trace_probe_t probes[2];
    struct in_addr address = make_address(9, 8, 7, 6);
    uint32_t i;

    for (i = 0; i + 1 < sizeof(lookup_name); ++i)
    {
        lookup_name[i] = (char)('a' + i % 26u);
    }
    lookup_name[sizeof(lookup_name) - 1] = '\0';

    memset(probes, 0, sizeof(probes));
    probes[0] = (trace_probe_t){0, 20, address, 1, 11, 0};
    probes[1] = (trace_probe_t){0, 21, address, 1, 3, 3};

    reset_resolver();
    lookup_mode = 1;
    sprintf(expected, " %2u %36s %4u%s %4u%s \n", 1u, lookup_name, 20u, "", 21u, "");
    assert(format_trace(buffer, probes, 2, 1) == (uint32_t)strlen(expected));
    assert(strcmp(buffer, expected) == 0);
    assert(lookup_calls == 1 && numeric_calls == 0);
    assert(last_lookup_length == sizeof(struct in_addr) && last_lookup_family == AF_INET);
    assert(last_lookup_address.s_addr == address.s_addr);

    reset_resolver();
    lookup_mode = 2;
    sprintf(expected, " %2u %36s %4u%s %4u%s \n", 1u, "9.8.7.6", 20u, "", 21u, "");
    assert(format_trace(buffer, probes, 2, 1) == (uint32_t)strlen(expected));
    assert(strcmp(buffer, expected) == 0);
    assert(lookup_calls == 1 && numeric_calls == 1);
}

int main(void)
{
    test_layout_and_empty_trace();
    test_ttl_zero_leaves_buffer_untouched();
    test_formatting_and_route_cap();
    test_lookup_and_fallback();
    return 0;
}
