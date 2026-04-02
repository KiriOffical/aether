# A.E.T.H.E.R.

**Asynchronous Edge-Tolerant Holographic Execution Runtime**

A decentralized P2P network implementation with Distributed Hash Table (DHT), messaging, and peer management. Written in C/C++.

## Features

- **P2P Networking**: TCP-based peer-to-peer communication
- **DHT Storage**: Kademlia-style distributed hash table for key-value storage
- **Peer Management**: Automatic peer discovery, connection management, and peer exchange (PEX)
- **Message Protocol**: Binary protocol with support for ping, store, get, find operations
- **Security**: Ed25519 signatures, SHA-256 hashing
- **Modern C++**: C++17 with clean API and RAII resource management

## Requirements

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Make or compatible build system
- Optional: `clang-format` for code formatting

## Installation

```bash
# Clone or copy to your project directory
cd aether

# Build the project
make

# Build with debug symbols
make debug

# Build optimized release
make release

# Install to /usr/local
sudo make install
```

## Quick Start

### Run a Node

```bash
# Run with default settings (port 7821)
./bin/aether-node run

# Run with custom port and data directory
./bin/aether-node run --port 8080 --datadir ./my_data

# Enable verbose logging
./bin/aether-node run --verbose

# Show help
./bin/aether-node run --help
```

### Connect to a Node

```bash
# Send a ping
./bin/aether-node connect --ping

# Store a value
./bin/aether-node connect --store mykey myvalue

# Get a value
./bin/aether-node connect --get mykey

# Get peer list
./bin/aether-node connect --peers

# Connect to remote node
./bin/aether-node connect -H 192.168.1.100 -p 7821 --ping

# Interactive mode
./bin/aether-node connect -i
```

### Use as a Library

```cpp
#include "aether.hpp"
#include "protocol.hpp"
#include <iostream>

using namespace aether;

int main() {
    // Create configuration
    Config config;
    config.listen_port = 7821;
    config.data_dir = "./aether_data";
    config.log_level = LogLevel::Info;

    // Create and start node
    Node node(config);
    
    if (node.start() != Error::Ok) {
        std::cerr << "Failed to start node" << std::endl;
        return 1;
    }

    // Store data in DHT
    node.dht_store({0x01, 0x02}, {0x03, 0x04});

    // Get data from DHT
    auto value = node.dht_get({0x01, 0x02});
    if (value) {
        std::cout << "Found value!" << std::endl;
    }

    // Get node info
    std::cout << "Node ID: " << node_id_to_hex(node.node_id()) << std::endl;
    std::cout << "Port: " << node.port() << std::endl;
    std::cout << "Peers: " << node.peer_count() << std::endl;

    // Run main loop (in separate thread)
    std::thread run_thread([&node]() { node.run(); });

    // ... do other work ...

    // Stop node
    node.stop();
    run_thread.join();

    return 0;
}
```

### Use the Client

```cpp
#include "protocol.hpp"
#include <iostream>

using namespace aether;

int main() {
    // Connect to a node
    Client client("localhost", 7821);
    
    if (client.connect() != Error::Ok) {
        std::cerr << "Failed to connect" << std::endl;
        return 1;
    }

    // Send ping
    double latency = client.ping();
    std::cout << "Latency: " << latency << "ms" << std::endl;

    // Store value
    client.store("mykey", "myvalue");

    // Get value
    auto value = client.get("mykey");
    if (value) {
        std::cout << "Value: " << *value << std::endl;
    }

    // Get peers
    auto peers = client.peer_exchange();
    std::cout << "Found " << peers.size() << " peers" << std::endl;

    client.disconnect();
    return 0;
}
```

## Architecture

```
aether/
├── include/
│   ├── aether.hpp      # Core types and constants
│   ├── crypto.hpp      # Cryptographic primitives
│   ├── dht.hpp         # DHT routing and storage
│   ├── peer.hpp        # Peer management
│   └── protocol.hpp    # Protocol and Node/Client classes
├── src/
│   ├── aether.cpp      # Core API implementation
│   ├── crypto.cpp      # Crypto implementation
│   ├── dht.cpp         # DHT implementation
│   ├── peer.cpp        # Peer management
│   ├── protocol.cpp    # Protocol, Node, Client
│   └── main.cpp        # CLI entry point
├── lib/
│   ├── ed25519/        # Ed25519 signature library
│   └── sha256/         # SHA-256 hash library
└── bin/                # Built binaries
```

## Protocol

### Message Format

```
+--------+--------+--------+--------+--------+
|  Type  |     Payload Length (32-bit)       |
+--------+--------+--------+--------+--------+
|              Payload (JSON)                 |
+--------+--------+--------+--------+--------+
```

### Message Types

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

## Configuration

### Config Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `identity_path` | string | "" | Path to keypair file |
| `data_dir` | string | "aether_data" | Data directory |
| `listen_port` | uint16 | 7821 | TCP listening port |
| `max_connections` | size_t | 10000 | Maximum peer connections |
| `log_level` | LogLevel | Info | Logging verbosity |
| `bootstrap_nodes` | vector | [...] | Bootstrap node addresses |
| `auth_token` | string | "" | Authentication token |

## API Reference

### Node Class

```cpp
class Node {
    // Lifecycle
    Node(const Config& config);
    Error start();           // Start listening
    Error stop();            // Stop node
    Error run();             // Run main loop (blocking)

    // Info
    const NodeId& node_id() const;
    uint16_t port() const;
    size_t peer_count() const;
    NodeStats stats() const;

    // DHT operations
    Error dht_store(const std::vector<uint8_t>& key,
                    const std::vector<uint8_t>& value);
    std::optional<std::vector<uint8_t>> dht_get(const std::vector<uint8_t>& key) const;
    std::vector<BucketEntry> dht_find_node(const NodeId& target, size_t k = 20) const;

    // Messaging
    Error send(const NodeId& target_id, const std::vector<uint8_t>& data);
    Error broadcast(const std::vector<uint8_t>& data);

    // Callbacks
    void set_message_callback(MessageCallback cb);
    void set_peer_callback(PeerCallback cb);
    void set_log_callback(LogCallback cb);
};
```

### Client Class

```cpp
class Client {
    Client(const std::string& host = "localhost", uint16_t port = 7821);

    Error connect(int timeout = 5);
    void disconnect();
    bool is_connected() const;

    double ping();
    Error store(const std::vector<uint8_t>& key,
                const std::vector<uint8_t>& value);
    std::optional<std::vector<uint8_t>> get(const std::vector<uint8_t>& key);
    std::vector<BucketEntry> find_node(const NodeId& target);
    std::vector<Endpoint> peer_exchange();
};
```

## Development

### Building

```bash
# Build everything
make

# Debug build
make debug

# Release build
make release

# Build static library only
make lib

# Clean build artifacts
make clean
```

### Testing

```bash
# Run basic tests
make test

# Test the node binary
./bin/aether-node --help
```

### Code Style

```bash
# Format code (requires clang-format)
make format

# Run linter (requires cppcheck)
make lint
```

## Error Handling

All API functions return `Error` enum values:

```cpp
enum class Error {
    Ok = 0,
    Io = -1,
    InvalidArg = -2,
    NoMemory = -3,
    ConnectionClosed = -4,
    HandshakeFailed = -5,
    // ... more error types
};

// Convert error to string
const char* error_string(Error err);
```

## Thread Safety

- `Node` is thread-safe for concurrent access
- `Client` is NOT thread-safe (use one per thread)
- All callbacks are invoked from worker threads

## License

MIT License - See LICENSE file for details.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests: `make test`
5. Format code: `make format`
6. Submit a pull request
