# A.E.T.H.E.R.

**Asynchronous Edge-Tolerant Holographic Execution Runtime**

A decentralized P2P network implementation with Distributed Hash Table (DHT), messaging, and peer management. Available in both C/C++ and Python.

## Features

### Core Features
- **P2P Networking**: TCP-based peer-to-peer communication
- **DHT Storage**: Kademlia-style distributed hash table for key-value storage
- **Peer Management**: Automatic peer discovery, connection management, and peer exchange (PEX)
- **Message Protocol**: Binary protocol with support for ping, store, get, find operations
- **Security**: Ed25519 signatures, SHA-256 hashing
- **Cross-Language**: Identical functionality in C/C++ and Python

### Advanced Features (New!)
- **Advanced DHT**: Replication, consistency checks, iterative lookups
- **Metrics & Monitoring**: Prometheus-compatible metrics, real-time dashboards
- **Error Handling**: Circuit breakers, retry logic, recovery mechanisms
- **Trust System**: Peer reputation scoring, misbehavior tracking
- **NAT Traversal**: STUN, UPnP, hole-punching support
- **Rate Limiting**: Multiple algorithms, DoS protection
- **Persistence**: Snapshots, write-ahead logging, data recovery
- **Containerization**: Docker, Kubernetes, Helm charts

## Project Structure

```
aether/
├── cpp/                    # C/C++ implementation
│   ├── include/           # Header files
│   ├── src/               # Source files
│   ├── lib/               # Third-party libraries
│   └── Makefile           # Build system
├── python/                 # Python implementation
│   ├── aether/            # Python package
│   │   ├── node.py        # Main node implementation
│   │   ├── protocol.py    # Protocol handling
│   │   ├── dht.py         # DHT implementation
│   │   ├── dht_advanced.py # Advanced DHT features
│   │   ├── crypto.py      # Cryptography
│   │   ├── peer.py        # Peer management
│   │   ├── metrics.py     # Metrics & monitoring
│   │   ├── errors.py      # Error handling
│   │   ├── trust.py       # Trust & reputation
│   │   ├── nat.py         # NAT traversal
│   │   ├── ratelimit.py   # Rate limiting
│   │   └── persistence.py # Data persistence
│   ├── tests/             # Test suite
│   └── setup.py           # Package setup
├── docker/                 # Container configs
│   ├── Dockerfile
│   ├── docker-compose.yml
│   └── kubernetes/        # K8s manifests
├── web/                    # Web dashboard
│   └── dashboard.html
├── docs/                   # Documentation
│   ├── api/               # API docs (OpenAPI)
│   └── examples/          # Usage examples
├── examples/               # Example applications
└── config/                 # Configuration files
```

## Quick Start

### Python Implementation

```bash
cd python

# Install dependencies
pip install -r requirements.txt

# Install package (optional)
pip install -e .

# Run a node
python -m aether.node run

# Or use the example
python ../examples/basic_node.py --port 7821

# Connect to a node
python -m aether.node connect --ping
python -m aether.node connect --store mykey myvalue
python -m aether.node connect --get mykey
```

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

### Docker Deployment

```bash
# Using Docker Compose
cd docker
docker-compose up -d

# Or run directly
docker run -p 7821:7821 aether-node:latest

# Access the web dashboard
open http://localhost:7821/dashboard.html
```

### Kubernetes Deployment

```bash
# Using Helm
cd docker/kubernetes/helm
helm install aether ./

# Or apply manifests directly
kubectl apply -f docker/kubernetes/deployment.yaml
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

## Module Usage Examples

### Advanced DHT with Replication

```python
from aether.dht_advanced import AdvancedDHT, ConsistencyLevel
from aether import Node, Config

node = Node(Config(listen_port=7821))
node.start()

# Wrap with advanced DHT
advanced_dht = AdvancedDHT(node.dht, replication_factor=7)
advanced_dht.start()

# Store with replication
advanced_dht.store(
    key=b'mykey',
    value=b'myvalue',
    consistency=ConsistencyLevel.QUORUM
)

# Get with iterative lookup
value = advanced_dht.get(b'mykey', use_lookup=True)
```

### Metrics and Monitoring

```python
from aether.metrics import MetricsCollector, StructuredLogger

# Setup metrics
metrics = MetricsCollector(node_id=node.node_id.hex())
metrics.start()

# Record operations
metrics.record_store(duration=0.05)
metrics.record_lookup(duration=0.12)
metrics.update_peer_count(count=42)

# Export in Prometheus format
prometheus_metrics = metrics.get_metrics()

# Structured logging
logger = StructuredLogger("mynode", node_id=node.node_id.hex())
logger.info("Node started", component="main")
logger.error("Connection failed", peer_id=peer_id)
```

### Error Handling with Circuit Breaker

```python
from aether.errors import ErrorHandler, ErrorType, ErrorSeverity, create_error

error_handler = ErrorHandler(node_id=node.node_id.hex())

# Handle errors with automatic recovery
error = create_error(
    code=1001,
    message="Network unreachable",
    error_type=ErrorType.NETWORK,
    severity=ErrorSeverity.HIGH,
    recoverable=True
)

# Handle with retry logic
error_handler.handle_error(error)

# Use retry operation
result = error_handler.retry_operation(
    func=my_network_call,
    arg1, arg2,
    error_context={"peer": peer_id}
)
```

### Peer Trust System

```python
from aether.trust import TrustManager, TrustLevel

trust_manager = TrustManager()

# Record interactions
trust_manager.record_successful_interaction(peer_id, response_time=0.05)
trust_manager.record_failed_interaction(peer_id)

# Check trust level
level = trust_manager.get_trust_level(peer_id)
if level >= TrustLevel.TRUSTED:
    # Allow privileged operations
    pass

# Get trusted peers
trusted = trust_manager.get_trusted_peers(min_trust=TrustLevel.TRUSTED)

# Blacklist bad actors
trust_manager.blacklist_peer(peer_id, reason="Provided invalid data")
```

### NAT Traversal

```python
from aether.nat import NATTraversalManager

nat_manager = NATTraversalManager(local_port=7821)
nat_manager.start()

# Get public endpoint
endpoint = nat_manager.get_public_endpoint()
print(f"Public: {endpoint.public_ip}:{endpoint.public_port}")

# Connect to peer behind NAT
success = nat_manager.connect_to_peer(peer_id, peer_endpoint)

# Check NAT type
nat_type = nat_manager.get_nat_type()
```

### Rate Limiting

```python
from aether.ratelimit import ProtectionManager, RateLimitConfig, RateLimitAlgorithm

config = RateLimitConfig(
    algorithm=RateLimitAlgorithm.TOKEN_BUCKET,
    requests_per_second=10.0,
    burst_size=20
)

protection = ProtectionManager(config)

# Check each request
allowed, reason = protection.check_request(peer_id, "dht_store")
if not allowed:
    print(f"Request blocked: {reason}")

# Get protection status
status = protection.get_protection_status()
```

### Data Persistence

```python
from aether.persistence import DataManager, PersistenceConfig

config = PersistenceConfig(
    data_dir="./aether_data",
    enable_snapshots=True,
    snapshot_interval=3600  # 1 hour
)

data_manager = DataManager(config)
data_manager.start()

# Store data
data_manager.store(b'key', b'value', metadata={'ttl': 3600})

# Create snapshot
snapshot = data_manager.create_snapshot()

# Restore from snapshot
data_manager.restore_snapshot(snapshot.snapshot_id)

# Export data
exported = data_manager.export_data(format='json')
```

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
