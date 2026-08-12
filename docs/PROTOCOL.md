# RDP protocol

## About this document

This describes the wire format and timing behavior implemented by `rdplib`.  It is the RDP transport used by the EQMac PPC, EQMac Intel, and TAKP Windows game clients.

It is not IETF RDP or Microsoft's Remote Desktop Protocol.  It is also unrelated to the later Sony UdpLibrary.

The behavior was recovered from all 3 clients and implemented here.  This document covers UDP framing, message types, sequence numbers, acknowledgements, windows, flow control, retransmission, application delivery, keepalive, and close.  The old serial framing is covered near the end.

This is an implementation guide, not a formal specification.  [KNOWN_BUGS.md](KNOWN_BUGS.md) describes the unsafe or unusual behavior retained by the source faithful build.

All multibyte RDP fields are unsigned integers in network byte order unless noted otherwise.

Sequence comparisons subtract at the field's original width, then read the wrapped result as signed.  For a 16 bit field:

```text
forward(a, b) := int16(a - b) > 0
```

The live windows are much smaller than half of the 16 bit space.  Exactly half a cycle has no valid before/after ordering.  Millisecond deadlines use the same idea with 32 bit values.

## How it works

RDP turns application messages into UDP datagrams and adds its own reliability.  It does not work like TCP:

- the first reliable application or control message also establishes the connection;
- reliable fragments have 16 bit message IDs and are acknowledged cumulatively or selectively;
- a fixed 120 position message ID window limits new reliable transmission;
- the receiver may deliver reliable unsequenced messages out of order;
- optional application streams add ordering independently of reliability;
- a local byte rate model paces output, but there is no advertised receive window, congestion control, or rate negotiation; and
- shutdown uses reliable FIN plus an immediate STOP indication and a separate local linger timer.

The outbound layers are:

```text
application message
    |
    +-- split reliable payload into 1..100 pieces of at most 512 bytes
    |
    +-- optional MSGID / FRAGMENT / stream fields
    |
    +-- 4 byte flags + packet sequence header
    +-- optional piggybacked ACK report
    |
    +-- optional CRC 32 trailer
    +-- optional padding and fixed cipher
    |
    `-- one UDP datagram
