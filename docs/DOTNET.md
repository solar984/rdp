# Using rdplib from .NET

.NET can call the shared rdplib library through `DllImport`.  This is the standard P/Invoke interface used by the classic .NET Framework and current .NET.

The native library name depends on the host:

| Host | Native library |
| --- | --- |
| Windows | `rdplib.dll` |
| Linux | `librdplib.so` |
| macOS | `librdplib.dylib` |

[`RdplibExamples.sln`](../examples/dotnet/RdplibExamples.sln) contains a client and server using the same connection lifecycle as the C++ example.  The managed projects are separate from the rdplib build.

## Build the native library

From the repository root, configure the `rdplib` subdirectory directly when only the native library is needed:

```sh
cmake -S rdplib -B build -DRDPLIB_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

`CMAKE_BUILD_TYPE` selects the configuration for single configuration generators such as Makefiles.  `--config Debug` selects it for multi configuration generators such as Visual Studio.  Supplying both keeps the command usable with either kind.

The shared library is enabled by default, but the explicit option makes the intent clear.  Configuring `rdplib/` instead of the repository root leaves the separate tests project out of the build.

The Windows build also creates `rdplib_import.lib`, but .NET does not use it.  The managed process and native library must use the same architecture.  An x64 .NET application needs the x64 native library.

## Put the library beside the program

The simplest deployment is to copy the native library into the managed output directory:

```text
Windows
bin/Debug/net10.0/
    RdplibExample.exe
    RdplibExample.dll
    rdplib.dll

Linux
bin/Debug/net10.0/
    RdplibExample.dll
    librdplib.so
```

The C# declarations use the name `rdplib`.  .NET adds the host's normal prefix and extension, so the same declarations resolve all 3 file names.  Microsoft documents the complete search rules under [native library loading](https://learn.microsoft.com/dotnet/standard/native-interop/native-library-loading).

## Declare the functions

A normal declaration looks like this:

```csharp
[DllImport("rdplib", EntryPoint = "rdplib_runtime_create", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
internal static extern int RuntimeCreate(out IntPtr runtime, uint fastAllocatorBytes);
```

Every rdplib function uses the C calling convention.  Runtime, endpoint, connection, and message handles are native pointers, so C# stores them as `IntPtr`.  Statistics are copied into matching C# structures.  The example declares the part of the exported API used by its client and server.

The shared library exports only the normal `rdplib_*` API.  The raw recovered interface in `rdp.h` is for native code which compiles or statically links rdplib.  See [API.md](API.md) for the complete function reference and return values.

### Keepalive and packet drop callbacks

The default library can set a per connection keepalive interval with [`rdplib_connection_enable_keepalive_with_interval`](API.md#rdplib_connection_enable_keepalive_with_interval).  It can also install a packet drop callback with [`rdplib_connection_set_packet_drop_callback`](API.md#rdplib_connection_set_packet_drop_callback).  A source faithful build returns `RDPLIB_ERROR_NOT_SUPPORTED` from both functions and keeps the recovered 10,000 ms keepalive interval.

The packet drop callback is a managed delegate.  An immediate outbound send can invoke it before the P/Invoke call returns.  Scheduled output and inbound traffic invoke it from rdplib's native I/O thread.  The connection lock serializes these calls.

Keep the delegate in a managed field for as long as rdplib holds its function pointer.  Otherwise the garbage collector can release it.  A nonzero return discards the datagram.

The packet pointer is borrowed and remains valid only during the callback.  The callback must return quickly, must not let a managed exception cross into native code, and must not call rdplib for the same connection.  Remove the callback before releasing managed state referenced by its context pointer.  Passing a null delegate removes it and waits for a callback already in progress to finish.

[`rdplib_connect`](API.md#rdplib_connect) receives its host string as `UnmanagedType.LPUTF8Str`.  Numeric IPv4 addresses and ordinary ASCII DNS names have the same byte representation in UTF-8.

## Lifetime and message ownership

The C ownership rules also apply to .NET:

1. Create the runtime, then create an endpoint.
2. Use the endpoint and its connections from a single application thread.
3. Call [`rdplib_endpoint_process`](API.md#rdplib_endpoint_process) to move received work to the application queues.
4. Release every message returned by a pop function exactly once.
5. Release every connection before destroying its endpoint.
6. Destroy every endpoint before destroying the runtime.

[`rdplib_endpoint_destroy`](API.md#rdplib_endpoint_destroy) returns `RDPLIB_ERROR_BUSY` without changing the endpoint while the application still owns an accepted or connected handle.

Multiple .NET threads must not call the same endpoint or its connections at the same time.  Different endpoints can belong to different application threads.

[`rdplib_message_data`](API.md#rdplib_message_data) returns a pointer to native memory.  It remains valid only until [`rdplib_message_release`](API.md#rdplib_message_release).  The example copies the data into a managed `byte[]` with `Marshal.Copy`, then releases the native message.  This keeps the native lifetime short and makes the copy explicit.

The endpoint owns rdplib's I/O thread.  The .NET application does not start, stop, or join it.

## Close a connection

Drain and release queued messages, then copy any statistics needed by the application.  [`rdplib_connection_begin_close`](API.md#rdplib_connection_begin_close) returns immediately while the native I/O thread handles FIN, retries, and linger.

Release the application connection handle after beginning close, but continue calling `rdplib_endpoint_process` while the endpoint is running.  If the connection already disconnected, release the handle without beginning close.

A short program can allow some time for close to finish before destroying the endpoint.  A long running server needs no separate close loop because its normal endpoint processing continues doing the work.
