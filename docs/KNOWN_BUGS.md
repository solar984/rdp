# Known bugs and assumptions

The raw API assumes its caller passes valid structures, allocations succeed, the peer sends the expected packet shapes, and shutdown happens in the expected order.  This was workable when the library was built directly into a game.  It is not a good interface for a new application.

The default build checks the assumptions most likely to cause a serious failure.  `RDPLIB_SOURCE_FAITHFUL=ON` keeps the recovered behavior, including its unchecked assumptions.  Both builds use the same function names and compile only the selected implementation.

This document lists the important cases a raw API caller takes responsibility for.

## Datagram and parser constraints

- `usend` joins scatter/gather buffers in a fixed 32,768 byte local workspace.  The recovered function does not reserve space for CRC or encryption padding and does not validate the combined vector size.
- The endpoint receives UDP into 536 bytes.  A 512 byte fragment with every optional header field, a full ACK mask, CRC, and cipher padding can exceed that buffer.  The sender does not automatically reduce the fragment or ACK mask to fit it.
- The recovered connected parser reads the flag selected ACK, message, fragment, and stream fields before checking the claimed length.  A physically short datagram can therefore be read beyond its supplied length.
- Every nonfinal fragment must contain 512 bytes.  The recovered final fragment check does not enforce the sender's 1..512 byte rule.
- A 0 byte final fragment can enter reliable receive bookkeeping, but normal input handling does not assemble it because it has neither payload nor FIN.  Its reliable ID may be acknowledged while the group remains incomplete.
- Fragment assembly assumes count, base ID, and fragment positions were already checked.  It allocates from the advertised shape and initializes the result without checking allocation failure.
- The default `rx_assemble` null check avoids that historical null write, but receive sequence and reliable ID bookkeeping has already run.  If an unfragmented reliable arrival allocation fails, its ID can still be acknowledged while no application message is delivered.  There is no recovered rollback or abort result that could undo that state; deterministic failure injection coverage preserves this known nontransactional result.
- In the source faithful build, recording an out of order packet 32 through 64 places behind the newest sequence uses the recovered signed 32 bit mask.  It can mark a new packet as a duplicate or fail to remember a packet already received.  Reliable traffic normally recovers through reliable ID tracking and retry.  Unreliable traffic can be lost or delivered twice.

The default parser checks every optional field before reading it.  It limits a group to the sender's 100 fragment maximum and requires a 1..512 byte final fragment before changing receive state.

## Send constraints

- The recovered sender accepts stream values through 255, but a connection owns only 20 stream records.  Valid stream IDs are 0..19.
- The reliable history check uses the current count without reserving IDs for every fragment in the new call.  A large fragmented send near the 4,096 ID limit can exceed the history array.
- Fragments are queued 1 at a time.  If a later allocation fails, the earlier fragments remain in flight as an incomplete logical message.
- Backend result 1 can abort and flush while a reliable fragment is temporarily outside every queue.  The sender then reattaches that fragment and a call with multiple fragments can continue queueing later fragments on the disconnected connection.  This recovered continuation/ownership hazard remains in both profiles.
- The unreliable send path dereferences its allocation without checking for failure.
- If the first outgoing message in a direction is unreliable, the recovered scheduler repeatedly selects it before SYN acknowledgement even though it cannot send it.  The I/O thread spins while holding the connection lock, so another thread cannot submit the reliable message needed to establish that direction.
- The recovered serializer consumes a pending ACK report before it knows the backend result.  Retryable result 5 leaves no report pending until another reliable arrival or retransmission creates one.
- The recovered keepalive and FIN helpers do not check reliable history space.  A keepalive at a span of 4,095 consumes the last representable position, leaving no position for a later FIN.
- The recovered ACK validator checks whether an ID was allocated, but not whether its record reached `sent_messages`.  An ACK can clear an outstanding bit for a record which is still ready or window blocked.  A cumulative report can also move the base past that record while leaving the queued record behind.
- Several byte count additions use unchecked 32 bit arithmetic.
- The recovered byte buffer gate checks current occupancy instead of current occupancy plus the proposed send.  This allows a single call to cross the nominal limit.

The default send path checks the stream, payload size, arithmetic, allocations, byte limit, and reliable ID count before queueing any fragments.  It also:

- leaves an early unreliable message dormant until a reliable message establishes that transmit direction;
- restores the pending ACK report and deadline after retryable result 5 while retaining the recovered bandwidth delay;
- stops scheduling keepalive when only the final FIN position remains; and
- checks the complete ACK report before applying it, requiring every newly claimed outstanding ID to have a matching record in `sent_messages`.

A rejected ACK report does not partially retire otherwise valid IDs.  Result 14 means the byte buffer is full.  Result 15 means there is no room for another reliable ID.  The caller retains its payload and may try again after the transport makes progress.

The default build rejects a projected outstanding count of 4,096 or more.  At exactly 4,096 IDs, the allocation and cumulative ACK positions are numerically indistinguishable.  FIN may consume the final position at a span of 4,095.  Both control helpers return result 15 without changing state when their required position is unavailable.

## Connection hash and endpoint constraints

- `sockaddr_cmp` returns an uninitialized local value when both endpoints use the same unsupported address family.  IPv4, IPX (family 6), and COMPORT (family 69) have defined paths.
- `connhash_create` clears its bucket allocation before checking whether allocation succeeded.
- Endpoint uniqueness is assumed.  Inserting a duplicate can return and lock an older hash member while returning the newly allocated pointer.
- A dormant receive gate returns without dropping a temporary hash reference.  No writer for that field appears in the reference clients.

The default hash constructor checks its pointer, exponent, allocation size, and allocation result before storing the new table.

## Message and thread ownership