```

Each fragment is reliable on its own.  The receiver reassembles the application message and applies stream ordering after the fragment IDs have been recorded.

## UDP datagram format

### Connected header

Every connected datagram begins with this variable length header:

```text
+------------------------------+------------------------------+
| Flags (16 bits)              | Packet sequence (16 bits)    |
+------------------------------+------------------------------+
| ACK base (16 bits, optional)                                |
+-------------------------------------------------------------+
| ACK mask (0..15 bytes, optional)                            |
+-------------------------------------------------------------+
| Message ID (16 bits, optional)                              |
+-------------------------------------------------------------+
| Fragment ID (16 bits, optional)                             |
+-------------------------------------------------------------+
| Fragment index (16 bits, optional)                          |
+-------------------------------------------------------------+
| Fragment count (16 bits, optional)                          |
+------------------------------+------------------------------+
| Stream ID (8 bits, optional) | Stream seq (8 bits)          |
+------------------------------+------------------------------+
| Payload (remaining bytes)                                   |
+-------------------------------------------------------------+
```

The diagram shows field order, not fixed row alignment.  Optional fields which are not selected by the flags are left out completely.

The fields occur in exactly this order:

1. flags, 16 bits;
2. packet sequence, 16 bits;
3. ACK base and encoded mask when an ACK form is present;
4. message ID when `MSGID` is set;
5. fragment ID, index, and count when `FRAGMENT` is set;
6. 1 byte stream ID when `SEQUENCED` is set;
7. 1 byte reliable stream sequence when both `SEQUENCED` and `MSGID` are set; and
8. all remaining bytes as payload.

The fixed header is 4 bytes.  The ACK portion is 2 through 17 bytes.  A connected datagram carrying every optional field can have 31 bytes before its application payload and framing trailer.

The I/O thread reads UDP into a fixed 536 byte buffer, so a maximum header and maximum payload do not always fit together.  For a 512 byte fragment carrying an ACK base, these are the largest masks that
still fit:

| Framing   | Fragment without stream fields | Fragment zero with both stream bytes |
| --------- | -----------------------------: | -----------------------------------: |
| Unframed  |                  10 mask bytes |                         8 mask bytes |
| CRC       |                   6 mask bytes |                         4 mask bytes |
| Encrypted |                   5 mask bytes |                         3 mask bytes |

The encrypted limits include CRC and the extra full padding block added to an already aligned packet.  ACK only packets and packets with smaller payloads can use the full 15 byte mask.

The original sender does not shorten a fragment or ACK mask to fit the 536 byte receive buffer.  It can send a maximum fragment with a large piggybacked mask that another client cannot receive whole.  Do not use this combination.

Every connected packet, including an ACK only packet, has a packet sequence.  A retransmission keeps its reliable message ID but gets the sender's current packet sequence.  The packet sequence only
advances after a successful send.

### Flag word

|     Mask | Name            | Wire meaning                                                                                                                          |
| -------: | --------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| `0x0001` | `SYSTEM`        | Transport owned reliable data, currently keepalive. It participates in normal ACK processing but is not delivered to the application. |
| `0x0002` | unused          | No reachable handler gives this bit a meaning. The recovered parser does not require it to be zero.                                   |
| `0x0004` | `ACKTHRU`       | ACK base is cumulative: every allocated reliable ID through the base is acknowledged.                                                 |
| `0x0008` | `MASKOFFSET`    | ACK base is selective: the base itself, but not preceding holes, is acknowledged.                                                     |
| `0x00F0` | ACK mask length | Bits 4..7 encode 0..15 mask bytes. This is a field, not a separate packet kind.                                                       |
| `0x0100` | `MULTI`         | Unimplemented historical multi message form. Receiving it is a protocol error.                                                        |
| `0x0200` | `MSGID`         | A 16 bit reliable message ID follows the ACK field.                                                                                   |
| `0x0400` | `STOP`          | Orders the receiver to stop its transmit side; the sender is closing or no longer accepts further traffic from that peer.             |
| `0x0800` | `FRAGMENT`      | A 6 byte fragment tuple follows the message ID.                                                                                       |
| `0x1000` | `SEQUENCED`     | A stream ID follows; a reliable packet also carries an 8 bit stream sequence.                                                         |
| `0x2000` | `SYN`           | This is the first reliable ID in this direction and may create receiver state.                                                        |
| `0x4000` | `FIN`           | Reliable ordered end of input indication for the application.                                                                         |
| `0x8000` | `RESET`         | Abort the matching connection.                                                                                                        |

`SYN`, `FIN`, and `FRAGMENT` each require `MSGID`.  `ACKTHRU` and `MASKOFFSET` are mutually exclusive.  A nonzero ACK mask length requires exactly one ACK base form.

Flags compose. Typical packets are:

```text
application unreliable            0000
sequenced unreliable              SEQUENCED
application reliable              MSGID
first application reliable        SYN | MSGID
reliable ordered application      MSGID | SEQUENCED
reliable fragment zero            MSGID | FRAGMENT [| SEQUENCED]
later reliable fragment           MSGID | FRAGMENT
keepalive                          SYSTEM | MSGID
locally closing FIN on the wire   STOP | FIN | MSGID [| SYN]
pure acknowledgement              ACKTHRU or MASKOFFSET [| mask length]
```

An ACK report may be added to any of these packets.  The serializer also adds `STOP` during local linger, or `RESET` after the connection has aborted.

### Optional message fields

`MSGID` adds:

```text
uint16 message_id
```

`FRAGMENT` adds, after the required message ID:

```text
uint16 fragment_id
uint16 fragment_index
uint16 fragment_count
```

The first index is 0.  The sender only uses `FRAGMENT` for a group of at least 2 fragments.  Every non final fragment contains exactly 512 payload bytes.  The final fragment contains 1 through 512 bytes.

`SEQUENCED` adds:

```text
uint8 stream_id
uint8 stream_sequence   // present only when MSGID is also set
```

The receiver has streams 0 through 19.  The send API uses stream 0 to mean unsequenced, so applications normally select ordered streams 1 through 19.  A raw peer can still encode `SEQUENCED` with stream 0, and the receiver accepts it.

For a fragmented reliable ordered message, only fragment 0 carries `SEQUENCED`, the stream ID, and the stream sequence.  That sequence identifies the reassembled message, not each fragment.

### ACK field

Let `N = (flags & 0x00F0) >> 4`.  When either ACK form is present, the wire field is:

```text
uint16 ack_base
uint8  ack_mask[N]
```

Mask bit 0 represents `ack_base + 1`, bit 1 represents `ack_base + 2`, and so on.  Bits run from the most significant bit to the least significant bit in each byte:

```text
mask byte 0:  80 40 20 10 08 04 02 01
message ID:   +1 +2 +3 +4 +5 +6 +7 +8
```

A set bit acknowledges that ID.  A clear bit only means that the ID is not covered by this ACK.  It is not a NACK and does not directly schedule retransmission.

The final mask byte must contain at least one set bit.  A trailing all zero mask byte is a protocol error.

### Connectionless form

A network order first word of `0xFFFF` identifies a connectionless datagram:

```text
+------------------------------+------------------------------+
| 0xFFFF (16 bits)             | Opaque payload               |
+------------------------------+------------------------------+
```

It has no packet sequence, connection lookup, ACK processing, or connected CRC trailer.  The sender address is delivered with the payload.  Decryption is attempted before checking the marker, but the
connected CRC rule is skipped once `0xFFFF` is recognized.

This form is separate from connection establishment and cannot create a connected transport association.

## Datagram integrity and optional cipher

Framing mode is fixed when an endpoint is created.  It is not negotiated, so both peers must use the same settings.

### Unframed

With neither option enabled, the connected header and body are the complete UDP payload.

### CRC mode

CRC mode appends a 4 byte, network order CRC 32:

```text
+---------------- RDP header and body ----------------+----------+
|                                                     | CRC 32   |
+-----------------------------------------------------+----------+
```

The calculation is standard reflected CRC 32 with polynomial `0xEDB88320`, seed 0, and initial and final complement.  It covers the connected RDP bytes before the trailer.  Encrypted mode always
includes this CRC even when the CRC option was not set separately.

### Encrypted mode

Encrypted mode first appends CRC, then pads to:

```text
padded_size = (crc_extended_size + 8) & ~7
```

This adds a full 8 byte padding block when the CRC extended packet is already aligned.  Padding comes from C `rand()`.  The low nibble of the last byte stores the padding count from 1 through 8.  Its high nibble remains random.

The padded buffer is transformed in 8 byte blocks using a fixed 4 round DES derived Feistel core and fixed schedules.  Chaining runs backward:

```text
C[last] = F(P[last])
C[i]    = F(P[i] XOR C[i + 1])
```

Decode walks forward and XORs each decoded block with the next ciphertext block.  The schedules are fixed and never negotiated, so this is only a historical compatibility transform.  It does not
authenticate the peer.

The receive path decodes only a nonempty multiple of 8.  It reads the final low nibble as a padding count and accepts values 0 through 8, although the sender never emits 0.

The source path counts an encrypted datagram which is not a multiple of 8, but continues with the undecoded bytes.  Connected CRC validation is only performed when at least 6 working bytes remain.

The framing code is in `crc.c`, `cypher.c`, `usend.c`, and the receive side of `rdp.c`.

## Independent sequence spaces

RDP has several unrelated counters:

| Space                    | Width | Advances on                                            | Purpose                                                                                |
| ------------------------ | ----: | ------------------------------------------------------ | -------------------------------------------------------------------------------------- |
| Packet sequence          |    16 | each successfully emitted connected datagram           | Duplicate/reorder filtering for every datagram, including ACK only and retransmission. |
| Reliable message ID      |    16 | each allocated reliable fragment or control message    | ACK identity, retransmission ownership, and the fixed reliable window.                 |
| Fragment ID              |    16 | each logical message that actually fragments           | Groups independently acknowledged fragment IDs for reassembly.                         |
| Reliable stream sequence |     8 | each reliable logical message sent on a nonzero stream | Per stream application delivery order.                                                 |
| Unreliable stream floor  |    16 | set to accepted packet sequence + 1                    | Drops old sequenced unreliable messages without waiting for gaps.                      |

A retransmission demonstrates the distinction:

```text
first send:    packet sequence 41, message ID 900
retransmit:    packet sequence 47, message ID 900
```

The receiver accepts packet sequence 47 as a new datagram, recognizes reliable ID 900 as a duplicate, does not deliver it again, and schedules another ACK for 900.

### Packet sequence receive window

The sender begins at packet sequence 0.  The receiver begins at `0xFFFF`, making the first sequence 0 one step forward.

Relative to the latest accepted packet sequence:

|      Signed distance | Treatment                                                              |
| -------------------: | ---------------------------------------------------------------------- |
|                  `0` | duplicate, discard                                                     |
|        `+1 .. +4096` | accept and make it the latest sequence                                 |
|          `-1 .. -64` | accept only if its bit is not already present in the 64 packet history |
|      less than `-64` | too old, discard                                                       |
| greater than `+4096` | too far in the future, discard                                         |

An accepted forward gap shifts the 64 bit history. A gap greater than 64 clears it. An accepted reordered packet sets its historical bit without moving the latest sequence.

After the packet sequence moves more than 16,000 positions beyond a separate stream reset value, all 20 sequenced unreliable floors are reset to `packet_sequence - 1`.  This brings them back near
the current packet sequence.  It does not clear reliable history.

PPC and TAKP Windows initialize all 64 artificial prehistory bits to 1.  EQMac Intel initializes the lower 32 bits to 1 and the upper 32 to 0.  Normal forward traffic quickly shifts this one
observed client divergence out of the window.

### Reliable message ID receive window

The first accepted reliable ID initializes the direction:

```text
initial_message_id = received_id
received_through   = received_id - 1
```

After initialization, the receiver accepts IDs 1 through 119 ahead of `received_through` and discards an ID exactly 120 or more ahead:

```text
int16(message_id - received_through - 120) >= 0  => discard
```

Old IDs are allowed to reach duplicate classification so a repeated reliable message can regenerate a lost ACK.

The receiver also has a 4,096 bit map beginning at `received_through + 1`.  When a reliable ID arrives, the receiver sets its bit and advances `received_through` across every consecutive set bit.  A
later ID can therefore be selectively ACKed while an earlier hole prevents the cumulative base from moving.

The 4,096 bit map is history capacity, not a 4,096 position wire receive window. The receiver only accepts the next 119 IDs beyond the cumulative base.

## Connection establishment

There is no separate 3 packet handshake like TCP.  The first reliable message establishes one direction of the connection and may carry application data:

```text
Initiator                                      Receiver
    |                                             |
    | SYN | MSGID=m | application/control bytes   |
    |-------------------------------------------->|
    |       create endpoint keyed connection      |
    |       record and possibly deliver m         |
    |                                             |
    |        ACKTHRU=m (possibly piggybacked)     |
    |<--------------------------------------------|
    | mark local SYN acknowledged                 |
