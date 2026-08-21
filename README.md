# rdplib

[solar@heliacal.net](mailto:solar@heliacal.net)

`rdplib` is a standalone C11 reconstruction of the reliable UDP transport used by EverQuest/EQMac and some other old video games.

RDP is the historical name of this game transport.  It is not related to Microsoft's Remote Desktop Protocol.

## Features

- Reliable and unreliable UDP messages with multiple logical streams.
- Fragmentation, reassembly, cumulative and selective acknowledgements, retransmission, rate control, and flow control.
- Keepalive with a per connection interval, timeout detection, connection statistics, and nonblocking close with an application selected linger period.
- ICMP port unreachable handling with exact peer attribution on supported Windows and Linux hosts.
- Optional endpoint receive and send socket buffer tuning with effective value queries.
- An optional per connection packet drop callback for application controlled loss testing.
- IPv4 support on Windows and POSIX systems.
- Bounds and transport state checks are enabled by default, with a source faithful build available for compatibility research.
- Optional compatibility with the recovered encrypted framing mode. This mode is historical and does not provide modern authentication or security.

## Examples

- [`examples/c/README.md`](examples/c/README.md) - minimal rdplib and raw API usage example.
- [`examples/cpp/README.md`](examples/cpp/README.md) - an EQ style game server and client.
- [`examples/dotnet/README.md`](examples/dotnet/README.md) - an EQ style game server and client using the native library from managed code.

## Documentation

- [`docs/API.md`](docs/API.md) — how to use the normal API, including threads, messages, and close.
- [`docs/DOTNET.md`](docs/DOTNET.md) — how to load the shared library from .NET and use the C API through `DllImport`.
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — packet framing, streams, sequencing, ACKs, windows, queues, timing, fragmentation, and connection lifetime.
- [`docs/KNOWN_BUGS.md`](docs/KNOWN_BUGS.md) — historical bugs and unsafe assumptions that are still relevant.

Valid traffic has been tested for exact wire compatibility with the reference implementations.  New applications should normally start with the `rdplib_*` interface.  The raw interface is still
available when direct access or exact source behavior is useful.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure -C Debug
```

The static library and shared library are both built by default.  Set `RDPLIB_BUILD_SHARED=OFF` when an embedding project only wants the static library.
The shared target produces `rdplib.dll` and `rdplib_import.lib` on Windows, or `librdplib.so` on Linux.  Shared builds export only the normal `rdplib_*` application API.
