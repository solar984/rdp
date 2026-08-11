// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#ifndef RDP_EXAMPLE_GAME_MANAGER_H
#define RDP_EXAMPLE_GAME_MANAGER_H

#include <chrono>
#include <cstdint>

class ServerManager;

class GameManager
{
public:
    GameManager();

    // Main calls this only while the server session is InGame.
    bool ProcessGame(ServerManager &server);

private:
    static constexpr auto PositionUpdateInterval = std::chrono::seconds(1);
    static constexpr auto ProfileSaveInterval = std::chrono::seconds(6);
    static constexpr auto GameDuration = std::chrono::seconds(30);

    bool m_started;
    std::uint32_t m_position_sequence;
    std::uint32_t m_save_sequence;
    float m_x;
    float m_y;
    float m_z;
    float m_heading;
    std::chrono::steady_clock::time_point m_started_at;
    std::chrono::steady_clock::time_point m_next_position_update;
    std::chrono::steady_clock::time_point m_next_profile_save;
    std::chrono::steady_clock::time_point m_logout_time;
};

#endif // RDP_EXAMPLE_GAME_MANAGER_H