```

An unknown endpoint is considered only when its datagram has `SYN`; other packets from an unknown endpoint are ignored.  The first reliable message in a direction receives `SYN` and is sent
immediately.  Every later reliable message waits until the first one has been acknowledged.  This matters because the first reliable ID received establishes the peer's receive base.

The ACK that sets `syn_acknowledged` does not have to name a separately remembered SYN ID.  It is the first ACK that covers newly acknowledged reliable data after SYN was sent.  With a correct peer,
the only reliable message that can be outstanding at this point is the first one.

Creating an outgoing handle does not send SYN by itself.  The direction remains unestablished until the application queues a reliable message or starts a clean close, whose FIN can be that first
reliable message.  Keepalive is not eligible until this direction has already sent SYN, so it cannot establish the direction.  Unreliable data sent before SYN cannot create the peer's connection.  It
stays in `ready_messages` until a reliable message establishes this direction and its ACK arrives.

Each direction has its own first reliable ID.  The first reliable response may therefore carry its own `SYN` even though both sides already have a connection object.

## Application send and fragmentation

The recovered public sender accepts a logical payload up to 51,200 bytes. Unreliable payloads cannot exceed 512 bytes. Reliable payloads are divided into 512 byte pieces:

```text
fragment_count = ceil(payload_bytes / 512)
```

Every reliable piece receives a consecutive message ID and its own outstanding history bit.  All pieces of one application message share a fragment ID.  The fragment ID advances after the complete
group has been queued.

For example, a 1,300 byte reliable ordered message becomes:

```text
ID 300: FRAGMENT id=12 index=0 count=3, stream=4 seq=9, 512 bytes
ID 301: FRAGMENT id=12 index=1 count=3,                 512 bytes
ID 302: FRAGMENT id=12 index=2 count=3,                 276 bytes
```

Each fragment can be ACKed or retransmitted independently.  The application does not receive the message until all 3 reliable IDs have arrived and the payload has been assembled.

A 0 byte reliable send succeeds without allocating an ID or emitting a datagram.  A 0 byte unreliable send creates an empty unreliable datagram, but normal receive handling does not create an
application arrival for an empty non FIN payload.

## Acknowledgement generation

### Receive side state

Every accepted `MSGID` packet, including a duplicate, updates the pending ACK range:

```text
unreported_count
unreported_min_message_id
unreported_max_message_id
```

It also arms a 50 ms delayed ACK.  Another reliable packet does not extend an already pending deadline.  Any outgoing packet before the deadline carries the ACK and clears the pending report.

ACK generation chooses between 2 bases:

```text
if oldest pending ID is normally within 8 of received_through:
    emit ACKTHRU(received_through)
