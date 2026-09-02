// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

using System;
using RdplibExample.Common;

namespace RdplibExample.Server
{
    internal sealed class Client : IDisposable
    {
        private enum SessionState
        {
            AwaitingLogin,
            AwaitingClientReady,
            AwaitingFirstUpdate,
            InGame,
            Leaving
        }

        private enum HandleResult
        {
            Continue,
            Close,
            Invalid
        }

        private const uint DefaultLingerTimeout = 1000;

        private RdpConnection _connection;
        private readonly string _description;
        private SessionState _state;
        private ClientPositionUpdate _lastPosition;

        internal Client(RdpConnection connection)
        {
            _connection = connection;
            _description = ReadDescription(connection);
            _state = SessionState.AwaitingLogin;
        }

        internal string Description
        {
            get { return _description; }
        }

        internal bool IsInGame
        {
            get { return _state == SessionState.InGame; }
        }

        // Returning false tells ClientManager to remove this client.
        internal bool ProcessNetwork()
        {
            while (_connection != null)
            {
                RdpMessage message = new RdpMessage();
                uint disconnectReason;
                RdpConnection.ReceiveResult receiveResult = _connection.Receive(message, out disconnectReason);

                if (receiveResult == RdpConnection.ReceiveResult.NoData)
                    return true;

                if (receiveResult == RdpConnection.ReceiveResult.PeerClosed)
                {
                    Console.WriteLine("client {0} closed its connection", _description);
                    Close(DefaultLingerTimeout);
                    return false;
                }

                if (receiveResult == RdpConnection.ReceiveResult.ConnectionLost)
                {
                    Console.WriteLine("client {0} lost its connection, reason 0x{1:x8}", _description, disconnectReason);
                    // The transport has already ended. There is nothing left
                    // to finish with the peer, so release it without linger.
                    Close(0);
                    return false;
                }

                ApplicationPacket packet;
                if (!ApplicationPacket.TryDecode(message.Data, out packet))
                {
                    Console.Error.WriteLine("received an invalid application packet from {0}", _description);
                    Close(DefaultLingerTimeout);
                    return false;
                }

                HandleResult handleResult = HandleNetworkMessage(packet);
                if (handleResult == HandleResult.Invalid)
                {
                    Console.Error.WriteLine("received an invalid application packet from {0}", _description);
                    Close(DefaultLingerTimeout);
                    return false;
                }
                if (handleResult == HandleResult.Close)
                {
                    Close(DefaultLingerTimeout);
                    return false;
                }
            }

            return false;
        }

