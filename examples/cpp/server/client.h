// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_SERVER_CLIENT_H
#define RDP_EXAMPLE_SERVER_CLIENT_H

#include "application_packet.h"
#include "application_protocol.h"
#include "rdp_connection.h"

#include <cstdint>
#include <memory>
#include <string>

class Client
{
public:
    enum class SessionState
    {
        AwaitingLogin,
        AwaitingClientReady,
        AwaitingFirstUpdate,
        InGame,
        Leaving
    };

    explicit Client(std::unique_ptr<RDPConnection> connection);
    ~Client();

    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // Returning false tells ClientManager to remove this client.
    bool ProcessNetwork();
    bool IsInGame() const;

    const std::string &Description() const
    {
        return m_description;
    }

private:
    enum class HandleResult
    {
        Continue,
        Close,
        Invalid
    };

    static constexpr std::uint32_t DefaultLingerTimeout = 1000;

    HandleResult HandleNetworkMessage(const ApplicationPacket &packet);
    bool SendPacket(const ApplicationPacket &packet, std::uint32_t flags);
    void Close(std::uint32_t linger_timeout_ms);

    std::unique_ptr<RDPConnection> m_connection;
    std::string m_description;
    SessionState m_state;
    ClientPositionUpdate m_last_position;
};

#endif // RDP_EXAMPLE_SERVER_CLIENT_H