else:
    emit MASKOFFSET(oldest pending ID)
```

The exact compiled comparison has an asymmetric sign extension at the `0x7FFF/0x8000` boundary. It is retained for compatibility; outside that edge, the description above is the useful model.

The mask spans the lowest through highest pending IDs, rounded up to whole bytes and limited to 15 bytes.  Its bits come from the 4,096 bit receive history.  Sending the report clears only the
pending count; it does not forget which IDs were received.

### ACKTHRU example

Assume the receiver is contiguous through ID 100. It receives 102 and 104 while 101 and 103 are absent:

```text
received_through = 100
history           0 1 0 1
ID                101 102 103 104
```

The oldest pending ID is close to the cumulative base, so the report is:

```text
ACKTHRU base=100 mask=0x50
                         ^ bit 1 acknowledges 102
                           bit 3 acknowledges 104
```

This means "all IDs through 100, plus 102 and 104." It does not say that 101 or 103 was lost permanently.

### MASKOFFSET example

If the cumulative base is 100 and the first new report is ID 110, followed by 112, the far offset form is:

```text
MASKOFFSET base=110 mask=0x40
```

The base selectively acknowledges 110.  Mask bit 1 acknowledges 112.  The cumulative base remains 100.

### ACK only timing

The delayed ACK target is 50 ms, but ACKs use the same socket and scheduler as everything else:

```text
reliable arrival at T
    |
    +-- arm one deadline at T + 50 ms
    |
    +-- earlier application send or retransmission
    |       `-- piggyback ACK and consume deadline
    |
    `-- no earlier send
            `-- scheduler emits an ACK only datagram when backend is ready
```

Deadline tests allow work up to 10 ms early.  Operating system scheduling can also make the packet later than 50 ms.  There is no second ACK delay constant.

The source faithful sender clears the ACK report before the socket send result is known.  If the send returns retryable result 5, later reliable traffic or a retransmission has to generate another
ACK.  The original code does not restore the report immediately.

The default build restores the report and its original delayed ACK deadline after result 5.  It keeps the original 100 ms worth of bandwidth pressure, so the scheduler waits for the backend pacing
deadline and then sends the same ACK on the next successful packet.  A packet which the backend accepted is unchanged.

## Applying acknowledgements

The sender keeps the same reliable IDs in 2 different forms:

- a 4,096 bit outstanding ID map relative to `acknowledged_through_message_id`; and
- a `sent_messages` queue containing reliable messages that have actually been sent and are waiting for an ACK.

An ID can be in the map while its message is still in `ready_messages` or `window_blocked_messages`.  A peer normally cannot ACK a message it has not seen, but the sender still has to distinguish allocated IDs from messages that have reached the wire.

The source faithful build checks the allocated ID range but does not check which queue owns the record.  It can accept an ACK for an outstanding record which is still ready or window blocked.  Recording that ACK clears the history bit and can also advance the cumulative base even though the record remains queued.

The default build checks the complete ACK report before changing any state.  Every newly claimed history bit must have a matching record in `sent_messages`.  An already clear bit is an ordinary repeated ACK.  If any cumulative or selective claim names a record which has not been sent, the complete report is discarded without retiring the other IDs in it.

Before changing state, the ACK handling checks:

1. exactly 0 or 1 ACK base form;
2. no mask without a base;
3. a set bit in the final encoded mask byte;
4. a highest claimed ID strictly before the next unallocated ID; and
5. a highest claim no more than 4,096 IDs behind the next unallocated ID; and
6. in the default build, a sent record for every newly claimed outstanding ID.

An invalid ACK shape aborts the connection.  A correctly formed ACK which names unavailable transmit state is discarded without changing anything.

For an advancing `ACKTHRU`, the sender:

1. visits every set outstanding bit between the old and new cumulative base;
2. removes matching records from `sent_messages` and frees them;
3. shifts the 4,096 bit outstanding map;
4. stores the new cumulative base;
5. moves newly eligible heads out of `window_blocked_messages`; and
6. applies optional selective mask bits relative to the new base.

For `MASKOFFSET`, the sender clears the bit for the selective base and then applies mask bits after it. Already clear bits make the ACK idempotent.

A selective ACK can free sent messages and stop their retransmission, but it does not move the cumulative base.  Selective ACK alone therefore cannot open more room in the 120 ID transmit window.

An ACK supplies an RTT sample only when the message was sent exactly once.  Retransmitted messages are not sampled because the sender cannot know which transmission the ACK answered.

## Fixed window and sender flow control

### The 3 different window limits

3 independent limits govern reliable sends:

| Limit                           |                               Value | What it controls                                                                                           |
| ------------------------------- | ----------------------------------: | ---------------------------------------------------------------------------------------------------------- |
| Transmit/receive forward window |                       120 positions | IDs at offset 120 or later wait at the sender and are rejected at the receiver; usable offsets are 1..119. |
| Outstanding reliability history |                          4,096 bits | Tracks allocated, unacknowledged IDs in the sent, ready, and blocked queues.                               |
| Send buffer byte limit          | configurable; initially 8,000 bytes | Limits serialized bytes held in the three transmit queues.                                                 |

The receiver does not advertise any of these limits.  Both peers know the fixed ID rule from the protocol.  Byte and rate limits are local settings.

A reliable message is window blocked when:

```text
int16(message_id - acknowledged_through - 120) >= 0
```

Thus, with cumulative ACK base 100, IDs 101 through 219 are in the window and ID 220 is blocked.

### Why cumulative ACK progress matters

Suppose IDs 101 through 219 were sent, 101 was lost, and 102 through 219 were selectively acknowledged. Their allocations can be retired, but:

```text
acknowledged_through remains 100
ID 220 remains window blocked
```

When 101 finally arrives, the receiver can advance through 219.  `ACKTHRU 219` moves the sender's base and makes IDs 220 through 338 eligible.  A missing reliable ID can therefore block all new IDs
even when every later message has already arrived.