- A raw received message contains a borrowed connection pointer.  Delivery does not add a connection reference.
- Raw `rdp_receive` supports a single receiving application thread.  The producer copies a complete intrusive list onto an application list which must be empty.
- Repeating a pending timed raw close replaces the installed result and event pair without waking the earlier waiter.
- Teardown assumes there are no temporary connection references, queue writers, receiving application threads, or caller owned raw messages.
- The historical `net_connect` and `net_shutdown` wrapper uses a single unsynchronized process global endpoint.  `net_shutdown` assumes that endpoint exists even when its connection argument is null.  The wrapper remains in `rdplib/src/net.c` for reference and is not part of the normal CMake library targets.

The `rdplib_*` interface assigns each endpoint to a single application thread, returns connection handles with explicit ownership, requires received messages to be released, and copies inspection results.  It does not make an endpoint safe for several application threads to call at once.

## Close behavior

- Cumulative ACK processing frees sent messages before updating the ACKTHRU base.  A FIN callback can therefore fail to complete a close from that ACK alone.
- A cooperative peer normally returns STOP when its application closes the received FIN.  A peer which only sends the ACK can leave a raw close waiter pending until linger teardown reports failure.
- STOP immediately disables further transmission.  FIN remains an ordered reliable application indication.  Both are separate from local linger and transport abort.
- The Mac `uevent_create` wrapper reports success even when semaphore creation fails.  The POSIX source faithful profile preserves that return value while avoiding use of an uninitialized semaphore; the default profile reports error 3.

Application code should use [`rdplib_connection_begin_close`](API.md#rdplib_connection_begin_close).  It begins close without exposing completion pointers or the internal I/O thread.

## Allocator and statistics

- Fast pool growth clears a new host allocation before checking it.  Allocation failure can become a null write.
- `fast_free` trusts the private pool index stored immediately before the caller pointer.
- The process global statistics pointer is used without null checks.
- Process-global statistics counters are updated under different connection locks.  They do not form a single atomic snapshot across every connection.  The normal API's per-connection counter getter is a separate snapshot protected by that connection's lock.

All maintained profiles clear new backing storage, matching the Mac clients.  The recovered Windows clients did not clear it.

`rdplib_runtime_create` initializes the allocator and statistics for normal applications.  Raw callers must initialize them directly and release memory through the matching allocator.

## Randomness and encrypted framing

- Source faithful `tx_init` seeds the process global C `rand` state from the millisecond clock for every connection, then truncates the first result to a 16 bit reliable ID.  Connections created from the same seed can repeat IDs.  Transport initialization also changes unrelated application uses of `rand`.
- The default build redirects only those initial ID calls to a host random service and leaves the application's C library state unchanged.
- Encrypted framing generates padding with `rand`.  The cipher uses fixed schedules, has no handshake, and has no per session secret.  It is a compatibility transform, not cryptographic protection.

## Peer authentication

The UDP address and port identify a connection, but packet sequences, reliable IDs, ACKs, STOP, FIN, RESET, and payload have no per session authentication.  CRC 32 is not keyed, and the fixed cipher does not change this.

This means:

- a valid RESET aborts the connection;
- STOP disables further transmission;
- an accepted future packet sequence can move legitimate traffic outside the receive history;
- a valid looking ACK can retire data the peer never received;
- short connected control forms can bypass the normal CRC length check; and
- connectionless `0xFFFF` traffic bypasses connected lookup, packet sequence validation, and the connected CRC trailer.

## Diagnostics and optional backends

- The serial only I/O wait path tests an uninitialized result in the reference implementations.
- `format_sockaddr` has no output capacity argument.  Its family 6 branches are unsafe or incomplete depending on the platform.
- Unknown socket errors use a single process global static text buffer without synchronization.
- Source faithful discard paths call `discard_log_append`, which lazily opens one unsynchronized process global file and never closes it. Windows uses `discard.log`; the retained Mac lineage uses `Logs:discard.log`. Builds without `RDPLIB_SOURCE_FAITHFUL` omit these retail logging calls.
- Traceroute trusts a fixed 90 record sample cursor and does not release every completed snapshot during destruction.
- Some send error paths abort and flush the connection while the current reliable record is temporarily outside every queue, then reattach that record.
- ICMP attribution matches the current remote IPv4 address and UDP port.  Neither the recovered raw parser nor the checked platform adapters compare the quoted payload with a saved wire datagram.  A sufficiently late diagnostic after exact endpoint reuse can therefore apply to the replacement connection.

IPv4 UDP, threads, synchronization, timing, and wake datagrams are implemented on Windows and POSIX.  POSIX reports the obsolete IPX transport and physical serial backend as unavailable.

The default build handles attributed ICMP diagnostics through `IP_RECVERR` on Linux and `SIO_UDP_CONNRESET` on Windows.  It does not open the recovered raw ICMP or traceroute sockets.  The source faithful build keeps the recovered raw socket behavior.  The default Windows and Linux builds therefore report attributable UDP errors without running the recovered active traceroute collector.

## Default build checks

The default build checks:

1. datagram vector and framing workspace size;
2. selected parser fields, fragment count, and final fragment length;
3. send stream, payload, arithmetic, allocation, byte buffer, and projected history limits;
4. ACK state after retryable backend result 5;
5. reliable history space for keepalive and FIN;
6. ACK claims against records which entered the sent queue;
7. connection hash allocation and supported address family shape;
8. the pre SYN unreliable scheduler spin; and
9. repeated ICMP disconnect publication for an already aborted connection.

It also keeps initial reliable ID generation out of the application's C library random state.  Successful valid sends retain the recovered wire bytes and state changes.  Outside the cases listed above, allocation, scheduling, and normal close continue to behave like the recovered transport.  The diagnostic and traceroute backend differences are described above.
