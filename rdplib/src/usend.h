// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

// Datagram builder called by tx_send_packet.
#ifndef RDP_USEND_H
#define RDP_USEND_H

#include <stdint.h>

struct connection_t;

#ifndef RDPLIB_BUFFER_T_DEFINED
#define RDPLIB_BUFFER_T_DEFINED
typedef struct rdp_buffer_t
{
    const void *data;
    uint32_t bytes;
} rdp_buffer_t;
#endif

#ifdef __cplusplus
extern "C"
{
#endif

// Join the buffers in a fixed 32768 byte stack buffer.  Add a network order
// CRC when CRC or encryption is enabled.  Encryption adds 1 through 8
// random padding bytes and encodes the buffer in place.  Send a datagram and
// return 0, 1, or 5.
//
// The default build checks the vector pointers and final size.
// RDPLIB_SOURCE_FAITHFUL keeps the recovered unchecked assumptions.
int usend(intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc);

// Normal connected send path with the packet drop hook after the buffers have been joined.
#ifndef RDPLIB_SOURCE_FAITHFUL
int rdplib_usend(struct connection_t *connection, intptr_t endpoint, const rdp_buffer_t *buffers, uint32_t buffer_count, const uint8_t destination[16], int use_encryption, int use_crc);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RDP_USEND_H */
