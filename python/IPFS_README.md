# A.E.T.H.E.R. Python - IPFS-like P2P Module

## Features

- **256KB Block Chunking** - Files split into 256KB chunks
- **BLAKE2b-256 Hashing** - Fast, secure content addressing
- **Merkle Tree CIDs** - Content Identifier = Merkle root of block hashes
- **Kademlia DHT** - UDP-based peer discovery
- **TCP Block Transfer** - Reliable block fetching
- **HTTP Gateway** - Browser access via `localhost:8080/ipfs/<CID>`
- **LMDB Storage** - Fast local indexing

## Installation

```bash
cd /workspaces/aether/python
pip install -e .

# For full IPFS features:
pip install lmdb aiohttp
```

## Usage

### Add a File

```bash
aether add myfile.txt
# Output: Added file. CID: 7a834da63a4429ef...
```

### Get a File

```bash
aether get <CID> output.txt
```

### Start Daemon

```bash
aether daemon --port 8080
# HTTP gateway: http://localhost:8080/ipfs/<CID>
```

### Python API

```python
from aether.ipfs import Node

# Create node
node = Node()
node.start()

# Add file
cid = node.add("/path/to/file.mp4")
print(f"CID: {cid.hex()}")

# Get file
node.get(cid, "/output/path")

node.stop()
```

## Architecture

```
python/
├── aether/
│   ├── ipfs.py       # Main IPFS implementation
│   ├── cli.py        # Command-line interface
│   ├── node.py       # Classic P2P node
│   ├── protocol.py   # P2P protocol
│   ├── crypto.py     # Cryptography (Ed25519, BLAKE2b)
│   └── dht.py        # Kademlia DHT
├── tests/
├── setup.py
└── requirements.txt
```

## Storage Layout

```
~/.my_ipfs/
├── blocks/
│   └── <2-char>/
│       └── <full-hash>  # Block data
├── manifests/
│   └── <cid>.json       # File manifest
└── index.db             # LMDB index
```

## Manifest Format

```json
{
    "cid": "7a834da63a4429ef...",
    "filename": "video.mp4",
    "total_size": 1048576,
    "block_count": 4,
    "blocks": [
        {"index": 0, "hash": "...", "size": 262144},
        {"index": 1, "hash": "...", "size": 262144}
    ]
}
```

## Testing

```bash
# Test add/get
echo "test" > /tmp/test.txt
aether add /tmp/test.txt
aether get <CID> /tmp/out.txt
diff /tmp/test.txt /tmp/out.txt

# Test daemon
aether daemon &
curl http://localhost:8080/ipfs/<CID>
```

## Comparison: C vs Python

| Feature | C11 | Python |
|---------|-----|--------|
| Chunking | ✓ | ✓ |
| BLAKE2b | ✓ (libsodium) | ✓ (hashlib) |
| Merkle Tree | ✓ | ✓ |
| DHT (UDP) | ✓ | ✓ |
| TCP Transfer | ✓ | ✓ |
| HTTP Gateway | ✓ (Mongoose) | ✓ (aiohttp) |
| Storage | ✓ (LMDB) | ✓ (LMDB) |
| Size | ~500KB | ~20KB |
| Speed | Fast | Moderate |

## Dependencies

- **Required**: Python 3.8+
- **Optional**: PyNaCl (crypto), lmdb (storage), aiohttp (HTTP)

## License

MIT License - Part of the A.E.T.H.E.R. P2P Protocol project.
