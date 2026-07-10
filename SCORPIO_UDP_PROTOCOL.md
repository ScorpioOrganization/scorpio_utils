# ScorpioUdp Protocol

ScorpioUdp is a connection-oriented, multiplexed protocol built on top of raw UDP. It adds reliable and unreliable ordered streams, connection handshaking, heartbeat-driven keepalive, and selective retransmission - all within a 512-byte packet budget.

## Architecture

```mermaid
graph TD
    App["Application"]

    subgraph ScorpioUdp["ScorpioUdp (one per socket)"]
        RT["receiver_thread\nreads from OS socket"]
        ST["sender_thread\nwrites to OS socket"]
        PT["processing_thread\nroutes packets to connections"]
        RC["receiver_channel\n(16K capacity)"]
        SC["sender_channel\n(16K capacity)"]
        RT -->|raw bytes| RC
        RC --> PT
        PT -->|outbound bytes| SC
        SC --> ST
    end

    subgraph Conn["ScorpioUdpConnection (one per remote peer)"]
        CPT["processing_thread\nheartbeat + stream mux"]
        IP["incoming_packets channel\n(1M capacity)"]
        AS["awaiting_streams channel"]
        PT -->|parsed packet| IP
        IP --> CPT
        App -->|create_stream| AS
        AS --> CPT
    end

    subgraph Stream["ScorpioUdpStream (one per logical channel)"]
        RCH["receive channel\n(1M capacity)"]
        SH["_sent_history\nring buffer (reliable only)"]
        ORD["Orderer\nreassembly buffer"]
        CPT -->|STREAM_DATA| ORD
        ORD --> RCH
        App -->|send| SH
    end

    App -->|listen / connect| ScorpioUdp
    ScorpioUdp -->|new connection| Conn
    Conn -->|new stream| App
    RCH -->|receive| App
```

## Packet Format

Every packet starts with a single **command byte**, followed by optional header fields, and then the payload. All multi-byte integers are **big-endian** (network byte order).

### Command byte layout

```
 Bit:  7       6       5   4   3   2   1   0
       NOT_LAST FIRST  [reserved]  command (4 bits)
```

| Bits | Name | Meaning |
|------|------|---------|
| 3-0 | command | One of the 9 command codes |
| 6 | FIRST | Set on the first packet of a multi-packet message |
| 7 | NOT_LAST | Set on every packet except the last of a multi-packet message |

### Header fields (conditional)

After the command byte, header fields appear in this order when present:

| Field | Type | Present when |
|-------|------|--------------|
| StreamNumber | `uint16_t` | command is `STREAM_DATA` or `CLOSE_STREAM` |
| SeqNumber | `uint32_t` | command is `STREAM_DATA` or `CLOSE_STREAM` |
| FramesLeft | `uint16_t` | `NOT_LAST` flag is set (more packets follow) |
| StreamEpoch | `uint8_t` | command is `STREAM_DATA` on a **reliable** stream (right after FramesLeft if present, otherwise right after SeqNumber) |

`StreamEpoch` on `STREAM_DATA` is only present for reliable streams, and is written into **every** fragment of a multi-packet message (not just the first). Unreliable `STREAM_DATA` carries no epoch.

Everything after the header is payload (subcommand byte + data).

### Single-packet message (no fragmentation)

```
+----------+--------------+-----------+---------------------+----------+
| cmd byte | StreamNumber | SeqNumber | StreamEpoch (opt.)  | payload  |
| (1 byte) |  (2 bytes)   | (4 bytes) |      (1 byte)       | (N bytes)|
+----------+--------------+-----------+---------------------+----------+
  FIRST set, NOT_LAST clear, no FramesLeft
  StreamEpoch present only for reliable STREAM_DATA
```

### Multi-packet message (fragmented)

First packet:
```
+----------+--------------+-----------+------------+---------------------+------------------+
| cmd byte | StreamNumber | SeqNumber | FramesLeft | StreamEpoch (opt.)  |    payload       |
| (1 byte) |  (2 bytes)   | (4 bytes) | (2 bytes)  |      (1 byte)       | fills to 512 B   |
+----------+--------------+-----------+------------+---------------------+------------------+
  FIRST=1, NOT_LAST=1
  StreamEpoch present only for reliable STREAM_DATA
```

Middle packets:
```
+----------+--------------+-----------+------------+---------------------+------------------+
| cmd byte | StreamNumber | SeqNumber | FramesLeft | StreamEpoch (opt.)  |    payload       |
  FIRST=0, NOT_LAST=1, FramesLeft counts down
  StreamEpoch is written into every fragment, not just the first
```

