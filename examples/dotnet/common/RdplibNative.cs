// Copyright (c) 2026 solar
// SPDX-License-Identifier: MIT

using System;
using System.Runtime.InteropServices;

namespace RdplibExample.Common
{
    internal static class RdplibNative
    {
        internal const int Ok = 0;
        internal const int ErrorInvalidArgument = -1;
        internal const int ErrorOutOfMemory = -2;
        internal const int ErrorBusy = -3;
        internal const int ErrorNotUsable = -4;

        internal const int ConnectionSendOk = 0;
        internal const int ConnectionSendNotConnected = 13;
        internal const int ConnectionSendBufferFull = 14;
        internal const int ConnectionSendHistoryFull = 15;
        internal const int ConnectionSendFinSent = 16;
        internal const int ConnectionSendPeerStopped = 17;

        internal const uint UseCrc = 0x40000000;
        internal const uint SendUnreliable = 0;
        internal const uint SendReliable = 1;
        internal const ushort FlagSequenced = 0x1000;
        internal const uint DisconnectReasonSendError = 0x00070000;

        private const string LibraryName = "rdplib";

        [StructLayout(LayoutKind.Sequential)]
        internal struct DisconnectInfo
        {
            internal uint Reason;
            internal byte IcmpType;
            internal byte IcmpCode;
            internal byte IcmpAddress0;
            internal byte IcmpAddress1;
            internal byte IcmpAddress2;
            internal byte IcmpAddress3;
        }

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_runtime_create(out IntPtr runtime, uint fastAllocatorBytes);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_runtime_destroy(IntPtr runtime);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_endpoint_create(
            IntPtr runtime,
            out IntPtr endpoint,
            ushort localPort,
            uint expectedConnections,
            uint flags
        );

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_endpoint_destroy(IntPtr endpoint);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern ushort rdplib_endpoint_local_port(IntPtr endpoint);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_endpoint_process(IntPtr endpoint, int timeoutMs);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr rdplib_endpoint_accept(IntPtr endpoint);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr rdplib_endpoint_pop_connectionless(IntPtr endpoint);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_connect(
            IntPtr endpoint,
            out IntPtr connection,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string host,
            ushort port
        );

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void rdplib_connection_release(IntPtr connection);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_connection_enable_keepalive(IntPtr connection);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr rdplib_connection_pop_message(IntPtr connection);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_connection_send(
            IntPtr connection,
            [In] byte[] data,
            uint bytes,
            uint stream,
            uint flags
        );

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_connection_begin_close(IntPtr connection, uint lingerTimeoutMs);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_connection_set_data_rate(IntPtr connection, uint bytesPerSecond);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_connection_set_send_buffer_size(IntPtr connection, uint bytes);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_connection_get_remote_ipv4(
            IntPtr connection,
            [Out, MarshalAs(UnmanagedType.LPArray, SizeConst = 4)] byte[] address,
            out ushort port
        );

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_connection_get_disconnect_info(IntPtr connection, out DisconnectInfo information);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern ushort rdplib_message_flags(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern byte rdplib_message_stream(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern uint rdplib_message_size(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr rdplib_message_data(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_message_is_disconnect(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int rdplib_message_has_fin(IntPtr message);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void rdplib_message_release(IntPtr message);
    }
}
