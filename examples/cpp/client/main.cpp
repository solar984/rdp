// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "game_manager.h"
#include "server_manager.h"

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    constexpr auto TickInterval = std::chrono::milliseconds(10);

    ServerManager server;
    int connect_result = server.Connect("127.0.0.1", 9000);
    if (connect_result != RDPLIB_OK)
    {
        std::cerr << "unable to connect to the RDP server, result " << connect_result << '\n';
        return 1;
    }

    GameManager game;
    while (server.ProcessNetwork())
    {
        // LoggingIn, EnteringGame and Leaving continue network processing but
        // do not run game simulation.
        if (server.IsInGame() && !game.ProcessGame(server))
            return 1;

        std::this_thread::sleep_for(TickInterval);
    }

    return server.Succeeded() ? 0 : 1;
}
