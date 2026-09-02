// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "server_manager.h"

#include <cstring>
#include <iostream>

ServerManager::ServerManager()
    : m_state(SessionState::Closed),
      m_logout_reason(0),
      m_logout_reply_received(false),
      m_succeeded(false),
      m_closing(false)
{
}

int ServerManager::Connect(const char *host, std::uint16_t port)
{
    if (host == nullptr)
        return RDPLIB_ERROR_INVALID_ARGUMENT;

    int result = m_runtime.Open();
    if (result != RDPLIB_OK)
        return result;

    result = m_endpoint.Open(m_runtime, 0, 1);
    if (result != RDPLIB_OK)
        return result;

    m_connection = m_endpoint.Connect(host, port, &result);
    if (m_connection == nullptr)
        return result;

    result = m_connection->EnableKeepalive();
    if (result == RDPLIB_OK)
        result = m_connection->SetDataRate(ServerDataRate);
    if (result == RDPLIB_OK)
        result = m_connection->SetSendBufferSize(ServerSendBufferSize);
    if (result != RDPLIB_OK)
    {
        StartClose(0);
        return result;
    }

    LoginRequest request{};
    request.protocol_version = ApplicationProtocolVersion;
    ApplicationPacket packet(static_cast<std::uint16_t>(ApplicationOpcode::LoginRequest), &request, sizeof(request));

    m_state = SessionState::LoggingIn;
    result = SendPacket(packet, RDPLIB_SEND_RELIABLE);
    if (result != RDPLIB_CONNECTION_SEND_OK)
    {
        StartClose(0);
        return result;
    }

    m_response_deadline = std::chrono::steady_clock::now() + ResponseTimeout;
    std::cout << "connected to " << host << ':' << port << " and sent login request\n";
    return RDPLIB_OK;
}

bool ServerManager::SendClientPositionUpdate(const ClientPositionUpdate &update)
{
    if (m_state != SessionState::InGame)
        return false;

    ApplicationPacket packet(static_cast<std::uint16_t>(ApplicationOpcode::ClientPositionUpdate), &update, sizeof(update));
    int result = SendPacket(packet, RDPLIB_SEND_UNRELIABLE);
    if (result == RDPLIB_CONNECTION_SEND_OK || result == RDPLIB_CONNECTION_SEND_BUFFER_FULL)
        return true;

    StartClose(0);
    return false;
}

bool ServerManager::SendSaveProfile(const SaveProfile &profile)
{
    if (m_state != SessionState::InGame)
        return false;

    ApplicationPacket packet(static_cast<std::uint16_t>(ApplicationOpcode::SaveProfile), &profile, sizeof(profile));
    int result = SendPacket(packet, RDPLIB_SEND_RELIABLE);
    if (result == RDPLIB_CONNECTION_SEND_OK)
        return true;

    StartClose(0);
    return false;
}

bool ServerManager::SendLogout(LogoutReason reason)
{
    if (m_state != SessionState::InGame)
        return false;

    LogoutRequest request{};
    request.reason = static_cast<std::uint32_t>(reason);
    ApplicationPacket packet(static_cast<std::uint16_t>(ApplicationOpcode::LogoutRequest), &request, sizeof(request));

    int result = SendPacket(packet, RDPLIB_SEND_RELIABLE);
    if (result != RDPLIB_CONNECTION_SEND_OK)
    {
        StartClose(0);
        return false;
    }

    m_logout_reason = request.reason;
    m_state = SessionState::Leaving;
    m_response_deadline = std::chrono::steady_clock::now() + ResponseTimeout;
    return true;
}

