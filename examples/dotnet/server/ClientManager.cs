// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using RdplibExample.Common;

namespace RdplibExample.Server
{
    internal sealed class ClientManager : IDisposable
    {
        private const uint MaximumClientAcceptsPerTick = 5;
        private const uint ClientDataRate = 400u * 1024u;
        private const uint ClientSendBufferSize = 1024u * 1024u;

        private readonly RdpRuntime _runtime;
        private readonly RdpEndpoint _endpoint;
        private readonly List<Client> _clients;

        internal ClientManager()
        {
            _runtime = new RdpRuntime();
            _endpoint = new RdpEndpoint();
            _clients = new List<Client>();
        }

        internal int Open(ushort port)
        {
            int result = _runtime.Open();
            if (result != RdplibNative.Ok)
                return result;

            result = _endpoint.Open(_runtime, port);
            if (result == RdplibNative.Ok)
                Console.WriteLine("RDP server listening on UDP port {0}", _endpoint.LocalPort);

            return result;
        }

        // Returning false tells main that the server can no longer continue.
        internal bool ProcessNetwork()
        {
            int processResult = _endpoint.Process();
            if (processResult < 0)
            {
                Console.Error.WriteLine("unable to process the RDP endpoint, result {0}", processResult);
                return false;
            }

            if (!AcceptClients())
                return false;

            for (int index = 0; index < _clients.Count;)
            {
                Client client = _clients[index];
                if (!client.ProcessNetwork())
                {
                    Console.WriteLine("removing client {0}", client.Description);
                    client.Dispose();
                    _clients.RemoveAt(index);
                }
                else
                {
                    ++index;
                }
            }

            return true;
        }

        private bool AcceptClients()
        {
            int acceptResult = RdplibNative.Ok;

            for (uint accepted = 0; accepted < MaximumClientAcceptsPerTick; ++accepted)
            {
                RdpConnection connection = _endpoint.Accept(out acceptResult);
                if (connection == null)
                    break;

                int setupResult = connection.EnableKeepalive();
                if (setupResult == RdplibNative.Ok)
                    setupResult = connection.SetDataRate(ClientDataRate);
                if (setupResult == RdplibNative.Ok)
                    setupResult = connection.SetSendBufferSize(ClientSendBufferSize);

                if (setupResult != RdplibNative.Ok)
                {
                    Console.Error.WriteLine("unable to configure an accepted connection, result {0}", setupResult);
                    connection.Dispose();
                    continue;
                }

                try
                {
                    Client client = new Client(connection);
                    Console.WriteLine("accepted client {0}", client.Description);
                    _clients.Add(client);
                }
                catch (OutOfMemoryException)
                {
                    connection.Dispose();
                    acceptResult = RdplibNative.ErrorOutOfMemory;
                    break;
                }
            }

            if (acceptResult != RdplibNative.Ok)
            {
                Console.Error.WriteLine("unable to accept an RDP client, result {0}", acceptResult);
                return false;
            }

            return true;
        }

        public void Dispose()
        {
            foreach (Client client in _clients)
                client.Dispose();
            _clients.Clear();

            _endpoint.Dispose();
            _runtime.Dispose();
        }
    }
}
