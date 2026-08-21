// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_CLIENT_MANAGER_H
#define RDP_EXAMPLE_CLIENT_MANAGER_H

#include "client.h"
#include "rdp_endpoint.h"

#include <cstdint>
#include <list>
#include <memory>

class ClientManager
{
public:
    ClientManager() = default;
    ~ClientManager() = default;

    ClientManager(const ClientManager &) = delete;
    ClientManager &operator=(const ClientManager &) = delete;

    int Open(std::uint16_t port);

    // Returning false tells main that the server can no longer continue.
    bool ProcessNetwork();

private:
    using ClientList = std::list<std::unique_ptr<Client>>;

    static constexpr std::uint32_t MaximumClientAcceptsPerTick = 5;
    static constexpr std::uint32_t ClientDataRate = 400u * 1024u;
    static constexpr std::uint32_t ClientSendBufferSize = 1024u * 1024u;
    static constexpr std::uint32_t ReceiveSocketBufferSize = 1024u * 1024u;
    static constexpr std::uint32_t SendSocketBufferSize = 1024u * 1024u;

    bool AcceptClients();

    // Declaration order is intentional.  Destruction runs in reverse so
    // clients release connections before the endpoint and runtime close.
    RDPRuntime m_runtime;
    RDPEndpoint m_endpoint;
    ClientList m_clients;
};

#endif // RDP_EXAMPLE_CLIENT_MANAGER_H
