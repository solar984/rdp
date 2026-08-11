// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "client_manager.h"

#include <iostream>
#include <new>
#include <utility>

int ClientManager::Open(std::uint16_t port)
{
    int result = m_runtime.Open();
    if (result != RDPLIB_OK)
        return result;

    result = m_endpoint.Open(m_runtime, port);
    if (result == RDPLIB_OK)
        std::cout << "RDP server listening on UDP port " << m_endpoint.LocalPort() << '\n';

    return result;
}

bool ClientManager::ProcessNetwork()
{
    int process_result = m_endpoint.Process();
    if (process_result < 0)
    {
        std::cerr << "unable to process the RDP endpoint, result " << process_result << '\n';
        return false;
    }

    if (!AcceptClients())
        return false;

    auto client = m_clients.begin();
    while (client != m_clients.end())
    {
        if (!(*client)->ProcessNetwork())
        {
            std::cout << "removing client " << (*client)->Description() << '\n';
            client = m_clients.erase(client);
        }
        else
        {
            ++client;
        }
    }

    return true;
}

bool ClientManager::AcceptClients()
{
    int accept_result = RDPLIB_OK;

    for (std::uint32_t accepted = 0; accepted < MaximumClientAcceptsPerTick; ++accepted)
    {
        std::unique_ptr<RDPConnection> connection = m_endpoint.Accept(&accept_result);
        if (connection == nullptr)
            break;

        int setup_result = connection->EnableKeepalive();
        if (setup_result == RDPLIB_OK)
            setup_result = connection->SetDataRate(ClientDataRate);
        if (setup_result == RDPLIB_OK)
            setup_result = connection->SetSendBufferSize(ClientSendBufferSize);

        if (setup_result != RDPLIB_OK)
        {
            std::cerr << "unable to configure an accepted connection, result " << setup_result << '\n';
            connection->Close(0);
            continue;
        }

        try
        {
            auto client = std::make_unique<Client>(std::move(connection));
            std::cout << "accepted client " << client->Description() << '\n';
            m_clients.emplace_back(std::move(client));
        }
        catch (const std::bad_alloc &)
        {
            accept_result = RDPLIB_ERROR_OUT_OF_MEMORY;
            break;
        }
    }

    if (accept_result != RDPLIB_OK)
    {
        std::cerr << "unable to accept an RDP client, result " << accept_result << '\n';
        return false;
    }

    return true;
}