### Send buffer byte limit

The sender adds the serialized bytes in `sent_messages`, `ready_messages`, and `window_blocked_messages`.  The source faithful check only rejects when the current total is already above the limit; it
does not include the new send.  One call can therefore cross the configured limit.  Send result 14 means the caller should keep its data and try again later.

The 4,096 ID limit is separate from bytes.  Many small reliable messages can run out of IDs while using little buffer space.  Send result 15 reports this case.  The default build rejects a fragmented
send before it can fill the complete ambiguous 4,096 ID span.

The default build also keeps the last representable history position available for FIN.  At a span of 4,095 outstanding IDs, keepalive is not scheduled and a direct keepalive request returns result
15 without changing the history.  FIN may use that final position.  If no position remains, its helper also returns result 15 without changing the connection.  ACK progress makes keepalive eligible
again.

### No congestion control feedback

ACKs only report receipt.  They do not report receiver capacity or a desired rate.  RDP has no congestion control feedback or advertised receive window.  It only has the local byte rate model and the
fixed message ID window.  A high configured rate permits larger bursts; it is not a measurement of the network.

## Transmit queues and how they drain

Each connection owns 3 intrusive FIFO queues:

```text
                         +---------------------------+
allocated reliable ---->| 120 ID window test         |
                         +-------------+-------------+
                                       |
                         out of window | in window
                                       |
           +---------------------------+-----------------------+
           v                                                   v
 window_blocked_messages                         SYN/pace/serial test
           |                                                   |
           | ACKTHRU advances base                 cannot send | can send
           |                                                   |
           `---------------------------> ready_messages        |
                                                   |           |
                                                   | scheduler | virgin send
                                                   v           v
                                               virgin send -> sent_messages
                                                                  |
                                                   ACK -----------+--> free
                                                   RTO -----------+--> resend,
                                                                       rotate tail
```

The first reliable message is the exception.  It gains `SYN` and is sent immediately.  No later reliable message is sent until it has been acknowledged.

After that:

1. an out of window reliable record enters `window_blocked_messages`;
2. an in window record enters `ready_messages` when SYN is unacknowledged, pacing is unavailable, serial is unavailable, or an older ready record already owns FIFO priority;
3. a first transmission stamps `first_sent_time_ms` and `last_sent_time_ms`, changes transmission count from 0 to 1, and places a reliable record in `sent_messages`; and
4. an unreliable record is freed immediately after its first send attempt.

When ACKTHRU opens the window, newly eligible blocked messages go through the normal send checks again.  They may send immediately or join `ready_messages`; ACK processing does not bypass pacing or
FIFO order.

When a new call arrives while `ready_messages` is nonempty and the backend can send, the new record is appended and the old head is sent. New traffic cannot jump the queued FIFO.

On retransmission, the due head is removed from `sent_messages`, given a new send time, and appended to the tail.  This keeps one missing ID from monopolizing the scheduler.  The source rechecks the
transmit window first; if that unusual check fails, it still updates the time and rotates the message without sending it.

Peer STOP, abort, or destruction flushes all 3 queues.

## Byte rate pacing

The sender maintains a virtual byte backlog:

```text
queued_bytes -= min(queued_bytes,
                    bytes_per_second * elapsed_ms / 1000)
```

On a successful UDP send it charges:

```text
4 byte base header
+ ACK bytes
+ serialized message fields and payload
+ 28 bytes of assumed IPv4/UDP overhead
```

The charge excludes the optional CRC and encryption padding expansion.  On a would block backend result, the model instead adds 100 ms worth of configured rate as pressure.

A message can be sent immediately only while:

```text
queued_bytes < bytes_per_second / 8
```

This permits a burst of roughly `rate / 8`.  Each fragment checks the backlog again, so a large application message does not use one stale decision for every piece.

Once a message enters `ready_messages`, its event is scheduled for the predicted time when the virtual backlog reaches 0, not when it merely drops below the `rate / 8` threshold.  This creates the
characteristic burst then wait pattern:

```text
application enqueue burst        owner scheduled drain
| packet packet packet ... |-----wait-----| one packet |-----wait-----
         until >= rate/8              backlog empty       backlog empty
```

The source initializes 3,000 bytes/s and an 8,000 byte send buffer.  A recovered application changes its outgoing connection to 5,120 bytes/s and 256 KiB.  These are local defaults, not part of the
wire protocol.  Applications should set their own values after connect or accept.

## Transmit scheduler and timing

Each connection places its earliest deadline in the endpoint event queue.  Transmit timing considers socket availability, the ready head, the oldest sent message, and a delayed ACK.  Each `tx_tx` call
does at most 1 piece of work.

### Scheduler priority

When selected, the scheduler proceeds as follows:

```text
1. backend more than 10 ms from ready?        return
2. ready head allowed before old sent head?   send one virgin; return
3. sent head at RTO?
       timeout policy exceeded?               abort; return
       otherwise                              retransmit/rotate one; return
4. delayed ACK pending?                       send ACK only; return
5. otherwise                                  no work
```

Ready first transmissions receive priority while the age of the sent queue head, measured from its latest send, is at most:

```text
UDP:    clamp(rtt_mean + 3 * rtt_deviation, 50, 65535) ms
serial: 10000 ms
```

Retransmission uses the smaller threshold:

```text
RTO = clamp(rtt_mean + 2 * rtt_deviation, 50, 65535) ms
```

The 2 thresholds are different.  New data can continue briefly beyond the RTO, but once the oldest sent message reaches the 3 deviation threshold, retransmission gets priority.

The due tests permit backend work and retransmission up to 10 ms early.

### Selecting the next event

If a ready record exists after SYN acknowledgement, backend availability is the transmit deadline. Otherwise, the earlier of delayed ACK and sent head RTO is chosen, then backend availability acts as
a lower bound:

```text
event_time = max(backend_available,
                 min(delayed_ack_deadline, retransmission_deadline))
