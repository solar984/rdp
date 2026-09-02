// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "client.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <utility>

Client::Client(std::unique_ptr<RDPConnection> connection)
    : m_connection(std::move(connection)),
      m_description("unknown"),
      m_state(SessionState::AwaitingLogin),
      m_last_position{}
{
    if (m_connection == nullptr)
        return;

    std::uint8_t address[4]{};
    std::uint16_t port = 0;
    if (m_connection->GetRemoteAddress(address, port) != RDPLIB_OK)
        return;

    char description[32]{};
    std::snprintf(
        description,
        sizeof(description),
        "%u.%u.%u.%u:%u",
        static_cast<unsigned>(address[0]),
        static_cast<unsigned>(address[1]),
        static_cast<unsigned>(address[2]),
        static_cast<unsigned>(address[3]),
        static_cast<unsigned>(port)
    );
    m_description = description;
}

Client::~Client()
{
    Close(0);
}

bool Client::ProcessNetwork()
{
    while (m_connection != nullptr)
    {
        RDPMessage message;
        std::uint32_t disconnect_reason = 0;
        RDPConnection::ReceiveResult receive_result = m_connection->Receive(&message, &disconnect_reason);

        if (receive_result == RDPConnection::NoData)
            return true;

        if (receive_result == RDPConnection::PeerClosed)
        {
            std::cout << "client " << m_description << " closed its connection\n";
            Close(DefaultLingerTimeout);
            return false;
        }

        if (receive_result == RDPConnection::ConnectionLost)
        {
            std::cout << "client " << m_description << " lost its connection, reason 0x" << std::hex << disconnect_reason << std::dec << '\n';
            // The transport has already ended.  There is nothing left to
            // finish with the peer, so release it without linger.
            Close(0);
            return false;
        }

        ApplicationPacket packet;
        if (!packet.Decode(message.Data(), message.Size()))
        {
            std::cerr << "received an invalid application packet from " << m_description << '\n';
            Close(DefaultLingerTimeout);
            return false;
        }

        HandleResult handle_result = HandleNetworkMessage(packet);
        if (handle_result == HandleResult::Invalid)
        {
            std::cerr << "received an invalid application packet from " << m_description << '\n';
            Close(DefaultLingerTimeout);
            return false;
        }
        if (handle_result == HandleResult::Close)
        {
            Close(DefaultLingerTimeout);
            return false;
        }
    }

    return false;
}

bool Client::IsInGame() const
{
    return m_state == SessionState::InGame;
}

Client::HandleResult Client::HandleNetworkMessage(const ApplicationPacket &packet)
{
    ApplicationOpcode opcode = static_cast<ApplicationOpcode>(packet.Opcode());

    // LoginRequest
    if (opcode == ApplicationOpcode::LoginRequest && m_state == SessionState::AwaitingLogin)
    {
        if (packet.PayloadSize() != sizeof(LoginRequest))
            return HandleResult::Invalid;

        LoginRequest request{};
        std::memcpy(&request, packet.Payload(), sizeof(request));

        LoginReply reply{};
        reply.accepted = request.protocol_version == ApplicationProtocolVersion ? 1u : 0u;
        ApplicationPacket response(static_cast<std::uint16_t>(ApplicationOpcode::LoginReply), &reply, sizeof(reply));
        if (!SendPacket(response, RDPLIB_SEND_RELIABLE))
            return HandleResult::Close;

        if (reply.accepted == 0)
        {
            std::cout << "rejected login from " << m_description << '\n';
            return HandleResult::Close;
        }

        m_state = SessionState::AwaitingClientReady;
        std::cout << "accepted login from " << m_description << "; waiting for client ready\n";
        return HandleResult::Continue;
    }

    // ClientReady
    if (opcode == ApplicationOpcode::ClientReady && m_state == SessionState::AwaitingClientReady)
    {
        if (packet.PayloadSize() != 0)
            return HandleResult::Invalid;

        ApplicationPacket response(static_cast<std::uint16_t>(ApplicationOpcode::ServerReady));
        if (!SendPacket(response, RDPLIB_SEND_RELIABLE))
            return HandleResult::Close;

        m_state = SessionState::AwaitingFirstUpdate;
        std::cout << "client " << m_description << " is ready; waiting for its first position update\n";
        return HandleResult::Continue;
    }

    // ClientPositionUpdate - an example unreliable gameplay message from the client
    if (opcode == ApplicationOpcode::ClientPositionUpdate &&
        (m_state == SessionState::AwaitingFirstUpdate || m_state == SessionState::InGame))
    {
        if (packet.PayloadSize() != sizeof(ClientPositionUpdate))
            return HandleResult::Invalid;

        ClientPositionUpdate update{};
        std::memcpy(&update, packet.Payload(), sizeof(update));
        m_last_position = update;

        if (m_state == SessionState::AwaitingFirstUpdate)
        {
            m_state = SessionState::InGame;
            std::cout << "client " << m_description << " entered game simulation\n";
        }

        std::cout << "position " << update.sequence << " from " << m_description
                  << " is (" << update.x << ", " << update.y << ", " << update.z << ")\n";

        // A real server would attach this client's entity ID and fan the
        // update out to the other clients participating in game simulation.
        return HandleResult::Continue;
    }

    // SaveProfile - an example reliable gameplay message from the client
    if (opcode == ApplicationOpcode::SaveProfile)
    {
        if (packet.PayloadSize() != sizeof(SaveProfile))
            return HandleResult::Invalid;

        if (m_state == SessionState::AwaitingFirstUpdate)
        {
            std::cout << "ignored an early profile save from " << m_description << '\n';
            return HandleResult::Continue;
        }
        if (m_state != SessionState::InGame)
            return HandleResult::Invalid;

        SaveProfile profile{};
        std::memcpy(&profile, packet.Payload(), sizeof(profile));
        std::cout << "saved profile " << profile.sequence << " for " << m_description
                  << " after " << profile.seconds_in_game << " seconds\n";
        return HandleResult::Continue;
    }

    // LogoutRequest
    if (opcode == ApplicationOpcode::LogoutRequest &&
        (m_state == SessionState::AwaitingFirstUpdate || m_state == SessionState::InGame))
    {
        if (packet.PayloadSize() != sizeof(LogoutRequest))
            return HandleResult::Invalid;

        LogoutRequest request{};
        std::memcpy(&request, packet.Payload(), sizeof(request));
        m_state = SessionState::Leaving;
        std::cout << "client " << m_description << " requested logout with reason " << request.reason << '\n';

        LogoutReply reply{};
        reply.reason = request.reason;
        ApplicationPacket response(static_cast<std::uint16_t>(ApplicationOpcode::LogoutReply), &reply, sizeof(reply));
        (void)SendPacket(response, RDPLIB_SEND_RELIABLE);
        return HandleResult::Close;
    }

    return HandleResult::Invalid;
}

bool Client::SendPacket(const ApplicationPacket &packet, std::uint32_t flags)
{
    int result = m_connection->Send(packet.Data(), packet.Size(), 0, flags);
    if (result == RDPLIB_CONNECTION_SEND_OK)
        return true;

    std::cerr << "unable to send an application packet to " << m_description << ", result " << result << '\n';
    Close(0);
    return false;
}

void Client::Close(std::uint32_t linger_timeout_ms)
{
    if (m_connection == nullptr)
        return;

    // Close releases the application handle immediately.  The endpoint keeps
    // the transport alive during linger, so removing this Client does not
    // discard a queued reply or FIN.
    m_connection->Close(linger_timeout_ms);
    m_connection.reset();
}
