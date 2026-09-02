// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using RdplibExample.Common;

namespace RdplibExample.Client
{
    internal sealed class ServerManager : IDisposable
    {
        internal enum SessionState
        {
            Closed,
            LoggingIn,
            EnteringGame,
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
        private const uint ServerDataRate = 400u * 1024u;
        private const uint ServerSendBufferSize = 1024u * 1024u;
        private static readonly TimeSpan ResponseTimeout = TimeSpan.FromSeconds(10);

        // Dispose releases these in connection, endpoint, runtime order.
        private readonly RdpRuntime _runtime;
        private readonly RdpEndpoint _endpoint;
        private readonly Stopwatch _responseTimer;
        private readonly Stopwatch _closeTimer;
        private RdpConnection _connection;
        private SessionState _state;
        private uint _logoutReason;
        private bool _logoutReplyReceived;
        private bool _succeeded;
        private bool _closing;
        private uint _closeTimeoutMs;

        internal ServerManager()
        {
            _runtime = new RdpRuntime();
            _endpoint = new RdpEndpoint();
            _responseTimer = new Stopwatch();
            _closeTimer = new Stopwatch();
            _state = SessionState.Closed;
        }

        internal bool IsInGame
        {
            get { return _state == SessionState.InGame; }
        }

        internal bool Succeeded
        {
            get { return _succeeded; }
        }

        internal int Connect(string host, ushort port)
        {
            if (host == null)
                return RdplibNative.ErrorInvalidArgument;

            int result = _runtime.Open();
            if (result != RdplibNative.Ok)
                return result;

            result = _endpoint.Open(_runtime, 0, 1);
            if (result != RdplibNative.Ok)
                return result;

            _connection = _endpoint.Connect(host, port, out result);
            if (_connection == null)
                return result;

            result = _connection.EnableKeepalive();
            if (result == RdplibNative.Ok)
                result = _connection.SetDataRate(ServerDataRate);
            if (result == RdplibNative.Ok)
                result = _connection.SetSendBufferSize(ServerSendBufferSize);
            if (result != RdplibNative.Ok)
            {
                StartClose(0);
                return result;
            }

            LoginRequest request = new LoginRequest();
            request.ProtocolVersion = ApplicationProtocol.Version;
            ApplicationPacket packet = new ApplicationPacket(
                ApplicationOpcode.LoginRequest,
                ApplicationProtocol.Encode(request)
            );

            _state = SessionState.LoggingIn;
            result = SendPacket(packet, RdplibNative.SendReliable);
            if (result != RdplibNative.ConnectionSendOk)
            {
                StartClose(0);
                return result;
            }

            _responseTimer.Restart();
            Console.WriteLine("connected to {0}:{1} and sent login request", host, port);
            return RdplibNative.Ok;
        }

        internal bool SendClientPositionUpdate(ClientPositionUpdate update)
        {
            if (_state != SessionState.InGame)
                return false;

            ApplicationPacket packet = new ApplicationPacket(
                ApplicationOpcode.ClientPositionUpdate,
                ApplicationProtocol.Encode(update)
            );
            int result = SendPacket(packet, RdplibNative.SendUnreliable);
            if (result == RdplibNative.ConnectionSendOk || result == RdplibNative.ConnectionSendBufferFull)
                return true;

            StartClose(0);
            return false;
        }

        internal bool SendSaveProfile(SaveProfile profile)
        {
            if (_state != SessionState.InGame)
                return false;

            ApplicationPacket packet = new ApplicationPacket(
                ApplicationOpcode.SaveProfile,
                ApplicationProtocol.Encode(profile)
            );
            int result = SendPacket(packet, RdplibNative.SendReliable);
            if (result == RdplibNative.ConnectionSendOk)
                return true;

            StartClose(0);
            return false;
        }

        internal bool SendLogout(LogoutReason reason)
        {
            if (_state != SessionState.InGame)
                return false;

            LogoutRequest request = new LogoutRequest();
            request.Reason = (uint)reason;
            ApplicationPacket packet = new ApplicationPacket(
                ApplicationOpcode.LogoutRequest,
                ApplicationProtocol.Encode(request)
            );

            int result = SendPacket(packet, RdplibNative.SendReliable);
            if (result != RdplibNative.ConnectionSendOk)
            {
                StartClose(0);
                return false;
            }

            _logoutReason = request.Reason;
            _state = SessionState.Leaving;
            _responseTimer.Restart();
            return true;
        }