```

Absent candidates are omitted from the minimum.

The source scheduler does not omit a ready record when it is an unreliable message waiting for the first reliable SYN.  With no sent message or delayed ACK to make progress, it repeatedly selects an
expired backend deadline and spins while holding the connection lock.  The default build treats that ready record as having no transmit deadline until a reliable message establishes the direction.
The queued unreliable message is then sent after the SYN is acknowledged.

Connection events also include keepalive, traceroute, and linger.  Equal deadlines keep this priority order: transmit, keepalive, traceroute, then linger.

After transmit, keepalive, or traceroute work, the dispatcher recalculates and continues while another event is already due.  It uses one fixed `now` value for that pass.  Linger expiry is returned to
the endpoint so it can remove the connection.  The outer loop waits at least 4 ms between immediately due passes to avoid spinning.

### RTT estimator

The estimator holds 64 weighted samples. It starts with a 500 ms seed. A generic inserted sample has:

```text
sample_ms = min(measured_ms, 65535)
weight    = max(1023 / transmission_count, 1)
```

The ACK path only samples a message sent once, so live samples have weight 1023 and retransmissions add no sample.  The integer mean and deviation feed the two thresholds above.  RTO is limited to 50
through 65,535 ms.

### Retransmission timing

`first_sent_time_ms` never changes. Every retry changes `last_sent_time_ms`, increments the 16 bit transmission count, obtains the current packet sequence, piggybacks any current ACK report, and
rotates the record to the sent tail.

This means:

- RTO scheduling measures time since the latest attempt;
- connection abort policy measures lifetime since the first attempt;
- an ACK can race a retry already selected by the I/O thread; and
- one retry immediately after an ACK may be a normal scheduling race.  More retries after the ACK has been processed indicate a real problem.

## Keepalive and failure detection

Keepalive is a per connection option.  It is not negotiated and does not change the peer's parser.  When enabled, a keepalive becomes eligible at:

```text
last_reliable_enqueue_time_ms + keepalive_interval_ms
```

only if:

- this transmit direction has already sent SYN on its first reliable message;
- local FIN has not been sent;
- the connection remains active; and
- peer STOP has not disabled transmission.

The normal build initializes the interval to 10,000 ms.  `rdplib_connection_enable_keepalive_with_interval` may replace it with a value from 1 through `INT32_MAX` while holding the connection lock,
then resorts the event queue.  Changing the interval does not reset `last_reliable_enqueue_time_ms`.  The source faithful build keeps the recovered `+ 10000u` expression and the interval API returns
`RDPLIB_ERROR_NOT_SUPPORTED`.

The SYN-sent gate tracks this direction only.  Receiving reliable traffic from the peer does not make local keepalive eligible, and SYN does not need to be acknowledged before keepalive scheduling
can begin.

Keepalive is a 0 byte `SYSTEM | MSGID` reliable message.  It consumes an ID, uses the normal queues and pacing, is retransmitted, and must be ACKed.  It does not establish a transmit direction or carry
its first SYN.  Receiving it updates reliable and ACK state but does not create an application message.

Timeout checks need an outstanding reliable message.  A completely idle connection has no sent message whose age can be tested.  Keepalive supplies one when there is no application traffic.

### Timeout policy

Before the first new ACK after SYN, both timeout limits are 10 seconds. That ACK changes them to:

```text
unacknowledged message timeout = 120 seconds
connection inactivity timeout =  30 seconds
```

For the due sent head:

```text
message_age = now - first_sent_time
receive_idle = now - last_accepted_packet_time

continue retransmitting only while:
    message_age < unacknowledged_timeout
and
    (message_age < inactivity_timeout
     or receive_idle < inactivity_timeout)
```

The first failed test reports an unacknowledged message timeout.  Inactivity is reported only when the outstanding message is old enough and no accepted packet has arrived during the inactivity
interval.

This is why the 30 second value alone cannot detect a silent idle peer.  The connection needs an outstanding reliable message, from application traffic or keepalive, before the scheduler can make the
timeout decision.

## Receive processing and application delivery

### Receive order

A UDP payload is processed in this order:

```text
decrypt/padding, if configured
    -> connected CRC validation, if applicable
    -> connectionless marker check
    -> endpoint lookup or SYN triggered creation
    -> flag shape validation
    -> packet sequence validation
    -> ACK validation
    -> message ID validation
    -> fragment and stream validation
    -> accepted arrival mutation
    -> fragment assembly
    -> stream/FIN ordering
    -> application producer queue
