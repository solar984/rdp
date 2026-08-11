// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#include "client_manager.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace
{
volatile std::sig_atomic_t run_server = 1;

void StopServer(int)
{
    run_server = 0;
}

}

int main()
{
    constexpr auto TickInterval = std::chrono::milliseconds(32);

    std::signal(SIGINT, StopServer);
    std::signal(SIGTERM, StopServer);

    ClientManager clients;
    int open_result = clients.Open(9000);
    if (open_result != RDPLIB_OK)
    {
        std::cerr << "unable to start the RDP server, result " << open_result << '\n';
        return 1;
    }

    auto next_tick = std::chrono::steady_clock::now();
    while (run_server)
    {
        if (!clients.ProcessNetwork())
            return 1;

        // All client input has been applied.  A real server would now simulate
        // only clients for which Client::IsInGame() is true.

        next_tick += TickInterval;
        auto now = std::chrono::steady_clock::now();
        if (next_tick < now)
            next_tick = now;

        std::this_thread::sleep_until(next_tick);
    }

    std::cout << "server stopped\n";
    return 0;
}
