// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "net_error.h"

#include <stdio.h>

// This was recovered from the clients but we omit the original call sites so this is essentially dead code just here for historical interest.

static const char *net_strerror_common(uint32_t error, int include_provider_errors, char *unknown_error)
{
    switch (error)
    {
    case 10004:
        return "WSAEINTR";
    case 10009:
        return "WSAEBADF";
    case 10013:
        return "WSAEACCES";
    case 10014:
        return "WSAEFAULT";
    case 10022:
        return "WSAEINVAL";
    case 10024:
        return "WSAEMFILE";
    case 10035:
        return "WSAEWOULDBLOCK";
    case 10036:
        return "WSAEINPROGRESS";
    case 10037:
        return "WSAEALREADY";
    case 10039:
        return "WSAEDESTADDRREQ";
    case 10040:
        return "WSAEMSGSIZE";
    case 10041:
        return "WSAEPROTOTYPE";
    case 10042:
        return "WSAENOPROTOOPT";
    case 10043:
        return "WSAEPROTONOSUPPORT";
    case 10044:
        return "WSAESOCKTNOSUPPORT";
    case 10045:
        return "WSAEOPNOTSUPP";
    case 10046:
        return "WSAEPFNOSUPPORT";
    case 10047:
        return "WSAEAFNOSUPPORT";
    case 10048:
        return "WSAEADDRINUSE";
    case 10049:
        return "WSAEADDRNOTAVAIL";
    case 10050:
        return "WSAENETDOWN";
    case 10051:
        return "WSAENETUNREACH";
    case 10052:
        return "WSAENETRESET";
    case 10053:
        return "WSAECONNABORTED";
    case 10054:
        return "WSAECONNRESET";
    case 10056:
        return "WSAEISCONN";
    case 10057:
        return "WSAENOTCONN";
    case 10058:
        return "WSAESHUTDOWN";
    case 10059:
        return "WSAETOOMANYREFS";
    case 10060:
        return "WSAETIMEDOUT";
    case 10061:
        return "WSAECONNREFUSED";
    case 10062:
        return "WSAELOOP";
    case 10063:
        return "WSAENAMETOOLONG";
    case 10064:
        return "WSAEHOSTDOWN";
    case 10065:
        return "WSAEHOSTUNREACH";
    case 10066:
        return "WSAENOTEMPTY";
    case 10067:
        return "WSAEPROCLIM";
    case 10068:
        return "WSAEUSERS";
    case 10069:
        return "WSAEDQUOT";
    case 10070:
        return "WSAESTALE";
    case 10071:
        return "WSAEREMOTE";
    case 10091:
        return "WSASYSNOTREADY";
    case 10092:
        return "WSAVERNOTSUPPORTED";
    case 10093:
        return "WSANOTINITIALISED";
    case 10101:
        return "WSAEDISCON";
    case 11001:
        return "WSAHOST_NOT_FOUND";
    case 11002:
        return "WSATRY_AGAIN";
    case 11003:
        return "WSANO_RECOVERY";
    case 11004:
        return "WSANO_DATA";
    default:
        break;
    }

    // These names exist in both Mac binaries but are absent from the TAKP Windows switch.
    if (include_provider_errors)
    {
        switch (error)
        {
        case 10102:
            return "WSAENOMORE";
        case 10103:
            return "WSAECANCELLED";
        case 10104:
            return "WSAEINVALIDPROCTABLE";
        case 10105:
            return "WSAEINVALIDPROVIDER";
        case 10106:
            return "WSAEPROVIDERFAILEDINIT";
        case 10107:
            return "WSASYSCALLFAILURE";
        case 10108:
            return "WSASERVICE_NOT_FOUND";
        case 10109:
            return "WSATYPE_NOT_FOUND";
        case 10110:
            return "WSA_E_NO_MORE";
        case 10111:
            return "WSA_E_CANCELLED";
        case 10112:
            return "WSAEREFUSED";
        default:
            break;
        }
    }

    // The clients use sprintf into a process global 64 byte buffer. A uint32_t always fits,
    // but another call overwrites the returned text and concurrent calls race.
    sprintf(unknown_error, "%u", error);
    return unknown_error;
}

const char *rdp_net_strerror_mac(uint32_t error)
{
    static char unknown_error[64];
    return net_strerror_common(error, 1, unknown_error);
}

const char *rdp_net_strerror_windows(uint32_t error)
{
    static char unknown_error[64];
    return net_strerror_common(error, 0, unknown_error);
}
