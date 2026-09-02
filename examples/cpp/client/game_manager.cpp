// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

#include "game_manager.h"

#include "application_protocol.h"
#include "server_manager.h"

#include <iostream>

GameManager::GameManager()
    : m_started(false),
      m_position_sequence(0),
      m_save_sequence(0),
      m_x(0.0f),
      m_y(0.0f),
      m_z(0.0f),
      m_heading(0.0f)
{
}

bool GameManager::ProcessGame(ServerManager &server)
{
    auto now = std::chrono::steady_clock::now();
    if (!m_started)
    {
        m_started = true;
        m_started_at = now;
        m_next_position_update = now;
        m_next_profile_save = now + ProfileSaveInterval;
        m_logout_time = now + GameDuration;
        std::cout << "game simulation started\n";
    }

    if (now >= m_logout_time)
    {
        if (!server.SendLogout(LogoutReason::ExampleComplete))
            return false;

        std::cout << "requested logout with reason " << static_cast<std::uint32_t>(LogoutReason::ExampleComplete) << '\n';
        return true;
    }

    // ClientPositionUpdate - an example unreliable gameplay message sent to the server
    if (now >= m_next_position_update)
    {
        ++m_position_sequence;
        m_x += 1.0f;
        m_y += 0.5f;
        m_heading += 8.0f;

        ClientPositionUpdate update{};
        update.sequence = m_position_sequence;
        update.x = m_x;
        update.y = m_y;
        update.z = m_z;
        update.heading = m_heading;

        if (!server.SendClientPositionUpdate(update))
            return false;

        std::cout << "sent unreliable position update " << update.sequence << '\n';
        m_next_position_update = now + PositionUpdateInterval;
    }

    // SaveProfile - an example reliable gameplay message sent to the server
    if (now >= m_next_profile_save)
    {
        SaveProfile profile{};
        profile.sequence = ++m_save_sequence;
        profile.seconds_in_game = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now - m_started_at).count()
        );
        profile.x = m_x;
        profile.y = m_y;
        profile.z = m_z;
        profile.heading = m_heading;

        if (!server.SendSaveProfile(profile))
            return false;

        std::cout << "sent reliable profile save " << profile.sequence << '\n';
        m_next_profile_save = now + ProfileSaveInterval;
    }

    return true;
}