        // Returning false tells main that this session is finished.
        internal bool ProcessNetwork()
        {
            int processResult = _endpoint.Process();
            if (processResult < 0)
            {
                Console.Error.WriteLine("unable to process the RDP endpoint, result {0}", processResult);
                return false;
            }

            if (_closing)
                return _closeTimer.ElapsedMilliseconds < _closeTimeoutMs;

            while (_connection != null)
            {
                RdpMessage message = new RdpMessage();
                uint disconnectReason;
                RdpConnection.ReceiveResult receiveResult = _connection.Receive(message, out disconnectReason);

                if (receiveResult == RdpConnection.ReceiveResult.NoData)
                    break;

                if (receiveResult == RdpConnection.ReceiveResult.PeerClosed)
                {
                    if (_state == SessionState.Leaving && _logoutReplyReceived)
                    {
                        Console.WriteLine("server completed the orderly close");
                        _succeeded = true;
                    }
                    else
                    {
                        Console.Error.WriteLine("server closed before the session completed");
                    }

                    StartClose(DefaultLingerTimeout);
                    return true;
                }

                if (receiveResult == RdpConnection.ReceiveResult.ConnectionLost)
                {
                    Console.Error.WriteLine("server connection was lost, reason 0x{0:x8}", disconnectReason);
                    // The transport has already ended. There is nothing left
                    // to finish with the peer, so release it without linger.
                    StartClose(0);
                    return false;
                }

                ApplicationPacket packet;
                if (!ApplicationPacket.TryDecode(message.Data, out packet))
                {
                    Console.Error.WriteLine("server returned an invalid application packet");
                    StartClose(DefaultLingerTimeout);
                    return true;
                }

                HandleResult handleResult = HandleNetworkMessage(packet);
                if (handleResult == HandleResult.Invalid)
                {
                    Console.Error.WriteLine("server returned an invalid application packet");
                    StartClose(DefaultLingerTimeout);
                    return true;
                }
                if (handleResult == HandleResult.Close)
                {
                    StartClose(DefaultLingerTimeout);
                    return true;
                }
            }

            bool waitingForResponse = _state == SessionState.LoggingIn ||
                _state == SessionState.EnteringGame ||
                _state == SessionState.Leaving;
            if (waitingForResponse && _responseTimer.Elapsed >= ResponseTimeout)
            {
                Console.Error.WriteLine("timed out waiting for the server");
                StartClose(DefaultLingerTimeout);
            }

            return true;
        }

        private HandleResult HandleNetworkMessage(ApplicationPacket packet)
        {
            if (packet.Opcode == ApplicationOpcode.LoginReply && _state == SessionState.LoggingIn)
            {
                LoginReply reply;
                if (!ApplicationProtocol.TryDecode(packet.Payload, out reply))
                    return HandleResult.Invalid;
                if (reply.Accepted == 0)
                {
                    Console.Error.WriteLine("server rejected the login request");
                    return HandleResult.Close;
                }

                ApplicationPacket ready = new ApplicationPacket(ApplicationOpcode.ClientReady);
                int result = SendPacket(ready, RdplibNative.SendReliable);
                if (result != RdplibNative.ConnectionSendOk)
                {
                    StartClose(0);
                    return HandleResult.Close;
                }

                _state = SessionState.EnteringGame;
                _responseTimer.Restart();
                Console.WriteLine("login accepted; sent client ready");
                return HandleResult.Continue;
            }

            if (packet.Opcode == ApplicationOpcode.ServerReady && _state == SessionState.EnteringGame)
            {
                if (packet.Payload.Length != 0)
                    return HandleResult.Invalid;

                _state = SessionState.InGame;
                Console.WriteLine("server ready; entering game simulation");
                return HandleResult.Continue;
            }

            if (packet.Opcode == ApplicationOpcode.LogoutReply && _state == SessionState.Leaving)
            {
                LogoutReply reply;
                if (!ApplicationProtocol.TryDecode(packet.Payload, out reply) || reply.Reason != _logoutReason)
                    return HandleResult.Invalid;

                _logoutReplyReceived = true;
                Console.WriteLine("server accepted logout reason {0}", reply.Reason);
                return HandleResult.Continue;
            }

            return HandleResult.Invalid;
        }

        private int SendPacket(ApplicationPacket packet, uint flags)
        {
            return _connection != null
                ? _connection.Send(packet.Data, 0, flags)
                : RdplibNative.ErrorNotUsable;
        }

        private void StartClose(uint lingerTimeoutMs)
        {
            if (_closing)
                return;

            if (_connection != null)
            {
                // Close releases the application handle immediately. The
                // endpoint keeps the transport alive during linger, so the
                // queued FIN still has time to reach the peer.
                _connection.Close(lingerTimeoutMs);
                _connection.Dispose();
                _connection = null;
            }

            _state = SessionState.Closed;
            _closing = true;
            _closeTimeoutMs = lingerTimeoutMs;
            _closeTimer.Restart();
        }

        public void Dispose()
        {
            if (_connection != null)
            {
                _connection.Dispose();
                _connection = null;
            }

            _endpoint.Dispose();
            _runtime.Dispose();
        }
    }
}