```

Accepted arrival mutation itself is ordered:

1. refresh last packet receive time;
2. process RESET and return immediately on abort;
3. process STOP before sequence or ACK mutation;
4. record packet sequence;
5. apply ACK fields;
6. record and classify reliable message ID;
7. update traffic counters; and
8. arm delayed ACK for every `MSGID`, including a duplicate.

This lets STOP take effect immediately even when FIN has to wait behind a missing reliable ID.  RESET returns before ACK or delivery state from the same packet can be applied.

### Accept, discard, abort

The parser has three dispositions:

| Value | Meaning | Effect                                         |
| ----: | ------- | ---------------------------------------------- |
|     0 | accept  | record state and consider delivery             |
|     1 | discard | ignore the datagram without disconnecting      |
|     2 | abort   | abort an active connection as a protocol error |

Duplicates, out of window packet sequences, reliable IDs at offset 120 or more, and impossible ACK history are discarded.  Invalid flag combinations, conflicting ACK forms, invalid streams, and bad
fragment structure abort the connection.

### Fragment reassembly

Fragments are grouped by fragment ID. Payload is copied at:

```text
fragment_index * 512
```

Fragment 0 supplies the message flags, base message ID, stream fields, and sender.  The final fragment determines the payload length.  Fragments may arrive out of order, but every unique reliable
fragment ID is required.  Once fragment 0 has initialized the group, every fragment must keep the same count and satisfy:

```text
fragment.message_id == base_message_id + fragment_index
```

The source faithful build accepts a final fragment with 0 bytes or more than 512 bytes even though the sender never creates one.  The default build accepts 1..512 bytes and checks every optional field before assembly.

### Delivery classes

Reliability and application ordering are independent:

| Wire form                  | Duplicate behavior              | Delivery behavior                                                                                                          |
| -------------------------- | ------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| no `MSGID`, no `SEQUENCED` | packet sequence filter only     | deliver immediately when nonempty                                                                                          |
| `MSGID`, no `SEQUENCED`    | reliable ID prevents redelivery | deliver immediately, even if earlier reliable IDs are missing                                                              |
| `SEQUENCED`, no `MSGID`    | packet sequence filter          | compare with the stream floor; accept at/ahead and set the floor to sequence+1; discard older packets; never wait for gaps |
| `SEQUENCED | MSGID`        | reliable ID prevents redelivery | sort by 8 bit stream sequence and deliver only the contiguous prefix                                                       |
| `SYSTEM | MSGID`           | reliable message bookkeeping    | do not deliver to the application                                                                                          |
| `FIN | MSGID`              | reliable message bookkeeping    | hold until the cumulative reliable base reaches the FIN ID, then deliver the final arrival                                 |

An ACK means that the transport received the reliable ID.  It does not mean the application has seen the message.  An ordered message may be ACKed while it waits for an earlier stream sequence, and a
fragment may be ACKed while the complete message is still missing pieces.

There are 20 reliable ordered stream queues and 20 sequenced unreliable floors.  Ordered stream sequences begin at 0 and wrap after 255.  Reliable streams wait for missing messages;
unreliable streams skip them.

### Application queue handoff

The I/O thread appends completed messages to a producer queue and signals when that queue changes from empty to nonempty.  The application drains its own queue.  When that queue becomes empty, it
takes the complete producer batch in FIFO order.

This is above the wire protocol, but it affects timing seen by the application.  ACK state is updated in the I/O thread before the application pops a message, and a whole batch may become visible after
several packets have already been ACKed.

## STOP, FIN, RESET, and linger

Shutdown is not represented by a single connection state.  `connected`, peer `transmit_stopped`, local `linger_active`, `fin_sent`, and `fin_ack_seen` are separate values and may overlap.

### Local graceful close

With a nonzero linger duration, local close:

1. removes queued application arrivals for the connection;
2. flushes receive owned fragment, stream, and saved FIN state;
3. starts or rearms the local linger deadline;
4. allocates a reliable `FIN | MSGID` if transmission is still permitted;
5. routes it through the normal window and pacing checks; and
6. leaves owner thread processing nonblocking.

Linger is already active when FIN is serialized, so the packet also carries STOP:

```text
Closer                                           Peer
  |                                                |
  | STOP | FIN | MSGID=f                           |
  |----------------------------------------------->|
  |                                                | process STOP immediately
  |                                                | ACK reliable ID f
  |                                                | deliver FIN in reliable order
  |                                                |
  | ACKTHRU f [possibly STOP after peer close]     |
  |<-----------------------------------------------|
  | close completion may become clean              |
  | remain discoverable until linger expires       |
