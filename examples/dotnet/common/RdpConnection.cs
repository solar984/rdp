// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

using System;
using System.Runtime.InteropServices;

namespace RdplibExample.Common
{
    internal sealed class RdpMessage
    {
        internal byte[] Data { get; private set; }
        internal byte StreamNumber { get; private set; }
        internal ushort Flags { get; private set; }

        internal RdpMessage()
        {
            Data = Array.Empty<byte>();
        }

        internal void Reset(byte[] data = null, byte streamNumber = 0, ushort flags = 0)
        {
            Data = data ?? Array.Empty<byte>();
            StreamNumber = streamNumber;
            Flags = flags;
        }
    }

    // Owns one application connection handle. Its endpoint and runtime must
    // outlive it.
    internal sealed class RdpConnection : IDisposable
    {
        internal enum ReceiveResult
        {
            NoData,
            MessageReceived,
            PeerClosed,
            ConnectionLost
        }

        private IntPtr _connection;
        private ReceiveResult _terminalResult;
        private uint _disconnectReason;

        internal RdpConnection(IntPtr connection)
        {
            _connection = connection;
            _terminalResult = ReceiveResult.NoData;
        }

        internal int EnableKeepalive()
        {
            return _connection != IntPtr.Zero
                ? RdplibNative.rdplib_connection_enable_keepalive(_connection)
                : RdplibNative.ErrorNotUsable;
        }

        internal int SetDataRate(uint bytesPerSecond)
        {
            return _connection != IntPtr.Zero
                ? RdplibNative.rdplib_connection_set_data_rate(_connection, bytesPerSecond)
                : RdplibNative.ErrorNotUsable;
        }

        internal int SetSendBufferSize(uint bytes)
        {
            return _connection != IntPtr.Zero
                ? RdplibNative.rdplib_connection_set_send_buffer_size(_connection, bytes)
                : RdplibNative.ErrorNotUsable;
        }

        // rdplib copies the borrowed data before returning.
        internal int Send(byte[] data, uint stream, uint flags)
        {
            if (_connection == IntPtr.Zero || _terminalResult != ReceiveResult.NoData || data == null)
                return RdplibNative.ErrorNotUsable;

            int result = RdplibNative.rdplib_connection_send(
                _connection,
                data,
                checked((uint)data.Length),
                stream,
                flags
            );

            bool reliable = (flags & RdplibNative.SendReliable) != 0;
            bool connectionLost = result == RdplibNative.ConnectionSendHistoryFull ||
                (result == RdplibNative.ConnectionSendBufferFull && reliable) ||
                result == RdplibNative.ConnectionSendNotConnected ||
                result == RdplibNative.ConnectionSendFinSent ||
                result == RdplibNative.ConnectionSendPeerStopped;

            if (connectionLost)
            {
                _terminalResult = ReceiveResult.ConnectionLost;
                _disconnectReason = RdplibNative.DisconnectReasonSendError;
            }

            return result;
        }

        // Native message data is copied before the native message is released.
        internal ReceiveResult Receive(RdpMessage message, out uint disconnectReason)
        {
            disconnectReason = 0;
            if (message == null)
                return ReceiveResult.NoData;

            message.Reset();
            if (_terminalResult != ReceiveResult.NoData)
            {
                if (_terminalResult == ReceiveResult.ConnectionLost)
                    disconnectReason = _disconnectReason;

                return _terminalResult;
            }

            if (_connection == IntPtr.Zero)
                return ReceiveResult.NoData;

            IntPtr nativeMessage = RdplibNative.rdplib_connection_pop_message(_connection);
            if (nativeMessage == IntPtr.Zero)
                return ReceiveResult.NoData;

            try
            {
                if (RdplibNative.rdplib_message_is_disconnect(nativeMessage) != 0)
                {
                    RdplibNative.DisconnectInfo information;
                    if (RdplibNative.rdplib_connection_get_disconnect_info(_connection, out information) == RdplibNative.Ok)
                        _disconnectReason = information.Reason;

                    _terminalResult = ReceiveResult.ConnectionLost;
                    disconnectReason = _disconnectReason;
                    return _terminalResult;
                }

                bool hasFin = RdplibNative.rdplib_message_has_fin(nativeMessage) != 0;
                uint byteCount = RdplibNative.rdplib_message_size(nativeMessage);
                if (hasFin)
                    _terminalResult = ReceiveResult.PeerClosed;

                if (hasFin && byteCount == 0)
                    return ReceiveResult.PeerClosed;

                int length = checked((int)byteCount);
                byte[] data = new byte[length];
                if (length != 0)
                    Marshal.Copy(RdplibNative.rdplib_message_data(nativeMessage), data, 0, length);

                ushort flags = RdplibNative.rdplib_message_flags(nativeMessage);
                byte streamNumber = (flags & RdplibNative.FlagSequenced) != 0
                    ? RdplibNative.rdplib_message_stream(nativeMessage)
                    : (byte)0;

                message.Reset(data, streamNumber, flags);
                return ReceiveResult.MessageReceived;
            }
            finally
            {
                RdplibNative.rdplib_message_release(nativeMessage);
            }
        }

        // Releases the application handle. The endpoint retains a lingering
        // transport connection until it finishes or its deadline expires.
        internal void Close(uint lingerTimeoutMs)
        {
            if (_connection == IntPtr.Zero)
                return;

            DiscardMessages();

            IntPtr connection = _connection;
            _connection = IntPtr.Zero;
            RdplibNative.rdplib_connection_begin_close(connection, lingerTimeoutMs);
            RdplibNative.rdplib_connection_release(connection);
        }

        internal int GetRemoteAddress(byte[] address, out ushort port)
        {
            port = 0;
            if (_connection == IntPtr.Zero || address == null || address.Length != 4)
                return RdplibNative.ErrorNotUsable;

            return RdplibNative.rdplib_connection_get_remote_ipv4(_connection, address, out port);
        }

        private void DiscardMessages()
        {
            if (_connection == IntPtr.Zero)
                return;

            IntPtr message;
            while ((message = RdplibNative.rdplib_connection_pop_message(_connection)) != IntPtr.Zero)
                RdplibNative.rdplib_message_release(message);
        }

        public void Dispose()
        {
            Close(0);
        }
    }
}
