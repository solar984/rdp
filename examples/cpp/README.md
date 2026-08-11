# C++ client and server example

This example is a small game style client and server built on the normal
`rdplib_*` API.  The common classes give runtime, endpoint, connection, and
message ownership a narrow C++ interface while leaving the application packet
format and session state in the client and server.

The CMake project builds 2 programs:

| Program | Purpose |
| --- | --- |
| `rdplib_cpp_server` | Accept connections and process login, gameplay, save, and logout messages. |
| `rdplib_cpp_client` | Connect to the server and run the example game session. |

## Build

From the repository root:

```sh
cmake -S examples/cpp -B build-cpp-example -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cpp-example --config Debug
```

The example adds the repository's `rdplib` directory directly and links the
static library.

## Run

Start the server first, then start the client:

```console
rdplib_cpp_server
rdplib_cpp_client
```

The server listens on UDP port `9000`, and the client connects to
`127.0.0.1:9000`.  The programs do not take command line arguments.

The client performs this sequence:

1. Send a reliable login request.
2. Complete the reliable client ready and server ready exchange.
3. Enter game simulation and send an unreliable position update every second.
4. Send a reliable profile save every 6 seconds.
5. After 30 seconds, send a reliable logout request and wait for the matching reply and orderly transport close.

The server enters game simulation for a client only after receiving its first
position update.  It continues processing network traffic while the client is
logging in, entering the game, or leaving, but those states do not participate
in game simulation.

The close paths are deliberate.  A clean application logout gives the queued
reply and FIN time to leave through the endpoint.  A connection which has
already failed is released immediately without linger.