Last packet:
```
+----------+--------------+-----------+---------------------+----------+
| cmd byte | StreamNumber | SeqNumber | StreamEpoch (opt.)  | payload  |
  FIRST=0, NOT_LAST=0, no FramesLeft
```

## Command Codes

| Value | Name | SeqNum | Stream | Connectionless | Description |
|-------|------|--------|--------|----------------|-------------|
| 0 | PING | - | - | yes | Connectivity probe |
| 1 | CONNECT | - | - | yes | Connection establishment |
| 2 | DISCONNECT | - | - | yes | Connection teardown |
| 3 | STATUS | - | - | - | Status query (reserved) |
| 4 | ERROR | - | - | - | Error notification (reserved) |
| 5 | HEARTBEAT | - | - | - | Keepalive + ACK carrier |
| 6 | CREATE_STREAM | - | - | - | Stream creation |
| 7 | CLOSE_STREAM | yes | yes | - | Stream closure |
| 8 | STREAM_DATA | yes | yes | - | Application data |

### Subcommands

Each command packet carries a 1-byte subcommand as the first payload byte. `CONNECT`, `DISCONNECT`, `CREATE_STREAM`, and `CLOSE_STREAM` packets additionally carry an 8-byte [connection id](#connection-identity) immediately after the subcommand byte (every subcommand of all four commands). `HEARTBEAT` also carries the connection id, right after the command byte (it has no subcommand). See the [Connection identity](#connection-identity), [Stream creation handshake](#stream-creation-handshake), [Stream closure](#stream-closure), and [HEARTBEAT packet payload](#heartbeat-packet-payload) sections for the exact layout of each.

**PING subcommands:**

| Value | Name | Direction |
|-------|------|-----------|
| 0 | PING | initiator -> peer |
| 1 | PONG | peer -> initiator |

> PING is only partially implemented; treat it as experimental.

**CONNECT subcommands:**

| Value | Name | Direction |
|-------|------|-----------|
| 0 | CONNECT | initiator -> acceptor |
| 1 | ACCEPTED | acceptor -> initiator |
| 2 | REJECTED | acceptor -> initiator |
| 3 | ALREADY_CONNECTED | acceptor -> initiator |

**DISCONNECT subcommands:**

| Value | Name | Direction |
|-------|------|-----------|
| 0 | DISCONNECT | either -> either |
| 1 | ACCEPTED | responder -> initiator |
| 2 | REJECTED | responder -> initiator |
| 3 | ALREADY_DISCONNECTED | responder -> initiator |

Both CONNECT and DISCONNECT packets lay out their payload as the subcommand byte followed by the 8-byte connection id:

```
+------+---------------+-----------------------------+
| cmd  | subcommand    | connection id               |
| 1 B  |   1 B         |   8 B (uint64, big-endian)  |
+------+---------------+-----------------------------+
```

The responder always echoes back the connection id it received, unchanged. See [Connection identity](#connection-identity).

**CREATE_STREAM subcommands:**

| Value | Name | Direction |
|-------|------|-----------|
| 0 | CREATE | initiator -> acceptor |
| 1 | ACCEPT | acceptor -> initiator |
| 2 | REJECT | acceptor -> initiator |
| 3 | REJECT_SIMILAR_EXISTED | acceptor -> initiator (same ID, different QoS) |
| 4 | ALREADY_EXISTS | acceptor -> initiator (same ID, same QoS) |

Every `CREATE_STREAM` subcommand carries the same fixed prefix, followed by subcommand-specific fields:

```
+------+---------------+--------------+-------+-----...
| cmd  | subcommand    | connection id| Stream| Stream
| 1 B  |   1 B         |  8 B (u64)   |Number | Epoch  ...
+------+---------------+--------------+-------+-----...
                                        2 B     1 B
```

`StreamNumber` and `StreamEpoch` follow the connection id on **every** subcommand (`CREATE`, `ACCEPT`, `REJECT`, `REJECT_SIMILAR_EXISTED`, `ALREADY_EXISTS`). Only `CREATE` (and the `ACCEPT` echo) additionally append the QoS bytes after that (see [Stream creation handshake](#stream-creation-handshake) for the QoS layout). The connection id is validated against the local connection; a mismatch causes the packet to be **silently dropped** (no response), same as HEARTBEAT.

**CLOSE_STREAM subcommands:**

| Value | Name | Direction |
|-------|------|-----------|
| 0 | CLOSE | initiator -> acceptor |
| 1 | CLOSED | acceptor -> initiator |
| 2 | ALREADY_CLOSED | acceptor -> initiator |

Every `CLOSE_STREAM` packet, regardless of subcommand, has this fixed layout:

```
+------+------------+----------------+--------------+-------------+
| cmd  | subcommand | connection id  | StreamNumber | StreamEpoch |
| 1 B  |    1 B     |  8 B (u64)     |    2 B       |    1 B      |
+------+------------+----------------+--------------+-------------+
```

A connection id mismatch causes the packet to be **silently dropped** (no response). A stream number/epoch that doesn't match a live stream on the receiver is treated as already-closed (`ALREADY_CLOSED` response) rather than dropped.

## Connection Lifecycle

### State machine

```mermaid
stateDiagram-v2
    [*] --> NEW
    NEW --> CONNECTING : connect() called / CONNECT packet sent
    CONNECTING --> CONNECTED : ACCEPTED received
    CONNECTING --> REJECTED : REJECTED received
    CONNECTED --> CLOSED : close() / DISCONNECT exchanged
    CONNECTED --> ERROR : panic()
    CONNECTING --> ERROR : panic()
```

### Connection handshake

```mermaid
sequenceDiagram
    participant C as Client
    participant S as Server

    Note over C: state = NEW
    C->>S: CONNECT { CONNECT }
    Note over C: state = CONNECTING

    loop Every 50ms while CONNECTING
        C->>S: CONNECT { CONNECT }
    end

    alt Server accepts (auto_accept = true)
        S->>C: CONNECT { ACCEPTED }
        Note over C: state = CONNECTED
        Note over S: new connection added
    else Server busy / rejects
        S->>C: CONNECT { REJECTED }
        Note over C: state = REJECTED
    else Already connected
        S->>C: CONNECT { ALREADY_CONNECTED }
    end
```

### Disconnection

```mermaid
sequenceDiagram
    participant A as Peer A
    participant B as Peer B

    A->>B: DISCONNECT { DISCONNECT }
    Note over A: closes all streams, state = CLOSED

    alt B has active connection
        B->>A: DISCONNECT { ACCEPTED }
        Note over B: state = CLOSED
    else B has no such connection
        B->>A: DISCONNECT { ALREADY_DISCONNECTED }
    end
```

### Connection identity

Every connection carries a **connection id**: a `uint64_t` (`ConnectionId`) chosen at random by the initiator when `connect()` is called. It is sent in every CONNECT and DISCONNECT packet (all subcommands) and the acceptor stores and echoes it back unchanged.

The connection id is **not** a demultiplexing key - a connection is still identified solely by its remote `ip:port`, and there is at most one connection per peer address. The id exists only as a guard against a peer that dies and restarts so fast that the other side never noticed the old connection went stale.

How the id is used:

- **CONNECT for an existing connection.** If the stored id matches, the acceptor replies `ALREADY_CONNECTED`. If it differs (a restarted peer reusing the same address with a fresh id), the acceptor **soft-panics its own stale connection and sends nothing back**. The old connection drops to `ERROR` (no longer alive) and is evicted on the next lookup; the initiator keeps retrying CONNECT every heartbeat and gets accepted once the stale entry is gone.
- **ACCEPTED / REJECTED / ALREADY_CONNECTED responses.** Ignored if the id does not match the local connection (`ACCEPTED` with a mismatched id also draws an `ERROR` reply). This prevents a late reply meant for a previous incarnation from advancing the current connection's state.
- **DISCONNECT.** Accepted (replied `ACCEPTED`, connection closed) only when the id matches a live connection for that address; otherwise the responder replies `ALREADY_DISCONNECTED`. A late DISCONNECT response addressed to a previous incarnation is therefore ignored and cannot tear down a freshly re-established connection.
- **CREATE_STREAM / CLOSE_STREAM / HEARTBEAT.** All three also carry the connection id (see [Stream creation handshake](#stream-creation-handshake), [Stream closure](#stream-closure), [HEARTBEAT packet payload](#heartbeat-packet-payload)). Unlike CONNECT/DISCONNECT, a mismatch here is **not** answered at all - the whole packet is silently dropped, since it means the peer's view of the connection is already stale.

## Stream Lifecycle

### State machine

```mermaid
stateDiagram-v2
    [*] --> NEW
    NEW --> CREATING : create_stream() / CREATE sent
    CREATING --> CREATED : ACCEPT received / connected()
    CREATING --> REJECTED : REJECT received
    CREATED --> CLOSING : close() / CLOSE sent
    CLOSING --> CLOSED : CLOSED or ALREADY_CLOSED received
    CREATING --> ERROR : panic()
    CREATED --> ERROR : panic()
    CLOSING --> ERROR : panic()
    CLOSED --> [*]
```

### Stream creation handshake

A CREATE_STREAM `CREATE` packet carries the connection id, stream number, stream epoch, and QoS inline after the subcommand byte (see [Subcommands](#subcommands) for the full per-subcommand layout):

```
+------+----------+---------------+--------------+-------------+-------------+------------------+
| cmd  | CREATE=0 | connection id | StreamNumber | StreamEpoch | reliability | depth (optional) |
| 1 B  |   1 B    |  8 B (u64)    |    2 B       |    1 B      |    1 B      |      2 B         |
+------+----------+---------------+--------------+-------------+-------------+------------------+
  depth only present when reliability > UNRELIABLE_LATEST_ONLY
```

`StreamEpoch` is a `uint8_t` chosen for the stream slot: initiators seed it randomly per stream number when the connection is created and increment it on every local `create_stream()` call; acceptors adopt whatever value the initiator sent in `CREATE`. A block/packet whose epoch doesn't match the receiver's live value for that stream number is treated as belonging to a stale, previous incarnation of the stream (see [HEARTBEAT packet payload](#heartbeat-packet-payload)).

```mermaid
sequenceDiagram
    participant A as Initiator
    participant B as Acceptor

    A->>B: CREATE_STREAM { CREATE, stream_id, QoS }
    Note over A: state = CREATING
    Note over A: retries every heartbeat period

    alt B accepts (auto_accept_stream = true, stream_id free)
        B->>A: CREATE_STREAM { ACCEPT, stream_id, QoS }
        Note over A: state = CREATED
        Note over B: state = CREATED, stream pushed to new_streams
    else B rejects
        B->>A: CREATE_STREAM { REJECT, stream_id, QoS }
        Note over A: state = REJECTED
    else Stream already exists with same QoS
        B->>A: CREATE_STREAM { ALREADY_EXISTS, stream_id }
        Note over A: stays CREATING (idempotent)
    else Stream exists with different QoS
        B->>A: CREATE_STREAM { REJECT_SIMILAR_EXISTED, stream_id }
        Note over B: existing stream panicked
    end
```

### Stream closure

Every CLOSE_STREAM packet carries `connection id`, `StreamNumber`, and `StreamEpoch` after the subcommand byte (see [Subcommands](#subcommands) for the exact layout).

```mermaid
sequenceDiagram
    participant A as Closer
    participant B as Peer

    A->>B: CLOSE_STREAM { CLOSE, connection_id, stream_id, epoch }
    Note over A: state = CLOSING
    Note over A: retries close every heartbeat

    alt B stream is alive with matching epoch
        B->>A: CLOSE_STREAM { CLOSED, connection_id, stream_id, epoch }
        Note over A: state = CLOSED
        Note over B: state = CLOSED
    else B stream already gone or epoch mismatch
        B->>A: CLOSE_STREAM { ALREADY_CLOSED, connection_id, stream_id, epoch }
        Note over A: state = CLOSED
    end
```

## Stream QoS

| Mode | Value | Description | Ordering | Retransmit |
|------|-------|-------------|----------|------------|
| UNRELIABLE | 0 | Best-effort, no ordering | No | No |
| UNRELIABLE_LATEST_ONLY | 1 | Only newest kept | No | No |
| RELIABLE_UNORDERED | 2 | All delivered, any order | No | Yes |
| RELIABLE_ORDERED | 3 | All delivered, in order | Yes | Yes |

Only `UNRELIABLE` and `RELIABLE_ORDERED` are currently supported. The `depth` field in QoS specifies the retransmit history size for reliable streams (0 means 65536).

## Reliable Delivery

Reliable streams use a **NACK-based retransmission** mechanism piggybacked onto the periodic HEARTBEAT packet.

### How it works

1. **Sender** stores every sent packet in `_sent_history`, a ring buffer of size `depth + SCU_UDP_QOS_DEPTH_SAFETY_BUFFER`.
2. **Receiver** buffers incoming packets in an `Orderer` and delivers them in order. It tracks which sequence number ranges it has received.
3. **Every heartbeat (50ms)**, the receiver encodes its held ranges into a HEARTBEAT packet.
4. **Sender** reads the HEARTBEAT, finds gaps between the reported ranges, and resends missing packets from `_sent_history`.

### HEARTBEAT packet payload

A single HEARTBEAT packet carries ACK/NACK state for one or more reliable streams. The command byte appears **once** and is immediately followed by the 8-byte [connection id](#connection-identity). Each reliable stream then contributes one block, packed back-to-back until the 512-byte packet is full.

```
+----------+---------------+     +--------------+-------+-------+------+-------+------+-------+------+----
|  cmd     | connection id |     | StreamNumber | epoch | count | end0 | beg1  | end1 | beg2  | end2 | ...
|  1 B     |     8 B        |     |    2 B       |  1 B  |  1 B  |  4 B |  4 B  |  4 B |  4 B  |  4 B |
+----------+---------------+     +--------------+-------+-------+------+-------+------+-------+------+----
                                 \___________________ repeated per reliable stream ___________________/
```

- `connection id` = the connection's `uint64_t` id. The receiver **drops the entire heartbeat** if it does not match the local connection (guards against a stale connection incarnation, same as CONNECT/CREATE_STREAM/...).
- `epoch` = the stream's `StreamEpoch` (`uint8_t`). A block whose epoch does not match the live stream is treated as an **unknown stream**: the receiver replies `CLOSE_STREAM { ALREADY_CLOSED }` and skips the block.
- `count` = number of gap ranges for this stream (`count + 1` ranges reported total)
- `end0` = next expected sequence number (one past the last contiguously received packet)
- Each following `(begin, end)` pair describes a held range that sits above a gap

The sender reads each stream block in turn and resends every sequence number in `[end_i, begin_{i+1})` that is still within its history window.

### Stuck-resend self-heal

If a requested sequence number has already fallen out of `_sent_history` (evicted by the ring buffer), the sender cannot honor the resend and just logs a warning - this can otherwise loop forever on a reliable-ordered stream. If the peer keeps requesting the **same** out-of-history sequence number continuously for `SCU_UDP_TIMEOUT`, the sender panics its own stream (forcing the peer to eventually rebuild it via a fresh CREATE_STREAM) instead of getting stuck retrying indefinitely.

### Sequence number wrapping

`SeqNumber` is `uint32_t` and wraps freely. A `_sequence_complement` counter (`uint32_t`) extends it to a monotonic `size_t` for history indexing and distance comparisons. Wrap detection uses the rule: if the delta exceeds half the `uint32_t` range, a wrap occurred.

## Fragmentation and Reassembly

Large messages are split automatically:

- A single-packet message carries no `FramesLeft` field (`FIRST` set, `NOT_LAST` clear)
- When fragmented, every packet except the last carries `FramesLeft` (counting down); the first fragment additionally sets the `FIRST` flag
- The last packet clears `NOT_LAST` and omits `FramesLeft`
- Usable payload is `512 - header_size`; fragmented packets lose a further 2 bytes to the `FramesLeft` field

On the receive side:
- **Reliable streams**: the `Orderer` reassembles fragments in sequence order before delivering
- **Unreliable streams**: fragments are held in `received_frames` keyed by sequence number; a complete message is assembled once all frames between a `FIRST` frame and the last frame are present. Incomplete sets expire after 500ms

## Keepalive and Timeout

- A HEARTBEAT is sent every **50ms** from each connection's processing thread.
- If no packet (of any kind) is received for **5 seconds** (`SCU_UDP_TIMEOUT`), the connection panics and closes.
- Each **reliable** stream additionally tracks its own last-heartbeat-ACK time independently of the connection: if `SCU_UDP_TIMEOUT` elapses without a heartbeat response covering that stream, only that stream panics (`State::ERROR`) - the connection and its other streams are unaffected.
- The HEARTBEAT serves double duty: keepalive signal and reliable-stream ACK/NACK carrier.

## Protocol Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `SCU_UDP_MAX_PACKET_SIZE` | 512 bytes | Maximum UDP payload |
| `SCU_UDP_HEARTBEAT_PERIOD` | 50 ms | Heartbeat interval |
| `SCU_UDP_TIMEOUT` | 5 s | No-packet timeout before disconnect |
| `SCU_UDP_CREATE_RETRY_PERIOD` | 5 s | Stream creation give-up timeout (CREATE retried every heartbeat until this elapses) |
| `SCU_UDP_UNRELIABLE_DATA_EXPIRY_NS` | 500 ms | Expiry for incomplete unreliable fragments |
| `SCU_UDP_QOS_DEPTH_SAFETY_BUFFER` | 4096 | Extra history slots beyond QoS depth |
