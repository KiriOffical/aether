# A.E.T.H.E.R. P2P Protocol Specification

## 1. Overview

The A.E.T.H.E.R. P2P protocol provides the foundation for decentralized communication between nodes. This specification defines the wire protocol, message formats, connection lifecycle, and discovery mechanisms.

## 2. Transport Layer

### 2.1 Connection Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Default Port | 7821 | TCP/UDP listening port |
| Protocol ID | `/aether/1.0.0` | Libp2p-style protocol identifier |
| Max Connections | 10,000+ | Via kernel-bypass I/O |
| Message Max Size | 64 MB | Maximum framed message size |
| Handshake Timeout | 10 seconds | Max time for handshake completion |
| Keepalive Interval | 30 seconds | Ping interval for connection health |

### 2.2 Wire Format

All messages use a length-prefixed binary format:

```
+----------------+------------------+
| Length (4B)    | Payload (N bytes)|
+----------------+------------------+
| Big-endian     | Protocol Buffers |
| uint32         | encoded message  |
+----------------+------------------+
```

**Frame Structure:**
- **Length**: 4-byte big-endian unsigned integer (max 67,108,864)
- **Payload**: Protocol Buffers encoded message

## 3. Message Types

### 3.1 Handshake Messages

#### Hello (Outbound)
Initiates connection with peer.

```protobuf
message Hello {
  uint32 version = 1;
  bytes node_id = 2;           // 32-byte Ed25519 public key
  uint64 timestamp = 3;        // Unix timestamp (microseconds)
  repeated string protocols = 4;  // Supported protocols
  Endpoint listen_addr = 5;    // Advertised listening address
  bytes signature = 6;         // Sign(node_id || timestamp)
}

message Endpoint {
  enum Protocol {
    IP4 = 0;
    IP6 = 1;
  }
  Protocol proto = 1;
  bytes address = 2;           // 4 or 16 bytes
  uint32 port = 3;
}
```

#### HelloAck (Inbound)
Response to Hello, completes handshake.

```protobuf
message HelloAck {
  bytes node_id = 1;
  uint64 timestamp = 2;
  repeated string protocols = 3;
  Endpoint listen_addr = 4;
  bytes signature = 5;
  bytes challenge = 6;         // Random bytes for proof-of-work
}
```

### 3.2 DHT Messages

#### FindNode
Request to find nodes closest to a target key.

```protobuf
message FindNode {
  bytes target_id = 1;         // 32-byte node ID being searched
  uint32 k = 2;                // Number of results desired (default: 20)
}
```

#### FindNodeResponse
Response with closest known nodes.

```protobuf
message FindNodeResponse {
  repeated NodeInfo nodes = 1;
  
  message NodeInfo {
    bytes node_id = 1;
    Endpoint endpoint = 2;
    uint32 distance = 3;       // XOR distance to target
  }
}
```

#### StoreValue
Store a key-value pair in the DHT.

```protobuf
message StoreValue {
  bytes key = 1;               // SHA-256 hash (32 bytes)
  bytes value = 2;
  uint64 expiration = 3;       // Unix timestamp
  bytes signature = 4;
}
```

#### GetValue
Retrieve a value by key.

```protobuf
message GetValue {
  bytes key = 1;
}
```

#### GetValueResponse
Response with stored value or not-found.

```protobuf
message GetValueResponse {
  oneof result {
    bytes value = 1;
    NotFound not_found = 2;
  }
  
  message NotFound {
    repeated NodeInfo closer_nodes = 1;
  }
}
```

### 3.3 Data Transfer Messages

#### FragmentRequest
Request a data fragment by hash.

```protobuf
message FragmentRequest {
  bytes fragment_id = 1;       // SHA-256 of fragment
  uint32 fragment_index = 2;   // Index in erasure-coded set
  bytes file_id = 3;           // Original file identifier
}
```

#### FragmentResponse
Data fragment or proof of non-existence.

