// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_SERVER_MANAGER_H
#define RDP_EXAMPLE_SERVER_MANAGER_H

#include "application_packet.h"
#include "application_protocol.h"
#include "rdp_connection.h"
#include "rdp_endpoint.h"

#include <chrono>
#include <cstdint>
#include <memory>

class ServerManager
{
public:
    enum class SessionState
    {
        Closed,
        LoggingIn,
        EnteringGame,
        InGame,
        Leaving
    };

    ServerManager();
    ~ServerManager() = default;

    ServerManager(const ServerManager &) = delete;
    ServerManager &operator=(const ServerManager &) = delete;

    int Connect(const char *host, std::uint16_t port);

    bool SendClientPositionUpdate(const ClientPositionUpdate &update);
    bool SendSaveProfile(const SaveProfile &profile);
    bool SendLogout(LogoutReason reason);

    // Returning false tells main that this session is finished.
    bool ProcessNetwork();
    bool IsInGame() const;
    SessionState State() const;
    bool Succeeded() const;

private:
    enum class HandleResult
    {
        Continue,
        Close,
        Invalid
    };

    static constexpr std::uint32_t DefaultLingerTimeout = 1000;
    static constexpr std::uint32_t ServerDataRate = 400u * 1024u;
    static constexpr std::uint32_t ServerSendBufferSize = 1024u * 1024u;
    static constexpr auto ResponseTimeout = std::chrono::seconds(10);

    HandleResult HandleNetworkMessage(const ApplicationPacket &packet);
    int SendPacket(const ApplicationPacket &packet, std::uint32_t flags);
    void StartClose(std::uint32_t linger_timeout_ms);

    // Declaration order is intentional.  Destruction runs in reverse so the
    // connection is released before the endpoint and runtime close.
    RDPRuntime m_runtime;
    RDPEndpoint m_endpoint;
    std::unique_ptr<RDPConnection> m_connection;

    SessionState m_state;
    std::uint32_t m_logout_reason;
    bool m_logout_reply_received;
    bool m_succeeded;
    bool m_closing;
    std::chrono::steady_clock::time_point m_response_deadline;
    std::chrono::steady_clock::time_point m_close_deadline;
};

#endif // RDP_EXAMPLE_SERVER_MANAGER_H
