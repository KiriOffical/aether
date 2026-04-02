# A.E.T.H.E.R. IPFS Module - Cross-Platform Compatibility

## Overview

The A.E.T.H.E.R. project now includes fully compatible IPFS-like implementations in:
- **C11** (Linux native)
- **C11** (Windows via MinGW)
- **Python 3.8+** (Cross-platform)

All three implementations can communicate with each other on the same P2P network.

## Protocol Compatibility

| Feature | C Linux | C Windows | Python |
|---------|---------|-----------|--------|
| UDP Port | 4001 | 4001 | 4001 |
| TCP Port | 4002 | 4002 | 4002 |
| HTTP Port | 8080 | 8080 | 8080 |
| MAGIC Number | 0x49504653 | 0x49504653 | 0x49504653 |
| Protocol Version | 1 | 1 | 1 |
| Chunk Size | 256 KB | 256 KB | 256 KB |
| Hash Algorithm | BLAKE2b-256 | BLAKE2b-256 | BLAKE2b-256 |
| Node ID Size | 256-bit | 256-bit | 256-bit |
| K-Bucket Size | 20 | 20 | 20 |

## Network Protocol

### UDP Packet Format (DHT)
```
+--------+--------+--------+--------+
| Magic (0x49504653)                |
+--------+--------+--------+--------+
|Version |RPC Type|Reserved         |
+--------+--------+--------+--------+
| Sender Node ID (32 bytes)         |
+--------+--------+--------+--------+
| Payload Length (4 bytes)          |
+--------+--------+--------+--------+
| Payload (variable)                |
+--------+--------+--------+--------+
```

### RPC Commands
| Type | Name | Purpose |
|------|------|---------|
| 1 | PING | Node liveness check |
| 2 | PONG | Liveness response |
| 3 | FIND_NODE | Find closest nodes to ID |
| 4 | FIND_VALUE | Find block providers |
| 5 | PROVIDE | Announce block availability |
| 6 | GET_BLOCK | TCP block request |

### TCP Block Transfer
```
Client: REQUEST_BLOCK:<hash_hex>\n
Server: BLOCK:<size>\n<data>
```

## Storage Compatibility

### Block Storage
Both implementations store blocks in the same sharded format:
```
~/.my_ipfs/blocks/<2-char-prefix>/<full-hash>
```

### Manifest Format (JSON)
```json
{
    "cid": "<hex-encoded-merkle-root>",
    "filename": "example.txt",
    "total_size": 1048576,
    "block_count": 4,
    "block_size": 262144,
    "blocks": [
        {"index": 0, "hash": "<hex>", "size": 262144},
        {"index": 1, "hash": "<hex>", "size": 262144}
    ]
}
```

## Usage Examples

### Start Nodes on Same Network

**Terminal 1 - C Node:**
```bash
cd /workspaces/aether/cpp
./bin/ipfs-node daemon --port 8080
```

**Terminal 2 - Python Node:**
```bash
cd /workspaces/aether/python
python -m aether.ipfs daemon --port 8081
```

**Terminal 3 - Windows (via Wine or native):**
```cmd
ipfs-node.exe daemon --port 8082
```

### Cross-Platform File Sharing

**1. Add file from Python:**
```bash
python -m aether.ipfs add myfile.txt
# Output: CID: 7a834da63a4429ef...
```

**2. Get file from C:**
```bash
./bin/ipfs-node get 7a834da63a4429ef... output.txt
```

**3. Access via HTTP (any node):**
```bash
curl http://localhost:8080/ipfs/7a834da63a4429ef...
```

### Connect Python to C Node

```bash
# Python connects to existing C node
python -m aether.ipfs connect 192.168.1.100 --udp-port 4001 --tcp-port 4002
```

## Testing Cross-Compatibility

```bash
# 1. Start C daemon
./cpp/bin/ipfs-node daemon &

# 2. Add file from Python
python -m aether.ipfs add test.txt

# 3. Get file from C (should fetch via P2P if not local)
./cpp/bin/ipfs-node get <CID> output.txt

# 4. Verify
diff test.txt output.txt
```

## Architecture

```
                    ┌─────────────────┐
                    │   P2P Network   │
                    │  UDP:4001/TCP   │
                    └────────┬────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
┌───────▼────────┐  ┌───────▼────────┐  ┌───────▼────────┐
│   C (Linux)    │  │ C (Windows)    │  │    Python      │
│  ipfs-node     │  │ ipfs-node.exe  │  │  aether.ipfs   │
│                │  │                │  │                │
│ - BLAKE2b      │  │ - BLAKE2b      │  │ - BLAKE2b      │
│ - Merkle CID   │  │ - Merkle CID   │  │ - Merkle CID   │
│ - Kademlia DHT │  │ - Kademlia DHT │  │ - Kademlia DHT │
│ - Mongoose HTTP│  │ - Mongoose HTTP│  │ - aiohttp HTTP │
│ - LMDB storage │  │ - File storage │  │ - LMDB storage │
└────────────────┘  └────────────────┘  └────────────────┘
```

## Limitations

1. **NAT Traversal**: All implementations work best on local network or with port forwarding
2. **Encryption**: P2P traffic is unencrypted (production would add TLS)
3. **Block Discovery**: Currently uses simplified DHT (no iterative lookup)
4. **Provider Records**: Temporary, not persisted in DHT

## Future Enhancements

- [ ] NAT hole punching (UDP/STUN)
- [ ] TLS encryption for P2P
- [ ] Full Kademlia iterative lookup
- [ ] Persistent provider records
- [ ] Cross-platform binary releases
- [ ] Docker containers

## Troubleshooting

**Nodes can't find each other:**
- Ensure UDP port 4001 is open between hosts
- Check firewall settings
- Verify same network segment or port forwarding

**Block transfer fails:**
- Ensure TCP port 4002 is open
- Check that announcing node is still running
- Verify block hash matches

**HTTP gateway not working:**
- Each node has its own HTTP server
- Can only serve locally stored blocks
- Use P2P to fetch first, then HTTP works

## License

MIT License - Part of the A.E.T.H.E.R. P2P Protocol project.