        private HandleResult HandleNetworkMessage(ApplicationPacket packet)
        {
            // LoginRequest
            if (packet.Opcode == ApplicationOpcode.LoginRequest && _state == SessionState.AwaitingLogin)
            {
                LoginRequest request;
                if (!ApplicationProtocol.TryDecode(packet.Payload, out request))
                    return HandleResult.Invalid;

                LoginReply reply = new LoginReply();
                reply.Accepted = request.ProtocolVersion == ApplicationProtocol.Version ? 1u : 0u;
                ApplicationPacket response = new ApplicationPacket(
                    ApplicationOpcode.LoginReply,
                    ApplicationProtocol.Encode(reply)
                );
                if (!SendPacket(response, RdplibNative.SendReliable))
                    return HandleResult.Close;

                if (reply.Accepted == 0)
                {
                    Console.WriteLine("rejected login from {0}", _description);
                    return HandleResult.Close;
                }

                _state = SessionState.AwaitingClientReady;
                Console.WriteLine("accepted login from {0}; waiting for client ready", _description);
                return HandleResult.Continue;
            }

            // ClientReady
            if (packet.Opcode == ApplicationOpcode.ClientReady && _state == SessionState.AwaitingClientReady)
            {
                if (packet.Payload.Length != 0)
                    return HandleResult.Invalid;

                ApplicationPacket response = new ApplicationPacket(ApplicationOpcode.ServerReady);
                if (!SendPacket(response, RdplibNative.SendReliable))
                    return HandleResult.Close;

                _state = SessionState.AwaitingFirstUpdate;
                Console.WriteLine("client {0} is ready; waiting for its first position update", _description);
                return HandleResult.Continue;
            }

            // ClientPositionUpdate is an example unreliable gameplay message.
            if (packet.Opcode == ApplicationOpcode.ClientPositionUpdate &&
                (_state == SessionState.AwaitingFirstUpdate || _state == SessionState.InGame))
            {
                ClientPositionUpdate update;
                if (!ApplicationProtocol.TryDecode(packet.Payload, out update))
                    return HandleResult.Invalid;

                _lastPosition = update;
                if (_state == SessionState.AwaitingFirstUpdate)
                {
                    _state = SessionState.InGame;
                    Console.WriteLine("client {0} entered game simulation", _description);
                }

                Console.WriteLine(
                    "position {0} from {1} is ({2}, {3}, {4})",
                    update.Sequence,
                    _description,
                    update.X,
                    update.Y,
                    update.Z
                );

                // A real server would attach this client's entity ID and fan
                // the update out to the other clients in game simulation.
                return HandleResult.Continue;
            }

            // SaveProfile is an example reliable gameplay message.
            if (packet.Opcode == ApplicationOpcode.SaveProfile)
            {
                SaveProfile profile;
                if (!ApplicationProtocol.TryDecode(packet.Payload, out profile))
                    return HandleResult.Invalid;

                if (_state == SessionState.AwaitingFirstUpdate)
                {
                    Console.WriteLine("ignored an early profile save from {0}", _description);
                    return HandleResult.Continue;
                }
                if (_state != SessionState.InGame)
                    return HandleResult.Invalid;

                Console.WriteLine(
                    "saved profile {0} for {1} after {2} seconds",
                    profile.Sequence,
                    _description,
                    profile.SecondsInGame
                );
                return HandleResult.Continue;
            }

            // LogoutRequest
            if (packet.Opcode == ApplicationOpcode.LogoutRequest &&
                (_state == SessionState.AwaitingFirstUpdate || _state == SessionState.InGame))
            {
                LogoutRequest request;
                if (!ApplicationProtocol.TryDecode(packet.Payload, out request))
                    return HandleResult.Invalid;

                _state = SessionState.Leaving;
                Console.WriteLine("client {0} requested logout with reason {1}", _description, request.Reason);

                LogoutReply reply = new LogoutReply();
                reply.Reason = request.Reason;
                ApplicationPacket response = new ApplicationPacket(
                    ApplicationOpcode.LogoutReply,
                    ApplicationProtocol.Encode(reply)
                );
                SendPacket(response, RdplibNative.SendReliable);
                return HandleResult.Close;
            }

            return HandleResult.Invalid;
        }

        private bool SendPacket(ApplicationPacket packet, uint flags)
        {
            int result = _connection.Send(packet.Data, 0, flags);
            if (result == RdplibNative.ConnectionSendOk)
                return true;

            Console.Error.WriteLine(
                "unable to send an application packet to {0}, result {1}",
                _description,
                result
            );
            Close(0);
            return false;
        }

        private void Close(uint lingerTimeoutMs)
        {
            if (_connection == null)
                return;

            // Close releases the application handle immediately. The endpoint
            // keeps the transport alive during linger, so removing this Client
            // does not discard a queued reply or FIN.
            _connection.Close(lingerTimeoutMs);
            _connection.Dispose();
            _connection = null;
        }

        private static string ReadDescription(RdpConnection connection)
        {
            if (connection == null)
                return "unknown";

            byte[] address = new byte[4];
            ushort port;
            if (connection.GetRemoteAddress(address, out port) != RdplibNative.Ok)
                return "unknown";

            return string.Format(
                "{0}.{1}.{2}.{3}:{4}",
                address[0],
                address[1],
                address[2],
                address[3],
                port
            );
        }

        public void Dispose()
        {
            Close(0);
        }
    }
}
