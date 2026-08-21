// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "client_manager.h"

#include <iostream>
#include <new>
#include <utility>

namespace
{
void LogSocketBuffer(const char *name, std::uint32_t requested_bytes, std::uint32_t actual_bytes)
{
    std::cout << "RDP " << name << " socket buffer requested " << requested_bytes << " bytes, operating system reported " << actual_bytes << " bytes\n";
    if (actual_bytes >= requested_bytes)
        return;

    std::cerr << "warning: the operating system supplied less " << name << " socket buffer space than requested\n";
    std::cerr << "if running on Linux, check net.core.rmem_max and net.core.wmem_max\n";
}

}

int ClientManager::Open(std::uint16_t port)
{
    rdplib_endpoint_options_t endpoint_options{};
    std::uint32_t receive_socket_buffer_bytes;
    std::uint32_t send_socket_buffer_bytes;

    int result = m_runtime.Open();
    if (result != RDPLIB_OK)
        return result;

    endpoint_options.structure_size = sizeof(endpoint_options);
    endpoint_options.receive_socket_buffer_bytes = ReceiveSocketBufferSize;
    endpoint_options.send_socket_buffer_bytes = SendSocketBufferSize;
    result = m_endpoint.Open(m_runtime, port, endpoint_options);
    if (result != RDPLIB_OK)
        return result;

    result = m_endpoint.GetSocketReceiveBufferSize(receive_socket_buffer_bytes);
    if (result != RDPLIB_OK)
        return result;
    result = m_endpoint.GetSocketSendBufferSize(send_socket_buffer_bytes);
    if (result != RDPLIB_OK)
        return result;

    LogSocketBuffer("receive", ReceiveSocketBufferSize, receive_socket_buffer_bytes);
    LogSocketBuffer("send", SendSocketBufferSize, send_socket_buffer_bytes);
    std::cout << "RDP server listening on UDP port " << m_endpoint.LocalPort() << '\n';

    return RDPLIB_OK;
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
