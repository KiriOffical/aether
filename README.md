# A.E.T.H.E.R. Node

**Asynchronous Edge-Tolerant Holographic Execution Runtime**

Decentralized P2P network node implementing the A.E.T.H.E.R. protocol.

[![Build Status](../../actions/workflows/build.yml/badge.svg)](../../actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

## Features

- **P2P Networking** - TCP-based peer-to-peer communication
- **Cryptographic Identity** - Ed25519 keypairs for node authentication
- **DHT Routing** - Kademlia-style distributed hash table
- **Message Framing** - Length-prefixed binary protocol
- **Peer Management** - Connection tracking, blacklisting, trust scores
- **Cross-Platform** - Windows, Linux, macOS support

## Quick Start

### Download Pre-built Binary

See [BUILD.md](BUILD.md) for download links and build instructions.

### Run

```bash
# Start node
./aether-node

# Or with options
./aether-node --port 7821 --verbose
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Aegis Browser                        │
├─────────────────────────────────────────────────────────┤
│              A.E.T.H.E.R. Node (This Project)           │
│  ┌───────────┬───────────┬───────────┬─────────────┐   │
│  │ Handshake │    DHT    │  Framing  │   Crypto    │   │
│  │ Protocol  │ (Kademlia)│   Layer   │  (Ed25519)  │   │
│  └───────────┴───────────┴───────────┴─────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## Project Structure

```
aether/
├── include/           # Public headers
│   ├── aether.h       # Main API
│   ├── crypto.h       # Cryptography
│   ├── dht.h          # DHT routing
│   ├── framing.h      # Message framing
│   ├── handshake.h    # Handshake protocol
│   ├── message.h      # Message types
│   ├── peer.h         # Peer management
│   ├── protocol.h     # Core protocol
│   └── util.h         # Utilities
├── src/               # Implementation
│   ├── crypto.c       # Ed25519, SHA-256
│   ├── dht.c          # Kademlia routing
│   ├── framing.c      # Length-prefixed framing
│   ├── handshake.c    # Handshake state machine
│   ├── message.c      # Message serialization
│   ├── node.c         # Main daemon + library
│   ├── peer.c         # Peer tracking
│   ├── protocol.c     # Protocol runner
│   └── util.c         # Helper functions
├── lib/               # Third-party (public domain)
│   ├── ed25519/       # Ed25519 signatures
│   └── sha256/        # SHA-256 hashing
├── .github/
│   └── workflows/
│       └── build.yml  # CI/CD pipeline
├── Makefile           # Build configuration
├── BUILD.md           # Build instructions
└── README.md          # This file
```

## Protocol Specification

See [docs/P2P_PROTOCOL_SPEC.md](docs/P2P_PROTOCOL_SPEC.md) for the wire protocol specification.

## API Usage

```c
#include "aether.h"

int main() {
    aether_config_t config = {0};
    config.listen_port = 7821;
    config.max_connections = 1000;
    
    aether_init();
    
    aether_node_t* node = aether_node_create(&config);
    aether_node_start(node);
    aether_node_run(node);  // Blocking
    
    aether_node_stop(node);
    aether_node_destroy(node);
    aether_cleanup();
    
    return 0;
}
```

## Building

```bash
# Standard build
make

# Debug build
make debug

# Clean
make clean

# See BUILD.md for platform-specific instructions
```

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 7821 | Listening port |
| `--datadir` | `aether_data` | Data directory |
| `--config` | - | Configuration file |
| `--verbose` | - | Enable debug logging |
| `--help` | - | Show help |

## Security Notes

⚠️ **This is a demo/prototype implementation:**

- Ed25519 implementation is simplified (not production-ready)
- Use libsodium or similar for production deployments
- No TLS/encryption on wire protocol yet
- Rate limiting is basic

## Roadmap

- [ ] Full Ed25519 implementation
- [ ] TLS/encryption layer
- [ ] Holographic storage (erasure coding)
- [ ] Semantic DHT with vector embeddings
- [ ] WebAssembly runtime
- [ ] CRDT support
- [ ] Phoenix Protocol failsafe

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests: `make test`
5. Submit a pull request

## License

MIT License - See [LICENSE](LICENSE) file.

---

**A.E.T.H.E.R.** - Building a self-healing, self-organizing decentralized web.