bool ServerManager::ProcessNetwork()
{
    int process_result = m_endpoint.Process();
    if (process_result < 0)
    {
        std::cerr << "unable to process the RDP endpoint, result " << process_result << '\n';
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (m_closing)
        return now < m_close_deadline;

    while (m_connection != nullptr)
    {
        RDPMessage message;
        std::uint32_t disconnect_reason = 0;
        RDPConnection::ReceiveResult receive_result = m_connection->Receive(&message, &disconnect_reason);

        if (receive_result == RDPConnection::NoData)
            break;

        if (receive_result == RDPConnection::PeerClosed)
        {
            if (m_state == SessionState::Leaving && m_logout_reply_received)
            {
                std::cout << "server completed the orderly close\n";
                m_succeeded = true;
            }
            else
            {
                std::cerr << "server closed before the session completed\n";
            }

            StartClose(DefaultLingerTimeout);
            return true;
        }

        if (receive_result == RDPConnection::ConnectionLost)
        {
            std::cerr << "server connection was lost, reason 0x" << std::hex << disconnect_reason << std::dec << '\n';
            // The transport has already ended.  There is nothing left to
            // finish with the peer, so release it without linger.
            StartClose(0);
            return false;
        }

        ApplicationPacket packet;
        if (!packet.Decode(message.Data(), message.Size()))
        {
            std::cerr << "server returned an invalid application packet\n";
            StartClose(DefaultLingerTimeout);
            return true;
        }

        HandleResult handle_result = HandleNetworkMessage(packet);
        if (handle_result == HandleResult::Invalid)
        {
            std::cerr << "server returned an invalid application packet\n";
            StartClose(DefaultLingerTimeout);
            return true;
        }
        if (handle_result == HandleResult::Close)
        {
            StartClose(DefaultLingerTimeout);
            return true;
        }
    }

    now = std::chrono::steady_clock::now();
    bool waiting_for_response = m_state == SessionState::LoggingIn ||
        m_state == SessionState::EnteringGame ||
        m_state == SessionState::Leaving;
    if (waiting_for_response && now >= m_response_deadline)
    {
        std::cerr << "timed out waiting for the server\n";
        StartClose(DefaultLingerTimeout);
    }

    return true;
}

bool ServerManager::IsInGame() const
{
    return m_state == SessionState::InGame;
}

ServerManager::SessionState ServerManager::State() const
{
    return m_state;
}

bool ServerManager::Succeeded() const
{
    return m_succeeded;
}

ServerManager::HandleResult ServerManager::HandleNetworkMessage(const ApplicationPacket &packet)
{
    ApplicationOpcode opcode = static_cast<ApplicationOpcode>(packet.Opcode());

    if (opcode == ApplicationOpcode::LoginReply && m_state == SessionState::LoggingIn)
    {
        if (packet.PayloadSize() != sizeof(LoginReply))
            return HandleResult::Invalid;

        LoginReply reply{};
        std::memcpy(&reply, packet.Payload(), sizeof(reply));
        if (reply.accepted == 0)
        {
            std::cerr << "server rejected the login request\n";
            return HandleResult::Close;
        }

        ApplicationPacket ready(static_cast<std::uint16_t>(ApplicationOpcode::ClientReady));
        int result = SendPacket(ready, RDPLIB_SEND_RELIABLE);
        if (result != RDPLIB_CONNECTION_SEND_OK)
        {
            StartClose(0);
            return HandleResult::Close;
        }

        m_state = SessionState::EnteringGame;
        m_response_deadline = std::chrono::steady_clock::now() + ResponseTimeout;
        std::cout << "login accepted; sent client ready\n";
        return HandleResult::Continue;
    }

    if (opcode == ApplicationOpcode::ServerReady && m_state == SessionState::EnteringGame)
    {
        if (packet.PayloadSize() != 0)
            return HandleResult::Invalid;

        m_state = SessionState::InGame;
        std::cout << "server ready; entering game simulation\n";
        return HandleResult::Continue;
    }

    if (opcode == ApplicationOpcode::LogoutReply && m_state == SessionState::Leaving)
    {
        if (packet.PayloadSize() != sizeof(LogoutReply))
            return HandleResult::Invalid;

        LogoutReply reply{};
        std::memcpy(&reply, packet.Payload(), sizeof(reply));
        if (reply.reason != m_logout_reason)
            return HandleResult::Invalid;

        m_logout_reply_received = true;
        std::cout << "server accepted logout reason " << reply.reason << '\n';
        return HandleResult::Continue;
    }

    return HandleResult::Invalid;
}

int ServerManager::SendPacket(const ApplicationPacket &packet, std::uint32_t flags)
{
    if (m_connection == nullptr)
        return RDPLIB_ERROR_NOT_USABLE;

    return m_connection->Send(packet.Data(), packet.Size(), 0, flags);
}

void ServerManager::StartClose(std::uint32_t linger_timeout_ms)
{
    if (m_closing)
        return;

    if (m_connection != nullptr)
    {
        // Close releases the application handle immediately.  The endpoint
        // keeps the transport alive during linger, so the queued FIN still has
        // time to reach the peer.
        m_connection->Close(linger_timeout_ms);
        m_connection.reset();
    }

    m_state = SessionState::Closed;
    m_closing = true;
    m_close_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(linger_timeout_ms);
}
