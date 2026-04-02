# A.E.T.H.E.R.

**Asynchronous Edge-Tolerant Holographic Execution Runtime**

A decentralized P2P network implementation with Distributed Hash Table (DHT), messaging, and peer management. Available in both C/C++ and Python.

## Project Structure

```
aether/
├── cpp/          # C/C++ implementation
│   ├── include/  # Header files
│   ├── src/      # Source files
│   ├── lib/      # Third-party libraries (ed25519, sha256)
│   └── Makefile  # Build system
├── python/       # Python implementation
│   ├── aether/   # Python package
│   ├── tests/    # Test suite
│   └── setup.py  # Package setup
└── README.md     # This file
```

## Quick Start

### C/C++ Implementation

```bash
cd cpp

# Build
make

# Run a node
./bin/aether-node run

# Connect to a node
./bin/aether-node connect --ping
./bin/aether-node connect --store mykey myvalue
./bin/aether-node connect --get mykey
```

### Python Implementation

```bash
cd python

# Install dependencies
pip install -r requirements.txt

# Install package (optional)
pip install -e .

# Run a node
python -m aether.node run

# Connect to a node
python -m aether.node connect --ping
python -m aether.node connect --store mykey myvalue
python -m aether.node connect --get mykey
```

## Features

- **P2P Networking**: TCP-based peer-to-peer communication
- **DHT Storage**: Kademlia-style distributed hash table for key-value storage
- **Peer Management**: Automatic peer discovery, connection management, and peer exchange (PEX)
- **Message Protocol**: Binary protocol with support for ping, store, get, find operations
- **Security**: Ed25519 signatures, SHA-256 hashing
- **Cross-Language**: Identical functionality in C/C++ and Python

## Message Types

| Type | Name | Description |
|------|------|-------------|
| 1 | HELLO | Server greeting on connect |
| 2 | HELLO_ACK | Client acknowledgment |
| 3 | FIND_NODE | Find nodes near target ID |
| 4 | FIND_NODE_RESPONSE | Response with closest nodes |
| 5 | STORE_VALUE | Store key-value in DHT |
| 6 | GET_VALUE | Retrieve value by key |
| 7 | GET_VALUE_RESPONSE | Response with value |
| 8 | PING | Connectivity check |
| 9 | PONG | Ping response |
| 10 | DISCONNECT | Graceful disconnect |
| 11 | PEER_EXCHANGE | Request peer list |
| 12 | ERROR | Error message |

## API Examples

### C/C++

```cpp
#include "aether.hpp"
#include "protocol.hpp"

using namespace aether;

int main() {
    // Create and start node
    Config config;
    config.listen_port = 7821;
    Node node(config);
    node.start();

    // Store data in DHT
    node.dht_store({0x01, 0x02}, {0x03, 0x04});

    // Get data from DHT
    auto value = node.dht_get({0x01, 0x02});

    // Use client
    Client client("localhost", 7821);
    client.connect();
    client.store("key", "value");
    auto result = client.get("key");
    client.disconnect();

    node.stop();
    return 0;
}
```

### Python

```python
from aether import Node, Config, Client

# Create and start node
config = Config(listen_port=7821)
node = Node(config)
node.start()

# Store data in DHT
node.dht_store(b'key', b'value')

# Get data from DHT
value = node.dht_get(b'key')

# Use client
client = Client('localhost', 7821)
client.connect()
client.store('key', 'value')
result = client.get('key')
client.disconnect()

node.stop()
```

## Development

### C/C++

```bash
cd cpp

# Build
make

# Debug build
make debug

# Release build
make release

# Clean
make clean

# Install
sudo make install
```

### Python

```bash
cd python

# Install dependencies
pip install -r requirements.txt

# Run tests
pytest tests/

# Format code
black aether/

# Lint
flake8 aether/

# Type check
mypy aether/
```

## Requirements

### C/C++
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Make or compatible build system

### Python
- Python 3.8+
- PyNaCl (optional, for Ed25519)
- pytest (for testing)

## License

MIT License - See LICENSE file for details.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests
5. Submit a pull request

Both C/C++ and Python implementations should be kept in sync for feature parity.
