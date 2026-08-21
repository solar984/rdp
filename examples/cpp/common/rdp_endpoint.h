// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_ENDPOINT_H
#define RDP_EXAMPLE_ENDPOINT_H

#include "rdp_runtime.h"

#include <cstdint>
#include <memory>

class RDPConnection;

// Owns one rdplib endpoint.  The application owns its connections.
class RDPEndpoint
{
public:
    RDPEndpoint();
    ~RDPEndpoint();

    RDPEndpoint(const RDPEndpoint &) = delete;
    RDPEndpoint &operator=(const RDPEndpoint &) = delete;

    // expected_connections selects the internal hash layout.  It is not a
    // connection limit.
    int Open(RDPRuntime &runtime, std::uint16_t local_port, std::uint32_t expected_connections = 100, std::uint32_t flags = RDPLIB_USE_CRC);
    int Open(RDPRuntime &runtime, std::uint16_t local_port, const rdplib_endpoint_options_t &options, std::uint32_t expected_connections = 100,
             std::uint32_t flags = RDPLIB_USE_CRC);
    int Close();

    int Process(std::int32_t timeout_ms = 0);

    // A null result with RDPLIB_OK means no connection is waiting.
    std::unique_ptr<RDPConnection> Accept(int *result = nullptr);
    std::unique_ptr<RDPConnection> Connect(const char *host, std::uint16_t port, int *result = nullptr);

    std::uint16_t LocalPort() const;
    int SetSocketReceiveBufferSize(std::uint32_t bytes);
    int SetSocketSendBufferSize(std::uint32_t bytes);
    int GetSocketReceiveBufferSize(std::uint32_t &bytes) const;
    int GetSocketSendBufferSize(std::uint32_t &bytes) const;

private:
    std::unique_ptr<RDPConnection> WrapConnection(rdplib_connection_t *connection, int *result);
    void DiscardConnectionless();

    rdplib_endpoint_t *m_endpoint;
};

#endif // RDP_EXAMPLE_ENDPOINT_H
