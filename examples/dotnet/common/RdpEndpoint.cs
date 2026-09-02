// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;

namespace RdplibExample.Common
{
    // Owns one rdplib endpoint. The application owns its connections.
    internal sealed class RdpEndpoint : IDisposable
    {
        private IntPtr _endpoint;

        internal ushort LocalPort
        {
            get
            {
                return _endpoint != IntPtr.Zero
                    ? RdplibNative.rdplib_endpoint_local_port(_endpoint)
                    : (ushort)0;
            }
        }

        // expectedConnections selects the internal hash layout. It is not a
        // connection limit.
        internal int Open(
            RdpRuntime runtime,
            ushort localPort,
            uint expectedConnections = 100,
            uint flags = RdplibNative.UseCrc
        )
        {
            if (_endpoint != IntPtr.Zero)
                return RdplibNative.ErrorBusy;
            if (runtime == null || !runtime.IsOpen)
                return RdplibNative.ErrorInvalidArgument;

            return RdplibNative.rdplib_endpoint_create(
                runtime.NativeHandle,
                out _endpoint,
                localPort,
                expectedConnections,
                flags
            );
        }

        internal int Close()
        {
            if (_endpoint == IntPtr.Zero)
                return RdplibNative.Ok;

            int result = RdplibNative.rdplib_endpoint_destroy(_endpoint);
            if (result == RdplibNative.Ok)
                _endpoint = IntPtr.Zero;

            return result;
        }

        internal int Process(int timeoutMs = 0)
        {
            if (_endpoint == IntPtr.Zero)
                return RdplibNative.ErrorNotUsable;

            int result = RdplibNative.rdplib_endpoint_process(_endpoint, timeoutMs);
            DiscardConnectionless();
            return result;
        }

        // A null result with RdplibNative.Ok means no connection is waiting.
        internal RdpConnection Accept(out int result)
        {
            result = RdplibNative.Ok;
            if (_endpoint == IntPtr.Zero)
            {
                result = RdplibNative.ErrorNotUsable;
                return null;
            }

            IntPtr connection = RdplibNative.rdplib_endpoint_accept(_endpoint);
            return connection != IntPtr.Zero ? WrapConnection(connection, out result) : null;
        }

        internal RdpConnection Connect(string host, ushort port, out int result)
        {
            if (_endpoint == IntPtr.Zero || host == null)
            {
                result = RdplibNative.ErrorInvalidArgument;
                return null;
            }

            IntPtr connection;
            result = RdplibNative.rdplib_connect(_endpoint, out connection, host, port);
            if (result != RdplibNative.Ok)
                return null;

            return WrapConnection(connection, out result);
        }

        private static RdpConnection WrapConnection(IntPtr connection, out int result)
        {
            result = RdplibNative.Ok;
            try
            {
                return new RdpConnection(connection);
            }
            catch (OutOfMemoryException)
            {
                ReleaseConnection(connection);
                result = RdplibNative.ErrorOutOfMemory;
                return null;
            }
        }

        private static void ReleaseConnection(IntPtr connection)
        {
            if (connection == IntPtr.Zero)
                return;

            IntPtr message;
            while ((message = RdplibNative.rdplib_connection_pop_message(connection)) != IntPtr.Zero)
                RdplibNative.rdplib_message_release(message);

            RdplibNative.rdplib_connection_begin_close(connection, 0);
            RdplibNative.rdplib_connection_release(connection);
        }

        private void DiscardConnectionless()
        {
            if (_endpoint == IntPtr.Zero)
                return;

            IntPtr message;
            while ((message = RdplibNative.rdplib_endpoint_pop_connectionless(_endpoint)) != IntPtr.Zero)
                RdplibNative.rdplib_message_release(message);
        }

        public void Dispose()
        {
            int result = Close();
            Debug.Assert(result == RdplibNative.Ok);
        }
    }
}
