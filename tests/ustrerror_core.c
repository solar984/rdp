// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "test_assert.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

#if defined(RDPLIB_SOURCE_FAITHFUL) || defined(RDP_DEAD_CODE)

#include <stdint.h>
#include <string.h>

#include "ustrerror.h"

typedef struct error_name_t
{
    uint32_t error_number;
    const char *name;
} error_name_t;

static const error_name_t net_error_names[] = {
    {10004u, "WSAEINTR"},           {10009u, "WSAEBADF"},             {10013u, "WSAEACCES"},          {10014u, "WSAEFAULT"},
    {10022u, "WSAEINVAL"},          {10024u, "WSAEMFILE"},            {10035u, "WSAEWOULDBLOCK"},     {10036u, "WSAEINPROGRESS"},
    {10037u, "WSAEALREADY"},        {10039u, "WSAEDESTADDRREQ"},      {10040u, "WSAEMSGSIZE"},        {10041u, "WSAEPROTOTYPE"},
    {10042u, "WSAENOPROTOOPT"},     {10043u, "WSAEPROTONOSUPPORT"},   {10044u, "WSAESOCKTNOSUPPORT"}, {10045u, "WSAEOPNOTSUPP"},
    {10046u, "WSAEPFNOSUPPORT"},    {10047u, "WSAEAFNOSUPPORT"},      {10048u, "WSAEADDRINUSE"},      {10049u, "WSAEADDRNOTAVAIL"},
    {10050u, "WSAENETDOWN"},        {10051u, "WSAENETUNREACH"},       {10052u, "WSAENETRESET"},       {10053u, "WSAECONNABORTED"},
    {10054u, "WSAECONNRESET"},      {10056u, "WSAEISCONN"},           {10057u, "WSAENOTCONN"},        {10058u, "WSAESHUTDOWN"},
    {10059u, "WSAETOOMANYREFS"},    {10060u, "WSAETIMEDOUT"},         {10061u, "WSAECONNREFUSED"},    {10062u, "WSAELOOP"},
    {10063u, "WSAENAMETOOLONG"},    {10064u, "WSAEHOSTDOWN"},         {10065u, "WSAEHOSTUNREACH"},    {10066u, "WSAENOTEMPTY"},
    {10067u, "WSAEPROCLIM"},        {10068u, "WSAEUSERS"},            {10069u, "WSAEDQUOT"},           {10070u, "WSAESTALE"},
    {10071u, "WSAEREMOTE"},         {10091u, "WSASYSNOTREADY"},       {10092u, "WSAVERNOTSUPPORTED"}, {10093u, "WSANOTINITIALISED"},
    {10101u, "WSAEDISCON"},         {11001u, "WSAHOST_NOT_FOUND"},    {11002u, "WSATRY_AGAIN"},       {11003u, "WSANO_RECOVERY"},
    {11004u, "WSANO_DATA"},
};

#if defined(RDP_DEAD_CODE) || (defined(RDPLIB_SOURCE_FAITHFUL) && !defined(_WIN32))
static const error_name_t mac_net_error_names[] = {
    {10102u, "WSAENOMORE"},          {10103u, "WSAECANCELLED"},         {10104u, "WSAEINVALIDPROCTABLE"}, {10105u, "WSAEINVALIDPROVIDER"},
    {10106u, "WSAEPROVIDERFAILEDINIT"}, {10107u, "WSASYSCALLFAILURE"}, {10108u, "WSASERVICE_NOT_FOUND"}, {10109u, "WSATYPE_NOT_FOUND"},
    {10110u, "WSA_E_NO_MORE"},       {10111u, "WSA_E_CANCELLED"},       {10112u, "WSAEREFUSED"},
};
_Static_assert(sizeof(mac_net_error_names) / sizeof(mac_net_error_names[0]) == 11, "Mac net_strerror additional table count");
#endif

_Static_assert(sizeof(net_error_names) / sizeof(net_error_names[0]) == 49, "net_strerror recovered table count");
_Static_assert(_Generic(&net_strerror, char *(*)(uint32_t): 1, default: 0), "net_strerror signature");
#ifdef RDP_DEAD_CODE
_Static_assert(_Generic(&sys_strerror, char *(*)(uint32_t): 1, default: 0), "sys_strerror signature");
_Static_assert(_Generic(&net_strerror_mac, char *(*)(uint32_t): 1, default: 0), "net_strerror_mac signature");
#endif

static void test_network_error_names(void)
{
    size_t index;

    for (index = 0; index < sizeof(net_error_names) / sizeof(net_error_names[0]); ++index)
    {
        assert(strcmp(net_strerror(net_error_names[index].error_number), net_error_names[index].name) == 0);
    }
}

