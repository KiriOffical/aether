# A.E.T.H.E.R. Examples

This directory contains example applications and tutorials demonstrating how to use A.E.T.H.E.R.

## Examples Overview

| Example | Description | Difficulty |
|---------|-------------|------------|
| [Basic Node](#basic-node) | Run a simple A.E.T.H.E.R. node | Beginner |
| [DHT Storage](#dht-storage) | Store and retrieve data from DHT | Beginner |
| [Chat Application](#chat-application) | P2P chat using A.E.T.H.E.R. | Intermediate |
| [File Sharing](#file-sharing) | Decentralized file sharing | Intermediate |
| [Custom Protocol](#custom-protocol) | Extend A.E.T.H.E.R. with custom messages | Advanced |

---

## Basic Node

The simplest example - running an A.E.T.H.E.R. node.

### Code

```python
from aether import Node, Config

# Create configuration
config = Config(
    listen_port=7821,
    data_dir="./aether_data",
    log_level="INFO"
)

# Create and start node
node = Node(config)
node.start()

print(f"Node started with ID: {node.node_id.hex()[:16]}...")
print(f"Listening on port: {node.get_port()}")

try:
    # Run main loop
    node.run()
except KeyboardInterrupt:
    print("\nShutting down...")
    node.stop()
```

### Running

```bash
python examples/basic_node.py
```

### Expected Output

```
Node started with ID: a1b2c3d4e5f67890...
Listening on port: 7821
[INFO] Node initialized successfully
[INFO] Listening for connections...
```

---

## DHT Storage

Store and retrieve data from the Distributed Hash Table.

### Code

```python
from aether import Node, Config
import time

# Setup node
config = Config(listen_port=7821)
node = Node(config)
node.start()

# Store some values
print("Storing values in DHT...")

node.dht_store(b"user:1001", b'{"name": "Alice", "age": 30}')
node.dht_store(b"user:1002", b'{"name": "Bob", "age": 25}')
node.dht_store(b"config:app", b'{"theme": "dark", "language": "en"}')

# Retrieve values
print("\nRetrieving values from DHT...")

user1_data = node.dht_get(b"user:1001")
if user1_data:
    print(f"User 1001: {user1_data.decode()}")

user2_data = node.dht_get(b"user:1002")
if user2_data:
    print(f"User 1002: {user2_data.decode()}")

config_data = node.dht_get(b"config:app")
if config_data:
    print(f"App Config: {config_data.decode()}")

# Try to get non-existent key
missing = node.dht_get(b"missing:key")
print(f"\nMissing key result: {missing}")

# Cleanup
node.stop()
```

### Running

```bash
python examples/dht_storage.py
```

### Expected Output

```
Storing values in DHT...

Retrieving values from DHT...
User 1001: {"name": "Alice", "age": 30}
User 1002: {"name": "Bob", "age": 25}
App Config: {"theme": "dark", "language": "en"}

Missing key result: None
```

---

## Chat Application

A simple P2P chat application using A.E.T.H.E.R.

### Code

```python
"""
P2P Chat Application using A.E.T.H.E.R.
"""

from aether import Node, Config, Client
import threading
import sys


class P2PChat:
    def __init__(self, port: int = 7821):
        self.config = Config(
            listen_port=port,
            data_dir=f"./chat_data_{port}"
        )
        self.node = Node(self.config)
        self.connected_peers = []
        
    def start(self):
        """Start the chat node."""
        self.node.start()
        print(f"Chat node started: {self.node.node_id.hex()[:16]}...")
        
        # Set message handler
        self.node.set_message_callback(self._handle_message)
        
        # Start input thread
        input_thread = threading.Thread(target=self._input_loop, daemon=True)
        input_thread.start()
        
    def connect_to_peer(self, host: str, port: int):
        """Connect to another chat node."""
        client = Client(host, port)
        try:
            client.connect()
            self.connected_peers.append(client)
            print(f"Connected to peer at {host}:{port}")
        except Exception as e:
            print(f"Connection failed: {e}")
    
    def send_message(self, message: str):
        """Broadcast message to all connected peers."""
        msg_data = f"CHAT:{message}".encode()
        
        for peer in self.connected_peers:
            try:
                peer.send(msg_data)
            except Exception as e:
                print(f"Send error: {e}")
    
    def _handle_message(self, from_id, data):
        """Handle incoming message."""
        try:
            message = data.decode()
            if message.startswith("CHAT:"):
                chat_msg = message[5:]
                print(f"\n[Message from {from_id.hex()[:8]}...]: {chat_msg}")
        except:
            pass
    
    def _input_loop(self):
        """Read user input."""
        print("\n=== P2P Chat ===")
        print("Commands:")
        print("  /connect <host> <port> - Connect to peer")
        print("  /send <message>        - Send message")
        print("  /quit                  - Exit\n")
        
        while True:
            try:
                user_input = input("> ").strip()
                
                if user_input.startswith("/connect"):
                    parts = user_input.split()
                    if len(parts) >= 3:
                        host = parts[1]
                        port = int(parts[2])
                        self.connect_to_peer(host, port)
                
                elif user_input.startswith("/send"):
                    message = user_input[5:].strip()
                    self.send_message(message)
                
                elif user_input == "/quit":
                    self.node.stop()
                    sys.exit(0)
                
                else:
                    print("Unknown command. Type /help for commands.")
                    
            except EOFError:
                self.node.stop()
                sys.exit(0)
            except Exception as e:
                print(f"Error: {e}")


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="P2P Chat Application")
    parser.add_argument("--port", type=int, default=7821, help="Listen port")
    args = parser.parse_args()
    
    chat = P2PChat(port=args.port)
    chat.start()
    
    # Keep running
    try:
        while True:
            pass
    except KeyboardInterrupt:
        chat.node.stop()


if __name__ == "__main__":
    main()
```

### Running

Start two or more chat nodes:

```bash
# Terminal 1
python examples/chat_app.py --port 7821

# Terminal 2
python examples/chat_app.py --port 7822
```

Then connect them:

```
# In terminal 2
> /connect localhost 7821
Connected to peer at localhost:7821

> /send Hello from node 2!

# In terminal 1, you'll see:
[Message from abc123...]: Hello from node 2!
```

---

## File Sharing

Decentralized file sharing using A.E.T.H.E.R. DHT.

### Code

```python
"""
Decentralized File Sharing with A.E.T.H.E.R.
"""

from aether import Node, Config
import hashlib
import os


class FileSharer:
    def __init__(self, port: int = 7821, share_dir: str = "./shared"):
        self.config = Config(
            listen_port=port,
            data_dir=f"./fileshare_data_{port}"
        )
        self.node = Node(self.config)
        self.share_dir = share_dir
        os.makedirs(share_dir, exist_ok=True)
        
    def start(self):
        """Start the file sharing node."""
        self.node.start()
        print(f"File share node: {self.node.node_id.hex()[:16]}...")
        print(f"Sharing directory: {self.share_dir}")
        
    def share_file(self, filepath: str) -> str:
        """Share a file and return its hash."""
        with open(filepath, 'rb') as f:
            content = f.read()
        
        # Calculate file hash (used as key)
        file_hash = hashlib.sha256(content).hexdigest()
        
        # Store in DHT
        key = f"file:{file_hash}".encode()
        self.node.dht_store(key, content)
        
        # Store metadata
        metadata = {
            'filename': os.path.basename(filepath),
            'size': len(content),
            'hash': file_hash
        }
        self.node.dht_store(
            f"meta:{file_hash}".encode(),
            str(metadata).encode()
        )
        
        print(f"Shared file: {filepath} -> {file_hash[:16]}...")
        return file_hash
    
    def download_file(self, file_hash: str, output_path: str) -> bool:
        """Download a file from the DHT."""
        # Get file content
        content_key = f"file:{file_hash}".encode()
        content = self.node.dht_get(content_key)
        
        if not content:
            print(f"File not found: {file_hash}")
            return False
        
        # Verify hash
        actual_hash = hashlib.sha256(content).hexdigest()
        if actual_hash != file_hash:
            print("Hash verification failed!")
            return False
        
        # Save file
        with open(output_path, 'wb') as f:
            f.write(content)
        
        print(f"Downloaded: {output_path} ({len(content)} bytes)")
        return True
    
    def list_shared_files(self) -> list:
        """List all shared files."""
        # This would query the DHT for all file: keys
        # Simplified for example
        print("Shared files would be listed here")
        return []
    
    def stop(self):
        """Stop the node."""
        self.node.stop()


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="File Sharing Node")
    parser.add_argument("--port", type=int, default=7821)
    parser.add_argument("--share-dir", default="./shared")
    parser.add_argument("--share", help="File to share")
    parser.add_argument("--download", help="File hash to download")
    parser.add_argument("--output", help="Output path for download")
    args = parser.parse_args()
    
    sharer = FileSharer(port=args.port, share_dir=args.share_dir)
    sharer.start()
    
    if args.share:
        sharer.share_file(args.share)
    
    if args.download:
        output = args.output or f"./downloaded_{args.download[:8]}"
        sharer.download_file(args.download, output)
    
    if not args.share and not args.download:
        print("\nFile sharing node running.")
        print("Share a file: python file_share.py --share myfile.txt")
        print("Download: python file_share.py --download <hash> --output out.txt")
        
        try:
            while True:
                pass
        except KeyboardInterrupt:
            pass
    
    sharer.stop()


if __name__ == "__main__":
    main()
```

### Running

```bash
# Start a node and share a file
python examples/file_share.py --port 7821 --share document.pdf

# On another node, download the file
python examples/file_share.py --port 7822 --download <file_hash> --output downloaded.pdf
```

---

## Custom Protocol

Extend A.E.T.H.E.R. with custom message types.

### Code

```python
"""
Custom Protocol Extension for A.E.T.H.E.R.
"""

from aether import Node, Config, Client
from aether.protocol import Message, MessageType
import json


# Define custom message types
class CustomMessageType(MessageType):
    CUSTOM_DATA = 100
    CUSTOM_QUERY = 101
    CUSTOM_RESPONSE = 102


class ExtendedNode(Node):
    """Node with custom message handling."""
    
    def __init__(self, config: Config):
        super().__init__(config)
        self.custom_handlers = {}
        
    def register_handler(self, msg_type: int, handler):
        """Register handler for custom message type."""
        self.custom_handlers[msg_type] = handler
        
    def send_custom_data(self, peer_id: bytes, data: dict):
        """Send custom data to peer."""
        msg = Message(
            type=CustomMessageType.CUSTOM_DATA,
            payload={'data': data}
        )
        self._send_message(peer_id, msg)
        
    def query_peer(self, peer_id: bytes, query: str) -> dict:
        """Query a peer for information."""
        msg = Message(
            type=CustomMessageType.CUSTOM_QUERY,
            payload={'query': query}
        )
        # Send and wait for response
        response = self._send_and_wait(peer_id, msg, timeout=5.0)
        if response:
            return response.payload.get('result', {})
        return {}
    
    def _handle_custom_message(self, peer_id: bytes, msg: Message):
        """Handle incoming custom message."""
        handler = self.custom_handlers.get(msg.type)
        if handler:
            handler(peer_id, msg.payload)


# Example usage
def main():
    config = Config(listen_port=7821)
    node = ExtendedNode(config)
    node.start()
    
    # Register custom handlers
    def handle_custom_data(peer_id, payload):
        print(f"Received custom data from {peer_id.hex()[:8]}...")
        print(f"Data: {payload.get('data')}")
    
    def handle_custom_query(peer_id, payload):
        query = payload.get('query')
        print(f"Query from {peer_id.hex()[:8]}...: {query}")
        
        # Process query and send response
        result = {'status': 'ok', 'query': query}
        # node.send_response(peer_id, result)
    
    node.register_handler(CustomMessageType.CUSTOM_DATA, handle_custom_data)
    node.register_handler(CustomMessageType.CUSTOM_QUERY, handle_custom_query)
    
    print("Extended node running with custom protocol handlers")
    
    try:
        node.run()
    except KeyboardInterrupt:
        node.stop()


if __name__ == "__main__":
    main()
```

---

## Additional Resources

- [API Documentation](../docs/api/README.md)
- [Architecture Overview](../docs/architecture.md)
- [Contributing Guide](../CONTRIBUTING.md)

## Getting Help

- GitHub Issues: https://github.com/aether/issues
- Documentation: https://docs.aether.network
