// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDPLIB_RDP_INTERNAL_H
#define RDPLIB_RDP_INTERNAL_H

#include <stdint.h>

#include "rdplib_platform.h"

typedef struct rdp_t rdp_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Host error adapter. This is intentionally not part of the recovered raw API.
void rdplib_rdp_handle_reported_icmp(rdp_t *rdp, struct sockaddr *remote_addr, uint8_t type, uint8_t code, uint8_t trace_response, uint8_t trace_sample, struct sockaddr_in *source_addr);

#ifdef __cplusplus
}
#endif

#endif /* RDPLIB_RDP_INTERNAL_H */
