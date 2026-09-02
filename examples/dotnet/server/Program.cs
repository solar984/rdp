// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using System.Threading;
using RdplibExample.Common;

namespace RdplibExample.Server
{
    internal static class Program
    {
        private static volatile bool _runServer = true;

        private static int Main()
        {
            const int TickIntervalMs = 32;

            Console.CancelKeyPress += StopServer;

            using (ClientManager clients = new ClientManager())
            {
                int openResult = clients.Open(9000);
                if (openResult != RdplibNative.Ok)
                {
                    Console.Error.WriteLine("unable to start the RDP server, result {0}", openResult);
                    return 1;
                }

                Stopwatch clock = Stopwatch.StartNew();
                long nextTick = 0;
                while (_runServer)
                {
                    if (!clients.ProcessNetwork())
                        return 1;

                    // All client input has been applied. A real server would
                    // now simulate only clients for which IsInGame is true.

                    nextTick += TickIntervalMs;
                    long now = clock.ElapsedMilliseconds;
                    if (nextTick < now)
                    {
                        nextTick = now;
                    }
                    else
                    {
                        Thread.Sleep((int)(nextTick - now));
                    }
                }
            }

            Console.WriteLine("server stopped");
            return 0;
        }

        private static void StopServer(object sender, ConsoleCancelEventArgs arguments)
        {
            arguments.Cancel = true;
            _runServer = false;
        }
    }
}
