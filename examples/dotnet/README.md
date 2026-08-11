# .NET client and server example

`RdplibExamples.sln` contains a client and server which follow the same flow as
the C++ example.  Both projects compile the files in `common/` directly.  There
is no separate managed library.

An x64 Windows build of `rdplib.dll` is included in [`native/`](native/README.md),
so the solution can be built and run directly from a Windows checkout. Linux
and macOS users need to build the shared library with CMake and copy it into
that directory. From the repository root, a normal native build is:

```sh
cmake -S rdplib -B build -DRDPLIB_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

The managed projects copy the matching native library beside each program.
Developers rebuilding rdplib frequently can instead supply its location
explicitly:

```sh
dotnet build examples/dotnet/RdplibExamples.sln -p:RdplibNativePath=/full/path/to/librdplib.so
```

Open `RdplibExamples.sln` in Visual Studio to select and debug either program,
or build and run them from the repository root:

```sh
dotnet build examples/dotnet/RdplibExamples.sln
dotnet run --project examples/dotnet/server/RdplibServer.csproj
dotnet run --project examples/dotnet/client/RdplibClient.csproj
```

The server listens on UDP port `9000`, and the client connects to
`127.0.0.1:9000`.  Start the server first.  The client logs in, enters game
simulation, sends unreliable position updates and reliable profile saves for
30 seconds, then completes an orderly logout.