static void test_network_error_fallbacks(void)
{
    assert(strcmp(net_strerror(0), "0") == 0);
    assert(strcmp(net_strerror(10038), "10038") == 0);
    assert(strcmp(net_strerror(10055), "10055") == 0);
#if defined(RDPLIB_SOURCE_FAITHFUL) && !defined(_WIN32)
    assert(strcmp(net_strerror(10102), "WSAENOMORE") == 0);
    assert(strcmp(net_strerror(10112), "WSAEREFUSED") == 0);
#else
    assert(strcmp(net_strerror(10102), "10102") == 0);
    assert(strcmp(net_strerror(10112), "10112") == 0);
#endif
    assert(strcmp(net_strerror(UINT32_MAX), "4294967295") == 0);
}

static void test_network_error_boundaries(void)
{
    static const error_name_t decimal_errors[] = {
        {10003u, "10003"}, {10005u, "10005"}, {10008u, "10008"}, {10010u, "10010"}, {10021u, "10021"}, {10023u, "10023"}, {10025u, "10025"},
        {10034u, "10034"}, {10038u, "10038"}, {10055u, "10055"}, {10072u, "10072"}, {10090u, "10090"}, {10094u, "10094"}, {10100u, "10100"},
        {10113u, "10113"}, {11000u, "11000"}, {11005u, "11005"},
    };
    size_t index;

    for (index = 0; index < sizeof(decimal_errors) / sizeof(decimal_errors[0]); ++index)
    {
        assert(strcmp(net_strerror(decimal_errors[index].error_number), decimal_errors[index].name) == 0);
    }
}

static void test_network_buffer_overwrite(void)
{
    char *first;
    char *second;

    first = net_strerror(7);
    assert(strcmp(first, "7") == 0);
    second = net_strerror(8);
    assert(second == first);
    assert(strcmp(first, "8") == 0);

    assert(strcmp(net_strerror(10004), "WSAEINTR") == 0);
    assert(strcmp(first, "8") == 0);
}

#if defined(RDPLIB_SOURCE_FAITHFUL) && !defined(_WIN32)
static void test_live_mac_network_profile(void)
{
    size_t index;

    for (index = 0; index < sizeof(mac_net_error_names) / sizeof(mac_net_error_names[0]); ++index)
    {
        assert(strcmp(net_strerror(mac_net_error_names[index].error_number), mac_net_error_names[index].name) == 0);
    }
}
#endif

#ifdef RDP_DEAD_CODE
static void test_system_error_numbers(void)
{
    char *first;
    char *second;

    first = sys_strerror(0);
    assert(strcmp(first, "0") == 0);
    second = sys_strerror(UINT32_MAX);
    assert(second == first);
    assert(strcmp(first, "4294967295") == 0);
}

static void test_mac_network_error_profile(void)
{
    size_t index;

    for (index = 0; index < sizeof(net_error_names) / sizeof(net_error_names[0]); ++index)
    {
        assert(strcmp(net_strerror_mac(net_error_names[index].error_number), net_error_names[index].name) == 0);
    }
    for (index = 0; index < sizeof(mac_net_error_names) / sizeof(mac_net_error_names[0]); ++index)
    {
        assert(strcmp(net_strerror_mac(mac_net_error_names[index].error_number), mac_net_error_names[index].name) == 0);
    }

    assert(strcmp(net_strerror_mac(10038), "10038") == 0);
    assert(strcmp(net_strerror_mac(10055), "10055") == 0);
    assert(strcmp(net_strerror_mac(UINT32_MAX), "4294967295") == 0);
}

static void test_separate_system_and_network_buffers(void)
{
    char *network;
    char *system;

    system = sys_strerror(17);
    network = net_strerror(23);
    assert(system != network);
    assert(strcmp(system, "17") == 0);
    assert(strcmp(network, "23") == 0);

    assert(sys_strerror(19) == system);
    assert(strcmp(system, "19") == 0);
    assert(strcmp(network, "23") == 0);

    assert(net_strerror(29) == network);
    assert(strcmp(network, "29") == 0);
    assert(strcmp(system, "19") == 0);
}
#endif

#endif /* RDPLIB_SOURCE_FAITHFUL || RDP_DEAD_CODE */

int main(void)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

#if defined(RDPLIB_SOURCE_FAITHFUL) || defined(RDP_DEAD_CODE)
    test_network_error_names();
    test_network_error_fallbacks();
    test_network_error_boundaries();
    test_network_buffer_overwrite();
#endif
#if defined(RDPLIB_SOURCE_FAITHFUL) && !defined(_WIN32)
    test_live_mac_network_profile();
#endif
#ifdef RDP_DEAD_CODE
    test_system_error_numbers();
    test_mac_network_error_profile();
    test_separate_system_and_network_buffers();
#endif
    return 0;
}
