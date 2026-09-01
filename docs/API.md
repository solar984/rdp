# Application API

`rdplib` has two C interfaces:

- [`rdplib.h`](../rdplib/include/rdplib.h) is the normal application interface.  It uses opaque handles and makes ownership explicit.
- [`rdp.h`](../rdplib/include/rdp.h) exposes the recovered transport directly.  It is useful for compatibility tools, protocol research, and programs that need to manage the internal objects themselves.

See [PROTOCOL.md](PROTOCOL.md) for the wire format, sequence numbers, acknowledgements, retransmission, flow control, keepalive, and shutdown protocol.

## rdplib API at a glance

Every function in the normal interface begins with `rdplib_`.

| Area | Functions | Purpose |
| --- | --- | --- |
| Runtime | [`rdplib_runtime_create`](#rdplib_runtime_create), [`rdplib_runtime_destroy`](#rdplib_runtime_destroy) | Set up and release the process runtime. |
| Endpoint | [`rdplib_endpoint_create`](#rdplib_endpoint_create), [`rdplib_endpoint_create_ex`](#rdplib_endpoint_create_ex), [`rdplib_endpoint_destroy`](#rdplib_endpoint_destroy), [`rdplib_endpoint_local_port`](#rdplib_endpoint_local_port), [`rdplib_endpoint_get_input_rate`](#rdplib_endpoint_get_input_rate), [`rdplib_endpoint_process`](#rdplib_endpoint_process) | Run a bidirectional UDP endpoint, inspect its input rate, configure its socket buffers, and move received data to the application. |
| Accept and connect | [`rdplib_endpoint_accept`](#rdplib_endpoint_accept), [`rdplib_endpoint_pop_connectionless`](#rdplib_endpoint_pop_connectionless), [`rdplib_connect`](#rdplib_connect) | Accept connections, receive connectionless messages, or connect to a peer. |
| Connection lifetime | [`rdplib_connection_release`](#rdplib_connection_release), [`rdplib_connection_is_usable`](#rdplib_connection_is_usable), [`rdplib_connection_begin_close`](#rdplib_connection_begin_close) | Check, close, and release a connection handle. |
| Connection configuration | [`rdplib_connection_enable_keepalive`](#rdplib_connection_enable_keepalive), [`rdplib_connection_enable_keepalive_with_interval`](#rdplib_connection_enable_keepalive_with_interval), [`rdplib_connection_set_data_rate`](#rdplib_connection_set_data_rate), [`rdplib_connection_set_send_buffer_size`](#rdplib_connection_set_send_buffer_size) | Configure local connection behavior. |
| Connection testing | [`rdplib_connection_set_packet_drop_callback`](#rdplib_connection_set_packet_drop_callback) | Discard selected connected datagrams for testing. |
| Connection I/O | [`rdplib_connection_send`](#rdplib_connection_send), [`rdplib_connection_pop_message`](#rdplib_connection_pop_message) | Send and receive application messages. |
| Connection inspection | [`rdplib_connection_get_remote_ipv4`](#rdplib_connection_get_remote_ipv4), [`rdplib_connection_get_counters`](#rdplib_connection_get_counters), [`rdplib_connection_get_perf_stats`](#rdplib_connection_get_perf_stats), [`rdplib_connection_get_disconnect_info`](#rdplib_connection_get_disconnect_info) | Copy address, traffic counters, performance, and disconnect information. |
| Message inspection | [`rdplib_message_flags`](#rdplib_message_flags), [`rdplib_message_stream`](#rdplib_message_stream), [`rdplib_message_size`](#rdplib_message_size), [`rdplib_message_data`](#rdplib_message_data), [`rdplib_message_is_connectionless`](#rdplib_message_is_connectionless), [`rdplib_message_is_disconnect`](#rdplib_message_is_disconnect), [`rdplib_message_has_fin`](#rdplib_message_has_fin), [`rdplib_message_get_sender_ipv4`](#rdplib_message_get_sender_ipv4) | Inspect an owned message without changing it. |
| Message lifetime | [`rdplib_message_release`](#rdplib_message_release) | Release an owned message. |

An endpoint can listen and connect.  There is no listener subclass, stream factory, shared connection handle, or public thread object.

## Build behavior

The default build adds checks around a few assumptions made by the recovered transport:

- Pending ACK state is preserved after a retryable backend send.
- The final reliable history position is kept available for FIN.
- An outstanding ID is not retired until its record has entered the sent queue.
- An early unreliable packet is held until a reliable SYN establishes that direction.

`RDPLIB_SOURCE_FAITHFUL=ON` builds the original versions under the same function names.  This also preserves their unchecked assumptions.

The static `rdplib` target is always built.

`RDPLIB_BUILD_SHARED=ON` is the default.  It also builds `rdplib_shared` from the same sources.  The output is `rdplib.dll` and `rdplib_import.lib` on Windows, `librdplib.so` on Linux, or the corresponding shared library on another supported POSIX host.

Only the functions marked with `RDPLIB_API` in `rdplib.h` are exported by the shared library.  The raw API is available when statically linking or compiling the library into another project.

Set `RDPLIB_BUILD_SHARED=OFF` when an embedding project only needs the static library.

## Constants and return values

Both interfaces include [`rdplib_constants.h`](../rdplib/include/rdplib_constants.h).

- `RDP_*` names describe recovered transport values used by the raw API.
- `RDPLIB_*` names describe flags, results, reasons, and limits used by the normal API.  Values taken from the recovered transport keep their original numbers.
- Negative `RDPLIB_ERROR_*` values come from the `rdplib_*` wrapper.
- Positive endpoint, connect, and send results retain the transport's numeric values.

The normal endpoint accepts `RDPLIB_USE_CRC` and `RDPLIB_USE_ENCRYPTION`.  It always creates an IPv4 endpoint.  The raw API also exposes the recovered transport selection flags.

Endpoint flags are fixed width macros.  The encryption bit is `UINT32_C(0x80000000)`, which does not fit in a signed 32 bit C enum.  Smaller result domains use enums.

| Function family | Non error result | Error result |
| --- | --- | --- |
| Runtime, configuration, and status functions | `RDPLIB_OK` | Negative `RDPLIB_ERROR_*` |
| `rdplib_endpoint_create` and `rdplib_endpoint_create_ex` | `RDPLIB_ENDPOINT_CREATE_OK` | Negative wrapper errors or positive `RDPLIB_ENDPOINT_CREATE_*` results |
| `rdplib_endpoint_destroy` | `RDPLIB_OK` | `RDPLIB_ERROR_BUSY` while an application connection handle remains |
| `rdplib_connect` | `RDPLIB_CONNECT_OK` | Negative wrapper errors or positive `RDPLIB_CONNECT_*` results |
| `rdplib_connection_send` | `RDPLIB_CONNECTION_SEND_OK` | Negative wrapper errors or positive `RDPLIB_CONNECTION_SEND_*` results |
| `rdplib_connection_begin_close` | `RDPLIB_OK` | Negative `RDPLIB_ERROR_*` |
| `rdplib_endpoint_process` | Nonnegative arrival count | Negative `RDPLIB_ERROR_*` |
| Value and message accessors | The requested value, pointer, or boolean | 0 or null where documented |

Send result 14 means the configured byte buffer is full.  Result 15 means there is no room for another reliable ID.  Neither result consumes the new message.  The application can try again after the transport has made progress.

`RDPLIB_ERROR_PLATFORM` means an operating system socket option call failed.

## Ownership and threading

One serialized application context owns an endpoint and every handle obtained from it.  The owning context calls every endpoint and connection operation except `rdplib_connection_send`.  Different endpoints may have different owning contexts.

`rdplib_connection_send` is the only concurrent application operation.  Multiple application threads may call it concurrently, including multiple calls for the same connection, and those calls may overlap the owning context while the handle remains application owned.  This does not transfer or share ownership of the connection handle.

Concurrent sends on one connection are serialized internally.  The order between overlapping calls is unspecified.  Applications which require ordering between producers must establish that ordering before calling rdplib.

`rdplib_connection_begin_close` is linearized with concurrent sends.  A successful send ordered before a successful close transition is completed before close starts.  Once close starts, later sends return `RDPLIB_ERROR_NOT_USABLE` without consuming the message.  A close attempt which returns `RDPLIB_ERROR_BUSY` does not start that transition.

Stop admitting new sends before beginning a shutdown which needs bounded progress.  Existing sends may safely finish concurrently with the close attempt, but mutex scheduling does not guarantee that close wins against an unlimited stream of new send calls.

Release remains an exclusive lifetime operation.  Before calling `rdplib_connection_release`, the application must prevent new calls and wait for every active send to return.  No thread may use the handle after release begins.

The I/O thread is private to the endpoint.  The application does not join or manipulate it.  Endpoint destruction stops and joins it internally.

The normal lifetime is:

1. Create the runtime.
2. Create one or more endpoints.
3. Accept or connect application connections.
4. Drain each connection, begin close or observe a terminal state, and release its handle.
5. Destroy the endpoints.
6. Release any popped messages which are still owned by the application.
7. Destroy the runtime.

A popped message may outlive its connection and endpoint.  It may not outlive the runtime.

Debug builds assert on some lifetime mistakes before returning the release build error.  In particular, runtime destruction asserts when endpoints or popped messages remain, and close asserts when its connection still has queued messages.  These errors are not intended to be used for polling or normal cleanup flow.

## Raw interface

The raw header exposes the recovered structures, global state, scatter/gather send, borrowed pointers, and both close functions.

The shared library does not export the raw functions.  Use this interface by statically linking `rdplib` or compiling it into the application.

Raw callers must set up the allocator and statistics, free received messages with `fast_free`, follow the connection reference rules, and make sure nothing is using the endpoint during destruction.

`connection_close` is nonblocking.  Its optional completion pointers are borrowed, and only one waiter is supported.

`connection_close_wait` creates an event and blocks until close completes or the connection is destroyed.  Its timeout argument controls linger.  It does not limit how long the function waits.

Use the rdplib examples in [`examples/c/rdplib_server.c`](../examples/c/rdplib_server.c) and [`examples/c/rdplib_client.c`](../examples/c/rdplib_client.c).  The raw examples are [`examples/c/hello_server.c`](../examples/c/hello_server.c) and [`examples/c/hello_client.c`](../examples/c/hello_client.c).

## Function reference

### `rdplib_runtime_create`

```c
int rdplib_runtime_create(rdplib_runtime_t **output, uint32_t fast_allocator_bytes);
```

Creates the process runtime and initializes the shared allocator.  `fast_allocator_bytes` is the allocator's initial byte budget and must be greater than 0.  The allocator grows when its pools are exhausted.

The recovered process global counters are not safe for concurrent connection activity.  The facade runtime redirects those writes to thread local discard storage in both behavior profiles.  Per connection counters and performance snapshots remain available through their connection APIs.  Direct raw API use outside the facade retains the recovered global counter behavior.

Only one runtime may be active in a process because the recovered transport stores its allocator globally.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_OUT_OF_MEMORY`.

`RDPLIB_ERROR_OUT_OF_MEMORY` covers allocation of the runtime object.  The initial fast allocator storage follows the recovered unchecked behavior, so failure there is not reported.

On success, the application owns `*output`.  Release it with `rdplib_runtime_destroy` after every endpoint and popped message is gone.

### `rdplib_runtime_destroy`

```c
int rdplib_runtime_destroy(rdplib_runtime_t *runtime);
```

Destroys the runtime.  Every endpoint must already be destroyed and every popped message must already be released.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_BUSY`.  A debug build also asserts if an endpoint or popped message remains.

On success, the runtime is freed and the pointer is no longer valid.  A busy return leaves it unchanged.

### `rdplib_endpoint_create`

```c
int rdplib_endpoint_create(
    rdplib_runtime_t *runtime,
    rdplib_endpoint_t **output,
    uint16_t local_port,
    uint32_t expected_connections,
    uint32_t flags);
```

Creates an IPv4 UDP endpoint, its connection table and queues, and its I/O thread.

Set `local_port` to 0 to let the operating system choose a port.  `flags` may contain `RDPLIB_USE_CRC` and `RDPLIB_USE_ENCRYPTION`.

This function retains the operating system receive and send socket buffer defaults.  It is equivalent to calling `rdplib_endpoint_create_ex` with null options.

`expected_connections` is a recovered connection table sizing hint.  Use 1 for a single peer endpoint.  Any other value selects the normal larger table.

Returns `RDPLIB_ENDPOINT_CREATE_OK`, a negative `RDPLIB_ERROR_*`, or a positive `RDPLIB_ENDPOINT_CREATE_*` result.

On success, the application owns `*output`.  Destroy it with `rdplib_endpoint_destroy` after releasing every application owned connection.

### `rdplib_endpoint_create_ex`

```c
int rdplib_endpoint_create_ex(
    rdplib_runtime_t *runtime,
    rdplib_endpoint_t **output,
    uint16_t local_port,
    uint32_t expected_connections,
    uint32_t flags,
    const rdplib_endpoint_options_t *options);
```

Creates the same IPv4 endpoint as `rdplib_endpoint_create` and can request receive and send buffer sizes for its UDP socket.

Set `options->structure_size` to `sizeof(rdplib_endpoint_options_t)`.  A receive or send value of 0 retains the operating system default for that direction.  A null options pointer retains both defaults.

Explicit buffer values must fit in a positive native `int`.  The options are applied after the socket is bound and before the I/O thread starts.  If an operating system call fails, creation fails and releases the partial endpoint.  An operating system may accept a request but provide a different value, so query the endpoint after creation when the effective value matters.

Returns the same result set and ownership as `rdplib_endpoint_create`.

### `rdplib_endpoint_destroy`

```c
int rdplib_endpoint_destroy(rdplib_endpoint_t *endpoint);
```

Stops the I/O thread and destroys the endpoint.

Every application owned connection must be released first.  If one remains, this returns `RDPLIB_ERROR_BUSY` without changing the endpoint.

The function releases unaccepted connections and messages which are still queued inside the endpoint.  Messages which were already popped remain owned by the application and may be released after the endpoint is gone.

Passing null is allowed and returns `RDPLIB_OK`.

Returns `RDPLIB_OK` or `RDPLIB_ERROR_BUSY`.  On success, the endpoint is freed and the pointer is no longer valid.  A busy return leaves it unchanged.

### `rdplib_endpoint_local_port`

```c
uint16_t rdplib_endpoint_local_port(const rdplib_endpoint_t *endpoint);
```

Returns the endpoint's local UDP port.  This is useful after creating an endpoint with port 0.

Returns 0 for a null endpoint.

The endpoint remains owned by the application.

### Endpoint socket buffer configuration

```c
int rdplib_endpoint_set_socket_receive_buffer_size(
    rdplib_endpoint_t *endpoint,
    uint32_t bytes);

int rdplib_endpoint_set_socket_send_buffer_size(
    rdplib_endpoint_t *endpoint,
    uint32_t bytes);

int rdplib_endpoint_get_socket_receive_buffer_size(
    const rdplib_endpoint_t *endpoint,
    uint32_t *bytes);

int rdplib_endpoint_get_socket_send_buffer_size(
    const rdplib_endpoint_t *endpoint,
    uint32_t *bytes);
```

The setters request a new `SO_RCVBUF` or `SO_SNDBUF` value on the endpoint's UDP socket.  A runtime request must be greater than 0 and fit in a positive native `int`.  These calls do not change any per connection RDP queue.

The getters return the native value reported by `getsockopt`.  Windows can accept a request but return less than the requested value.  Linux doubles an explicit request for bookkeeping and returns that doubled value.  For example, a satisfied 1 MiB Linux request is normally reported as 2 MiB.  Linux limits unprivileged requests with `net.core.rmem_max` and `net.core.wmem_max`.

The operating system may cap a request without making `setsockopt` fail.  Applications which require a minimum should call the matching getter and inspect its value.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_PLATFORM`.  The endpoint remains owned by the application.

### `rdplib_endpoint_get_input_rate`

```c
int rdplib_endpoint_get_input_rate(
    const rdplib_endpoint_t *endpoint,
    rdplib_endpoint_input_rate_t *input_rate);
```

Copies the endpoint's latest completed input-rate sample.  `bytes_per_second` is the recovered endpoint-wide input accounting, not application payload throughput.  Each counted UDP datagram contributes its length plus the transport's fixed 28 byte IPv4 and UDP overhead estimate.  Datagrams rejected before accounting, including receives shorter than two bytes and the transport's internal loopback wakeup, are excluded.  The separate raw-ICMP receive path uses the same accounting when present.  `duplicate_reliable_bytes_per_second` is the subset attributed to duplicate reliable message IDs.  Do not add the duplicate value to the total.

At the end of an I/O-loop pass, the thread publishes a new sample if more than one second has elapsed since the previous sample.  An idle endpoint can therefore retain an older sample until the I/O loop wakes.  Querying it does not force a sample or maintain a high water mark.  The two values are copied from one coherent sample.

The interval accumulators and published values are `uint32_t` and wrap naturally.  The maintained profile widens the multiplication by 1000 during the rate calculation before narrowing the result; the source faithful profile retains the recovered 32-bit intermediate overflow behavior.  A source faithful endpoint also retains the recovered uninitialized first total-rate value until the first sample is published.

Returns `RDPLIB_OK` or `RDPLIB_ERROR_INVALID_ARGUMENT`.  Invalid calls leave the output unchanged, the caller owns the output, and the endpoint remains owned by the application.

### `rdplib_endpoint_process`

```c
int rdplib_endpoint_process(rdplib_endpoint_t *endpoint, int32_t timeout_ms);
```

Waits for an arrival up to `timeout_ms`, then moves everything currently ready into the application queues.

A `timeout_ms` value of 0 polls, a positive value waits up to that many milliseconds, and `-1` waits without a deadline.  Other negative values are unsupported.

Call this from the application thread which owns the endpoint.  The function does not transfer ownership of the endpoint.

Returns the nonnegative number of arrivals processed, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_OUT_OF_MEMORY`.

### `rdplib_endpoint_accept`

```c
rdplib_connection_t *rdplib_endpoint_accept(rdplib_endpoint_t *endpoint);
```

Returns the next incoming connection owned by the application, or null if none are ready.

An incoming connection becomes visible after it produces its first application message or FIN.  SYN by itself does not make it visible.

The returned handle belongs to the application.  Release it exactly once with `rdplib_connection_release`.  A null return transfers nothing.

### `rdplib_endpoint_pop_connectionless`

```c
rdplib_message_t *rdplib_endpoint_pop_connectionless(rdplib_endpoint_t *endpoint);
```

Returns the next connectionless message, or null.  Connectionless messages do not create connection handles.

The returned message belongs to the application.  Inspect its sender with `rdplib_message_get_sender_ipv4` and release it exactly once with `rdplib_message_release`.  A null return transfers nothing.

### `rdplib_connect`

```c
int rdplib_connect(
    rdplib_endpoint_t *endpoint,
    rdplib_connection_t **output,
    const char *host,
    uint16_t port);
```

Creates an outgoing connection to an IPv4 host.  This does not necessarily send anything immediately.

The first reliable transport message carries SYN.  This is normally an application message.  It can also be FIN if the connection is closed before anything else is sent.  Keepalive does not become eligible until the direction has already sent SYN.

Configure keepalive, data rate, and send buffer size after connecting.

Returns `RDPLIB_CONNECT_OK`, a negative `RDPLIB_ERROR_*`, or a positive `RDPLIB_CONNECT_*` result.  On a valid call, `*output` is cleared before connection setup begins and remains null if setup fails.

On success, the application owns `*output`.  Release it exactly once with `rdplib_connection_release`.

### `rdplib_connection_release`

```c
void rdplib_connection_release(rdplib_connection_t *connection);
```

Releases an application owned connection handle.  Call it exactly once.

Drain and release its messages first.  Start local close first if the peer should receive FIN.  A handle may also be released after peer FIN, disconnect, STOP, or internal removal.

Passing null does nothing.  Debug builds assert if messages remain or if a live connection is released without first starting close or reaching a terminal state.

This function has no return value.  A non null handle is freed and must not be used again.

Release is not a sender lifetime barrier.  Before calling it, prevent new send calls and wait for every active send to return.  A mutex inside rdplib cannot protect a thread which retains a stale handle and enters after the handle has been freed.

### `rdplib_connection_is_usable`

```c
int rdplib_connection_is_usable(rdplib_connection_t *connection);
```

Returns nonzero while the connection can still send.

It becomes false after local close starts, peer FIN, disconnect, STOP, or internal removal.  What the application does with its player or session is a separate decision.

Returns 0 for a null handle.  The call does not change ownership or connection state.

### `rdplib_connection_enable_keepalive`

```c
int rdplib_connection_enable_keepalive(rdplib_connection_t *connection);
```

Enables reliable keepalives for the rest of the connection.  Calls may be repeated.

The interval starts at `RDPLIB_DEFAULT_KEEPALIVE_INTERVAL_MS`, which is 10,000 ms.  If a custom interval was already set, this function does not replace it.

Keepalive starts only after this transmit direction has sent SYN on its first reliable message.  It does not establish an otherwise idle direction.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_NOT_USABLE`.  The application keeps ownership of the connection.

### `rdplib_connection_enable_keepalive_with_interval`

```c
int rdplib_connection_enable_keepalive_with_interval(
    rdplib_connection_t *connection,
    uint32_t interval_ms);
```

Sets and enables a per connection keepalive interval.  It accepts 1 through `INT32_MAX` milliseconds.

The deadline is measured from the last reliable enqueue.  Shortening the interval can make a keepalive due immediately.

Keepalive starts only after this transmit direction has sent SYN on its first reliable message.  It does not establish an otherwise idle direction.

The source faithful build does not have this option.  It returns `RDPLIB_ERROR_NOT_SUPPORTED` and continues using the recovered 10,000 ms value.

The normal build returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_NOT_USABLE`.  The application keeps ownership of the connection.

### `rdplib_connection_set_packet_drop_callback`

```c
int rdplib_connection_set_packet_drop_callback(
    rdplib_connection_t *connection,
    rdplib_packet_drop_callback_t callback,
    void *context);
```

Installs or replaces the normal build's packet drop callback.  Return nonzero from the callback to discard a datagram as if it were lost.

The callback receives a borrowed, contiguous RDP datagram.  The direction is `RDPLIB_PACKET_DROP_INBOUND` or `RDPLIB_PACKET_DROP_OUTBOUND`.  `RDPLIB_PACKET_DROP_BOTH` is only an application mask.

Outbound data is inspected after the send vector is joined and before CRC, encryption, or padding.  Inbound data is inspected after framing is removed and the connection is found, but before header parsing, ACK handling, sequencing, or fragment assembly.

Retransmissions, ACK packets, keepalives, and fragments are separate callback calls.  The callback cannot assume it has a complete application message.

The callback runs under the connection lock.  Immediate output can call it from `rdplib_connection_send`.  Scheduled output and input call it from the I/O thread.  It must return quickly, must not retain or change the packet, and must not reenter rdplib.  Reentry through a different connection can deadlock against another callback which acquires the same connections in the opposite order.

Pass null as the callback to unregister it.  This waits for a callback which is already running.  Unregister it before the application can lose ownership of `context`.

The source faithful build returns `RDPLIB_ERROR_NOT_SUPPORTED` and never calls the callback.

The normal build returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_NOT_USABLE`.

The application owns `context`.  rdplib only borrows it during a callback.  The application also keeps ownership of the connection.

### `rdplib_connection_pop_message`

```c
rdplib_message_t *rdplib_connection_pop_message(rdplib_connection_t *connection);
```

Returns the next message from a connection, or null.

Every returned message belongs to the application and must be released exactly once with `rdplib_message_release`.  This includes FIN and disconnect messages.  A null return transfers nothing.

The application keeps ownership of the connection.

### `rdplib_connection_send`

```c
int rdplib_connection_send(
    rdplib_connection_t *connection,
    const void *data,
    uint32_t bytes,
    uint32_t stream,
    uint32_t flags);
```

Sends one contiguous application message.  Stream IDs are 0 through 19.  Stream 0 does not add application ordering.

Use `RDPLIB_SEND_RELIABLE` or `RDPLIB_SEND_UNRELIABLE`.  A reliable message may contain up to 51,200 bytes and is split into 512 byte fragments.  An unreliable message may contain up to 512 bytes.

A zero byte reliable send returns success without allocating an ID or sending a datagram.  A zero byte unreliable send sends an empty datagram, but normal receive handling does not deliver an application message for it.

The first packet sent in each direction should be reliable.  It carries SYN and establishes the initial reliable ID.  The default build holds an unreliable message submitted before SYN and sends it after the first reliable message is acknowledged.

Result 14 means the configured send buffer is full.  Result 15 means there is no room for another reliable ID.  Neither result consumes the new message, so it can be tried again after the transport makes progress.

Returns `RDPLIB_CONNECTION_SEND_OK`, a negative `RDPLIB_ERROR_*`, or a positive `RDPLIB_CONNECTION_SEND_*` result.

rdplib copies the payload before returning.  The application keeps ownership of `data` and may reuse or release it after the call.

This function may be called concurrently on the same or different connection handles.  Calls on one connection are serialized internally, but their order is unspecified when they overlap.  Sends on different connections do not share a facade lock.  Each call still returns its ordinary send result and can fail validation or encounter transport backpressure.

The call may overlap endpoint processing and `rdplib_connection_begin_close`.  A send racing with peer FIN, disconnect, or local close either submits before that terminal transition or returns the applicable terminal result.

### `rdplib_connection_begin_close`

```c
int rdplib_connection_begin_close(
    rdplib_connection_t *connection,
    uint32_t linger_timeout_ms);
```

Starts close and returns immediately.  Drain and release every queued message first.  Debug builds assert if messages remain; other builds return `RDPLIB_ERROR_BUSY`.

This function is linearized with concurrent sends.  Once it successfully begins the close transition, later sends return `RDPLIB_ERROR_NOT_USABLE`.  Returning `RDPLIB_ERROR_BUSY` leaves the connection open.  This does not make release safe to race with senders.  The application must still prevent new calls and wait for every active send before releasing the handle.

For bounded shutdown progress, stop admitting new sends before calling this function.  The close operation is safe to race with already active sends, but it is not given priority over an unlimited stream of new callers.

After this call begins close, the handle can no longer send or inspect the underlying connection.  The I/O thread handles FIN, STOP, retransmission, and linger.

A linger of 0 marks the transport connection for immediate removal.  A nonzero linger keeps it in the endpoint table long enough to absorb retries and reordered close packets.

Repeated calls do nothing and return success.  Release the handle when the application has finished its own session work.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, `RDPLIB_ERROR_NOT_USABLE`, or `RDPLIB_ERROR_BUSY`.

The application still owns the wrapper handle after close starts and must release it with `rdplib_connection_release`.  A failed precondition leaves the handle unchanged.

### `rdplib_connection_set_data_rate`

```c
int rdplib_connection_set_data_rate(
    rdplib_connection_t *connection,
    uint32_t bytes_per_second);
```

Sets the local outbound data rate.  The setting is not sent to the peer.

The rate must be greater than 0 because the scheduler divides by it.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_NOT_USABLE`.  The application keeps ownership of the connection.

### `rdplib_connection_set_send_buffer_size`

```c
int rdplib_connection_set_send_buffer_size(
    rdplib_connection_t *connection,
    uint32_t bytes);
```

Sets the local aggregate serialized byte limit across the blocked, ready, and sent queues.  The setting is not sent to the peer.

This is a high water backstop rather than a strict allocation limit.  The current queue size is checked before adding a new message, so the queue can exceed the setting by one application send.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_NOT_USABLE`.  The application keeps ownership of the connection.

### `rdplib_connection_get_remote_ipv4`

```c
int rdplib_connection_get_remote_ipv4(
    rdplib_connection_t *connection,
    uint8_t address[4],
    uint16_t *port);
```

Copies the remote IPv4 address in display order and returns the host order port.

The address is cached in the wrapper, so this remains available after the transport has ended.

Returns `RDPLIB_OK` or `RDPLIB_ERROR_INVALID_ARGUMENT`.  The output buffers belong to the caller and the connection remains owned by the application.

### `rdplib_connection_get_counters`

```c
int rdplib_connection_get_counters(
    rdplib_connection_t *connection,
    rdplib_connection_counters_t *counters);
```

Copies a locked snapshot of the connection's cumulative traffic, acknowledgement, sequencing, discard, RTT-update, and ICMP counters.

The public names use reliable and unreliable terminology.  `reliable_packets_retransmitted` and `reliable_bytes_retransmitted` count additional transmission attempts and are separate from the original reliable transmit counters.  `duplicate_reliable_*` records duplicate reliable arrivals separately rather than including them in `reliable_*_rx`.  The ACK fields are orthogonal packet classifications and can overlap other packet groups; they are not byte totals.

The byte counters preserve the recovered accounting and are not symmetric wire-byte totals.  Transmit data bytes include application payload plus serialized message-ID, fragment, and stream metadata when present.  They exclude the base and ACK header, CRC or encryption expansion, and IP or UDP overhead.  Transmit packet and retransmission counters count attempts, including packets discarded by the maintained profile's drop callback or rejected by the send backend.  Receive data bytes exclude the parsed RDP header, which `header_bytes_rx` records separately.  The in-sequence and out-of-sequence byte counters include both RDP header and data, but not IP or UDP overhead.  The ICMP arrays are indexed by ICMP code.

Every exposed counter is `uint32_t` and wraps naturally.  The internal debug-only rolling `tqd_*` measurements are not exposed.

The snapshot remains available after a peer FIN or transport failure because the raw connection remains attached until local close or release.  Read it before `rdplib_connection_begin_close` or `rdplib_connection_release`; afterward the wrapper no longer has a usable raw connection.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_NOT_USABLE`.  Errors leave the output unchanged, the caller owns the output, and the connection remains owned by the application.

### `rdplib_connection_get_perf_stats`

```c
int rdplib_connection_get_perf_stats(
    rdplib_connection_t *connection,
    rdplib_connection_perf_stats_t *statistics);
```

Copies the current connection performance statistics.  It does not return pointers into the connection.

Call this before local close detaches the transport from the wrapper.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_NOT_USABLE`.  The output structure belongs to the caller.

### `rdplib_connection_get_disconnect_info`

```c
int rdplib_connection_get_disconnect_info(
    rdplib_connection_t *connection,
    rdplib_disconnect_info_t *information);
```

Copies the transport disconnect reason and ICMP information.

The ICMP source is a 4 byte IPv4 address in display order.  The ICMP fields are 0 unless an ICMP message caused the disconnect.

Call this before local close detaches the transport from the wrapper.

Returns `RDPLIB_OK`, `RDPLIB_ERROR_INVALID_ARGUMENT`, or `RDPLIB_ERROR_NOT_USABLE`.  The output structure belongs to the caller.

### `rdplib_message_flags`

```c
uint16_t rdplib_message_flags(const rdplib_message_t *message);
```

Returns the message's RDP flags.  Returns 0 for null.

The message remains owned by the application.

### `rdplib_message_stream`

```c
uint8_t rdplib_message_stream(const rdplib_message_t *message);
```

Returns the message's application stream.  Returns 0 for null.

The message remains owned by the application.

### `rdplib_message_size`

```c
uint32_t rdplib_message_size(const rdplib_message_t *message);
```

Returns the application payload size.  Returns 0 for null.

The message remains owned by the application.

### `rdplib_message_data`

```c
const void *rdplib_message_data(const rdplib_message_t *message);
```

Returns a borrowed pointer to the application payload.  No extra payload copy is made.

The pointer remains valid until `rdplib_message_release`.  It is null for a null message or a zero byte payload.

The application still owns the message.  Copy any bytes which must be kept before releasing it.

### `rdplib_message_is_connectionless`

```c
int rdplib_message_is_connectionless(const rdplib_message_t *message);
```

Returns nonzero for a connectionless message.

Returns 0 for null.  The message remains owned by the application.

### `rdplib_message_is_disconnect`

```c
int rdplib_message_is_disconnect(const rdplib_message_t *message);
```

Returns nonzero for a transport disconnect notification.

Returns 0 for null.  The message remains owned by the application.

### `rdplib_message_has_fin`

```c
int rdplib_message_has_fin(const rdplib_message_t *message);
```

Returns nonzero when the message carries FIN.  FIN and payload size are independent, so a zero byte FIN is valid.

Returns 0 for null.  The message remains owned by the application.

### `rdplib_message_get_sender_ipv4`

```c
int rdplib_message_get_sender_ipv4(
    const rdplib_message_t *message,
    uint8_t address[4],
    uint16_t *port);
```

Copies a connectionless message's sender address in display order and returns the host order port.

Returns `RDPLIB_ERROR_INVALID_ARGUMENT` if the message is not connectionless.

Returns `RDPLIB_OK` on success.  The output buffers belong to the caller and the message remains owned by the application.

### `rdplib_message_release`

```c
void rdplib_message_release(rdplib_message_t *message);
```

Releases a message owned by the application.  Release every popped message exactly once.

The message may outlive its connection and endpoint, but not its runtime.  Passing null does nothing.

This function has no return value.  A non null message is freed and must not be used again.
