# A.E.T.H.E.R. Python

A.E.T.H.E.R. (Asynchronous Edge-Tolerant Holographic Execution Runtime) - Python Implementation

## Features

- **Classic P2P Mode**: Ed25519 signatures, Kademlia DHT, peer messaging
- **IPFS-like Mode**: BLAKE2b chunking, Merkle trees, HTTP gateway
- **Cross-Platform**: Works with C (Linux/Windows) implementations

## Installation

```bash
pip install -e .

# For IPFS features:
pip install lmdb aiohttp
```

## Usage

```bash
# Add a file
aether add myfile.txt

# Get a file
aether get <CID> output.txt

# Start daemon with HTTP gateway
aether daemon --port 8080

# Access via browser
http://localhost:8080/ipfs/<CID>
```

## API Endpoints

- `GET /ipfs/{cid}` - Get file by CID
- `POST /api/v0/add` - Add file (multipart form)
- `GET /api/v0/cat?arg={cid}` - Get file (IPFS-compatible)
- `GET /status` - Node status

## License

MIT License