```

Every packet sent during local linger gains STOP, not just FIN.  RESET takes precedence after an abort.

FIN acknowledgement or peer STOP can report a clean close, but neither ends linger early.  Linger controls how long the connection remains in the endpoint table.  At expiry the I/O thread removes it
even if close already completed successfully.

An immediate close with linger 0 removes membership without sending FIN.

### Receiving STOP and FIN

STOP immediately completes a local close waiter, flushes the outgoing queues, and sets `transmit_stopped`.  It does not disconnect the receive side or start local linger.

FIN in the same packet is still reliable input.  It is held until every reliable ID through the FIN is present, then delivered to the application.  An ACK already owed may still be sent after STOP even
though new application transmission is disabled.

When the application responds to peer FIN by starting local close, it usually does not send another FIN: peer STOP has already disabled transmission. The close is classified clean and starts linger so
duplicate and reordered close traffic can be absorbed.

This is not a TCP FIN exchange.  STOP controls transmission immediately, FIN is an ordered notification to the application, and linger controls local cleanup.

### RESET and transport abort

An accepted RESET aborts immediately and skips the remaining ACK and delivery fields in that datagram. Protocol error, send failure, timeout, and selected ICMP diagnostics enter the same
transport abort code with different reason codes. Abort:

- flushes all outgoing and receive owned transport queues;
- clears connected and sets transmit stopped;
- records a reason;
- fails a pending close operation; and
- causes a later application disconnect notification.

The application separately decides whether to remove its session immediately or keep it in a disconnected state.  RDP only reports the transport condition.

For ICMP, the recovered raw receiver identifies a connection from the source port and destination address and port quoted inside the diagnostic. The default host adapters can provide the same
destination directly from the UDP socket: Linux through `IP_RECVERR`, and Windows through an attributable `WSAECONNRESET`. Type 3/code 3 aborts the matching connection. Repeated reports after that
abort do not publish another disconnect in the default build.

## Default and source faithful builds

The default build adds checks and state corrections around these boundaries without changing normal wire output:

- UDP vector size and framing workspace;
- selected field parser bounds, fragment count, and final fragment size;
- application send stream, size, allocation, and projected history bounds;
- ACK state after a retryable backend send and reliable history space for keepalive and FIN;
- ACK retirement only after an outstanding record has entered the sent queue;
- connection hash allocation and family checks;
- unreliable scheduling before SYN, so a queued unreliable record cannot spin while it waits for a reliable message; and
- duplicate ICMP disconnect reporting when several diagnostics are already queued for one peer.

`RDPLIB_SOURCE_FAITHFUL=ON` builds the original versions and keeps their unchecked assumptions.  It uses the same function names.  There is no second set of alternate symbols.

The default build does not change normal ACKs, sequence windows, pacing, retransmission, timeouts, close framing, CRC, or cipher.  The clients see the same bytes during a normal exchange.

Important source faithful edge behavior includes:

- optional header fields are read before the final claimed length check;
- a final fragment of 0 or more than 512 bytes passes the source validator;
- the source send gate accepts stream values through 255 even though only 20 stream records exist;
- a multi fragment call can project beyond the 4,096 bit history;
- the byte buffer check uses current occupancy rather than reserving the new call;
- result 5 consumes the pending ACK report before the backend has accepted it;
- keepalive can consume the final reliable history position needed by FIN;
- an ACK can retire allocated state which has not entered the sent queue; and
- making the first outgoing message unreliable can spin the I/O thread until another application thread blocks trying to send the required reliable message.

These are hazards in the original implementation.  They are not extra packet forms that should be sent.

## Historical serial envelope

The optional serial backend carries the same connected RDP datagram inside a native endian frame:

```text
offset  bytes  field
0       18     "\n\rwho's yo daddy?" including terminating 0
18       2     sender/local port
20       2     destination/remote port
22       2     RDP payload byte count
24       2     one's complement checksum
26       n     RDP datagram
```

The checksum covers the last 8 header bytes and payload, with the checksum field set to 0 during calculation.  The receiver accepts at most 536 payload bytes and creates address family 69 from
the sender port.  Unlike UDP `usend`, serial framing does not add the optional CRC/cipher envelope; it uses this checksum instead.

The Windows clients contain a working asynchronous serial backend with at most 2 pending writes.  The Mac clients keep the parser and framing code, but their receive fill function only prints a
warning.  The POSIX build has no physical serial backend.  The game uses UDP; the serial code is included because it was part of the library.

## Decoder checklist

A packet analysis tool can decode connected UDP traffic with this procedure:

1. Select the endpoint's static unframed, CRC, or encrypted mode.
2. If encrypted, require a nonzero multiple of 8, decode, and remove the low nibble padding count.
3. Read the first network order word.  If it is `0xFFFF`, classify the remainder as connectionless and stop connected decoding.
4. If CRC framing is effective, remove the final 4 bytes and verify CRC 32.
5. Require the 4 byte connected base header and decode flags and packet sequence.
6. Read `2 + N` ACK bytes when either ACK form is set, where `N = (flags & 0x00F0) >> 4`.
7. Read message ID for `MSGID`.
8. Read the 6 byte tuple for `FRAGMENT`.
9. Read stream ID for `SEQUENCED`, plus stream sequence only for `SEQUENCED | MSGID`.
10. Treat the remainder as fragment or logical payload.
11. Track packet sequence and reliable message ID separately.
12. Interpret ACK mask bits MSB first from `base + 1`.

A useful trace table includes at least:

```text
time, direction, UDP endpoints, datagram bytes, flags, packet sequence,
ACK form, ACK base, ACK mask, message ID, fragment tuple,
stream ID, stream sequence, payload bytes, CRC result
```

For timing analysis, also derive:

```text
message first send time, latest send time, transmission count,
ACK coverage time, receive gap lifetime, current cumulative base,
next window blocked ID, and inter packet delay
```

A repeated message ID is normally a retransmission carried by a new packet sequence, not a duplicate UDP packet.  A clear ACK mask bit only means that the ID is not currently acknowledged; it is not a
loss declaration.

## Constants summary

| Property                             |                                           Value |
| ------------------------------------ | ----------------------------------------------: |
| Connected base header                |                                         4 bytes |
| Owner UDP receive workspace          |                                       536 bytes |
| ACK base                             |                                         2 bytes |
| Maximum ACK mask                     |                             15 bytes / 120 bits |
| Reliable message ID                  |                                         16 bits |
| Packet sequence                      |                                         16 bits |
| Packet backward duplicate history    |                                      64 packets |
| Maximum accepted packet forward jump |                                           4,096 |
| Reliable forward window boundary     |                  120 IDs; offsets 1..119 usable |
| Reliable outstanding history         |                                      4,096 bits |
| Fragment payload                     |                  512 bytes, except final 1..512 |
| Maximum reliable application payload |                    51,200 bytes / 100 fragments |
| Maximum unreliable payload           |                                       512 bytes |
| Stream count                         |                                              20 |
| Reliable stream sequence             |                                          8 bits |
| Delayed ACK                          |                            50 ms, non extending |
| Scheduler early tolerance            |                                           10 ms |
| Initial RTT seed                     |                                          500 ms |
| RTO                                  |       `clamp(mean + 2*deviation, 50, 65535)` ms |
| Ready priority age                   |       `clamp(mean + 3*deviation, 50, 65535)` ms |
| Keepalive interval                   |           10,000 ms after last reliable enqueue |
| Pre SYN ACK timeouts                 |         10,000 ms unacknowledged and inactivity |
| Established timeouts                 | 120,000 ms unacknowledged; 30,000 ms inactivity |
| Source bandwidth default             |                                   3,000 bytes/s |
| Source send buffer default           |                          8,000 serialized bytes |
| Bandwidth immediate threshold        |                             configured rate / 8 |
| Virtual IPv4/UDP charge              |                28 bytes per successful datagram |

## Implementation reading map

The source is the final authority:

| Subject                                              | Implementation                             |
| ---------------------------------------------------- | ------------------------------------------ |
| Flags and shared constants                           | `include/rdplib_constants.h`               |
| Optional message field serialization                 | `src/msg_outgoing.c`                       |
| CRC, encryption padding, UDP send                    | `src/crc.c`, `src/cypher.c`, `src/usend.c` |
| Receive and application routing                      | `src/rdp.c`                                |
| Parsing, close state, event selection                | `src/connection.c`                         |
| Packet/message receive windows and ACK construction  | `src/rx.c`                                 |
| Send checks, ACK application, retransmission, pacing | `src/tx.c`                                 |
| Byte rate model                                      | `src/bandwidth.c`                          |
| RTT estimator                                        | `src/timeout.c`                            |
| Fragment storage and metadata                        | `src/msg_arrival.c`                        |
| Serial envelope and receive parser                   | `src/serial.c`, `src/serial_rx.c`          |

Use [API.md](API.md) for the normal application interface.
