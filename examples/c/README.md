# C examples

This directory contains 2 small echo client and server pairs:

| Programs | Interface |
| --- | --- |
| `rdplib_hello_server`, `rdplib_hello_client` | The normal `rdplib_*` application API from `rdplib.h`. |
| `rdp_hello_server`, `rdp_hello_client` | The recovered raw API from `rdp.h`. |

New applications should normally use the `rdplib_*` pair.  It demonstrates
runtime, endpoint, connection, and message ownership without exposing the
transport's internal structures.  The raw pair shows the extra allocator,
statistics, connection, and message handling required by the recovered API.

## Build

The examples are a standalone CMake project which adds the repository's
`rdplib` directory directly:

```sh
cmake -S examples/c -B build-c-examples -DCMAKE_BUILD_TYPE=Debug
cmake --build build-c-examples --config Debug
```

`CMAKE_BUILD_TYPE` selects the configuration for single configuration
generators.  `--config Debug` selects it for multi configuration generators
such as Visual Studio.

## Run

Start either server, then run its matching client.  Both use UDP port `9000`
by default:

```console
rdplib_hello_server
rdplib_hello_client
```

The server echoes one reliable message.  The client prints the reply and both
sides perform an orderly close.  The `rdplib_*` pair also prints connection
statistics before releasing the connection.

The server accepts an optional port.  The client accepts an optional host and
port:

```console
rdplib_hello_server 9001
rdplib_hello_client 127.0.0.1 9001
```

Use `rdp_hello_server` and `rdp_hello_client` the same way to run the raw API
pair.  Multi configuration builds normally place the executables under a
configuration directory such as `build-c-examples/Debug`.
