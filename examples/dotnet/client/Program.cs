// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

using System;
using System.Threading;
using RdplibExample.Common;

namespace RdplibExample.Client
{
    internal static class Program
    {
        private static int Main()
        {
            const int TickIntervalMs = 10;

            using (ServerManager server = new ServerManager())
            {
                int connectResult = server.Connect("127.0.0.1", 9000);
                if (connectResult != RdplibNative.Ok)
                {
                    Console.Error.WriteLine("unable to connect to the RDP server, result {0}", connectResult);
                    return 1;
                }

                GameManager game = new GameManager();
                while (server.ProcessNetwork())
                {
                    // LoggingIn, EnteringGame and Leaving continue network
                    // processing but do not run game simulation.
                    if (server.IsInGame && !game.ProcessGame(server))
                        return 1;

                    Thread.Sleep(TickIntervalMs);
                }

                return server.Succeeded ? 0 : 1;
            }
        }
    }
}