```protobuf
message FragmentResponse {
  oneof result {
    FragmentData data = 1;
    NotFound not_found = 2;
  }
  
  message FragmentData {
    bytes fragment_id = 1;
    bytes payload = 2;
    bytes proof = 3;           // Merkle proof
  }
  
  message NotFound {
    string reason = 1;
  }
}
```

### 3.4 Maintenance Messages

#### Ping
Liveness check.

```protobuf
message Ping {
  uint64 sequence = 1;
}
```

#### Pong
Ping response.

```protobuf
message Pong {
  uint64 sequence = 1;
  uint64 latency_ns = 2;       // Nanosecond response time
}
```

#### Disconnect
Graceful connection termination.

```protobuf
message Disconnect {
  enum Reason {
    SHUTDOWN = 0;
    PROTOCOL_ERROR = 1;
    TIMEOUT = 2;
    MAINTENANCE = 3;
    BLACKLISTED = 4;
  }
  Reason reason = 1;
  string message = 2;
}
```

## 4. Connection Lifecycle

### 4.1 Handshake Flow

```
Initiator                           Responder
    |                                   |
    |-------- Hello (with sig) -------->|
    |                                   |
    |<------- HelloAck (with sig) ------|
    |                                   |
    |---- Verify signature & timestamp --|
    |                                   |
    |<--- Connection established ------> |
```

**Handshake Rules:**
1. Timestamp must be within ±5 minutes of receiver's clock
2. Signature must be valid Ed25519 signature of `(node_id || timestamp)`
3. Node ID must not be in local blacklist
4. Protocol version must be compatible

### 4.2 Connection States

```
┌─────────┐     Hello      ┌─────────────┐   HelloAck   ┌──────────────┐
│ CLOSED  │ ──────────────>│ AWAITING_ACK│ ────────────>│ CONNECTED    │
└─────────┘                └─────────────┘              └──────────────┘
    ↑                            │                            │
    │                            │ Timeout                    │ Disconnect
    │                            ▼                            ▼
    │                      ┌─────────────┐              ┌──────────────┐
    └──────────────────────│ CLOSING     │<─────────────│ CLOSING      │
                           └─────────────┘              └──────────────┘
```

## 5. Peer Discovery

### 5.1 Bootstrap Nodes

Initial seed nodes for network entry:

```
bootstrap.aether.network:7821
archive.org.aether.network:7821
myrient.org.aether.network:7821
```

### 5.2 Peer Exchange (PEX)

Nodes periodically exchange known peer lists:

```protobuf
message PeerExchange {
  repeated Endpoint peers = 1;
  uint64 timestamp = 2;
}
```

**PEX Rules:**
- Sent every 60 seconds to connected peers
- Maximum 50 peers per message
- Only share peers connected within last 24 hours

## 6. Security Considerations

### 6.1 Node Identity

- Each node generates Ed25519 keypair on first run
- Node ID = SHA-256(public_key)
- Private key stored in secure enclave when available

### 6.2 Message Authentication

- All handshake messages signed with node's private key
- Application-layer messages may include optional signatures
- Replay protection via timestamp validation

### 6.3 Rate Limiting

| Message Type | Limit | Window |
|--------------|-------|--------|
| Hello | 10/second | Per IP |
| FindNode | 100/second | Per node |
| FragmentRequest | 1000/second | Per node |
| StoreValue | 10/second | Per node |

### 6.4 Blacklist Management

Nodes maintain local blacklist for:
- Invalid signatures
- Protocol violations
- Excessive failed requests
- Guardian-issued bans (Phoenix Protocol)

## 7. Error Codes

```protobuf
enum ErrorCode {
  OK = 0;
  INVALID_FORMAT = 1;
  INVALID_SIGNATURE = 2;
  VERSION_MISMATCH = 3;
  NOT_FOUND = 4;
  RATE_LIMITED = 5;
  INTERNAL_ERROR = 6;
  BLACKLISTED = 7;
  PROTOCOL_ERROR = 8;
}
```

## 8. Protocol Buffers Schema

Complete schema file: `proto/aether.proto`
