// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;

namespace RdplibExample.Common
{
    // Owns the process rdplib runtime.
    internal sealed class RdpRuntime : IDisposable
    {
        private IntPtr _runtime;

        internal bool IsOpen
        {
            get { return _runtime != IntPtr.Zero; }
        }

        internal IntPtr NativeHandle
        {
            get { return _runtime; }
        }

        // This is an initial allocator size, not a memory limit.
        internal int Open(uint fastAllocatorBytes = 4u * 1024u * 1024u)
        {
            if (_runtime != IntPtr.Zero)
                return RdplibNative.ErrorBusy;

            return RdplibNative.rdplib_runtime_create(out _runtime, fastAllocatorBytes);
        }

        // Every endpoint must be closed first.
        internal int Close()
        {
            if (_runtime == IntPtr.Zero)
                return RdplibNative.Ok;

            int result = RdplibNative.rdplib_runtime_destroy(_runtime);
            if (result == RdplibNative.Ok)
                _runtime = IntPtr.Zero;

            return result;
        }

        public void Dispose()
        {
            int result = Close();
            Debug.Assert(result == RdplibNative.Ok);
        }
    }
}
