# A.E.T.H.E.R. IPFS-like P2P Module

A content-addressable, peer-to-peer file storage and sharing system implemented in C11, following the IPFS model with simplified Kademlia DHT.

## Tech Stack

- **Networking/Event Loop**: epoll (Linux) / kqueue (macOS/BSD) via socket APIs
- **Cryptography**: libsodium (BLAKE2b-256 for hashing, randombytes for node IDs)
- **HTTP Sidecar**: Mongoose embedded web server
- **Local Storage**: Raw file I/O with LMDB indexing

## Architecture

### Step 1: Data Chunking & Content Addressing

- **Chunking**: Files are split into 256 KB blocks
- **Hashing**: Each block is hashed using BLAKE2b-256 (faster than SHA-256)
- **Manifest**: A JSON manifest lists all block hashes in order
- **CID (Content Identifier)**: Merkle root of the manifest serves as the unique file identifier
- **Storage**: Blocks stored in `~/.my_ipfs/blocks/<first-2-chars>/<full-hash>`
- **Indexing**: LMDB database maps hashes to physical file paths

### Step 2: P2P Network (Simplified Kademlia over UDP)

- **Node Identity**: 256-bit random ID generated on first startup
- **UDP Socket**: Binds to port 4001 (configurable) for DHT communication
- **Routing Table**: K-buckets sorted by XOR distance
- **RPC Commands**:
  - `PING` / `PONG`: Liveness check
  - `FIND_NODE`: Find closest nodes to a target ID
  - `FIND_VALUE`: Find providers of a specific hash

### Step 3: Data Transfer Protocol (TCP)

- **TCP Socket**: Binds to port 4002 (configurable) for block transfers
- **Protocol**: Simple `REQUEST_BLOCK:<hash>\n` header followed by raw bytes
- **Verification**: Blocks are hashed on receipt to verify integrity

### Step 4: HTTP Sidecar

- **Server**: Mongoose embedded on `127.0.0.1:8080`
- **Endpoint**: `GET /ipfs/<CID>` returns the file content
- **Streaming**: Blocks are assembled and streamed to the browser in real-time

## Building

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install libsodium-dev liblmdb-dev

# macOS
brew install libsodium lmdb
```

### Build

```bash
cd cpp
make ipfs
```

Output: `bin/ipfs-node`

## Usage

### Start Daemon

```bash
./bin/ipfs-node daemon
# Or with custom ports:
./bin/ipfs-node daemon --port 8080 --udp-port 4001 --tcp-port 4002
```

### Add a File

```bash
./bin/ipfs-node add /path/to/file.mp4
# Output: Added file. CID: 7a834da63a4429ef...
```

### Get a File

```bash
./bin/ipfs-node get <CID> [output_path]
# Example:
./bin/ipfs-node get 7a834da63a4429ef... /tmp/output.mp4
```

### HTTP Gateway

Once the daemon is running, access files via browser:

```
http://localhost:8080/ipfs/<CID>
```

Chrome/Firefox will stream the content as if from a regular web server.

## Project Structure

```
cpp/
├── include/ipfs/
│   ├── chunk.h      # Chunking, hashing, Merkle tree
│   ├── storage.h    # Block storage, LMDB indexing
│   ├── network.h    # P2P networking, Kademlia DHT
│   ├── http.h       # HTTP sidecar (Mongoose)
│   └── node.h       # Main node API
├── src/ipfs/
│   ├── chunk.c      # Chunking implementation
│   ├── storage.c    # Storage with LMDB
│   ├── network.c    # UDP/TCP networking
│   ├── http.c       # HTTP server
│   ├── node.c       # Node orchestration
│   └── main.c       # CLI entry point
└── lib/
    ├── mongoose.c   # Embedded web server
    └── mongoose.h   # Mongoose header
```

## API Reference

### Node Lifecycle

```c
ipfs_node_config_t config;
ipfs_node_config_default(&config);
config.http_port = 8080;
config.udp_port = 4001;

ipfs_node_t* node;
ipfs_node_create(&node, &config);
ipfs_node_start(node);
ipfs_node_run(node);  // Blocking event loop
ipfs_node_stop(node);
ipfs_node_destroy(node);
```

### Add/Get Files

```c
uint8_t cid[IPFS_CID_SIZE];
ipfs_node_add(node, "/path/to/file", cid);

ipfs_node_get(node, cid, "/output/path");
```

## Testing

```bash
# Create test file
echo "Hello, IPFS!" > /tmp/test.txt

# Add file
./bin/ipfs-node add /tmp/test.txt
# CID: 7a834da63a4429ef...

# Retrieve file
./bin/ipfs-node get 7a834da63a4429ef... /tmp/retrieved.txt

# Verify
diff /tmp/test.txt /tmp/retrieved.txt
```

## Network Protocol

### UDP Packet Format

```
+--------+--------+--------+--------+
| Magic  |Version | RPC    |Reserved|
| 0x49504653 | 0x01 | Type   |        |
+--------+--------+--------+--------+
| Sender Node ID (32 bytes)          |
+------------------------------------+
| Payload Length (4 bytes)           |
+------------------------------------+
| Payload (variable)                 |
+------------------------------------+
```

### TCP Block Request

```
Client: REQUEST_BLOCK:<hex_hash>\n
Server: BLOCK:<size>\n
Server: <raw_bytes>...
```

## Limitations (MVP)

1. **No NAT traversal**: Works on local network or with port forwarding
2. **No encryption**: P2P traffic is unencrypted (production would use TLS)
3. **No garbage collection**: Blocks are never automatically deleted
4. **Single-threaded polling**: Production would use proper async I/O
5. **No provider records**: DHT doesn't persist provider information

## Future Enhancements

- [ ] UDP hole punching for NAT traversal
- [ ] TLS encryption for P2P connections
- [ ] Block replication and redundancy
- [ ] Garbage collection with pinning
- [ ] WebRTC transport for browser clients
- [ ] Pubsub for real-time updates
- [ ] IPNS-like naming system

## License

Part of the A.E.T.H.E.R. P2P Protocol project.
