// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_NET_ERROR_H
#define RDP_NET_ERROR_H

#include <stdint.h>

// This was recovered from the clients but we omit the original call sites so this is essentially dead code just here for historical interest.

#ifdef __cplusplus
extern "C"
{
#endif

// Use the error name table linked into both EQMac game clients.  Unknown
// values are formatted in a shared static 64 byte buffer.
const char *rdp_net_strerror_mac(uint32_t error);

// Use the shorter error table linked into TAKP Windows.  Unknown values use a
// separate shared static buffer.
const char *rdp_net_strerror_windows(uint32_t error);

// Format a classic Mac sockaddr.  The output has no size argument.  Family 6
// returns without writing anything, matching both Mac game clients.
int rdp_format_sockaddr_mac(char *output, const uint8_t address[16]);

// Format a Win32 sockaddr.  The output has no size argument.  The family 6
// source passes 2 uninitialized 16 byte arrays to `%s`, so it must not be
// called for a normal RDP endpoint.
int rdp_format_sockaddr_windows(char *output, const uint8_t address[16]);

#ifdef __cplusplus
}
#endif

#endif /* RDP_NET_ERROR_H */
