# Native rdplib library

This directory includes a checked x64 Windows Release build of `rdplib.dll`
so the .NET examples work from a normal Windows checkout. Linux and macOS
users need to build the shared library and copy it here using the name for the
current host:

| Host | File |
| --- | --- |
| Windows | `rdplib.dll` |
| Linux | `librdplib.so` |
| macOS | `librdplib.dylib` |

`RdplibClient` and `RdplibServer` copy the matching file from here into their
own output directories. Locally built Linux and macOS libraries are ignored
by Git.

## Windows

The included DLL can be replaced with a new checked Release build by running
these commands from the repository root in a Visual Studio developer command
prompt:

```console
cmake -S rdplib -B build -DRDPLIB_BUILD_SHARED=ON -DRDPLIB_SOURCE_FAITHFUL=OFF
cmake --build build --config Release
cmake -E copy_if_different build/Release/rdplib.dll examples/dotnet/native/rdplib.dll
```

## Linux

```console
cmake -S rdplib -B build -DRDPLIB_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cmake -E copy_if_different build/librdplib.so examples/dotnet/native/librdplib.so
```

## macOS

```console
cmake -S rdplib -B build -DRDPLIB_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cmake -E copy_if_different build/librdplib.dylib examples/dotnet/native/librdplib.dylib
```

If a generator places the library in another configuration subdirectory, copy
the file from that location instead.  Developers rebuilding rdplib frequently
can avoid the copy by supplying its full path directly:

```console
dotnet build examples/dotnet/RdplibExamples.sln -p:RdplibNativePath=/full/path/to/librdplib.so
```
