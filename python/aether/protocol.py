"""
Core protocol for A.E.T.H.E.R. P2P network.
"""

import json
import socket
import struct
import threading
import time
from dataclasses import dataclass
from enum import IntEnum
from typing import Dict, List, Optional, Callable, Any
import logging

from .crypto import Crypto, KeyPair
from .dht import DHT, Endpoint, BucketEntry
from .peer import Peer, PeerManager, PeerState


logger = logging.getLogger("aether")


class MessageType(IntEnum):
    """Message types for the P2P protocol."""
    HELLO = 1
    HELLO_ACK = 2
    FIND_NODE = 3
    FIND_NODE_RESPONSE = 4
    STORE_VALUE = 5
    GET_VALUE = 6
    GET_VALUE_RESPONSE = 7
    PING = 8
    PONG = 9
    DISCONNECT = 10
    PEER_EXCHANGE = 11
    ERROR = 12


@dataclass
class Message:
    """Protocol message."""
    type: MessageType
    payload: dict
    
    def encode(self) -> bytes:
        """Encode message to bytes."""
        payload_json = json.dumps(self.payload).encode('utf-8')
        header = struct.pack('>BI', self.type, len(payload_json))
        return header + payload_json
    
    @classmethod
    def decode(cls, data: bytes) -> Optional['Message']:
        """Decode message from bytes."""
        if len(data) < 5:
            return None
        
        msg_type, payload_len = struct.unpack('>BI', data[:5])
        if len(data) < 5 + payload_len:
            return None
        
        try:
            payload = json.loads(data[5:5+payload_len].decode('utf-8'))
            return cls(MessageType(msg_type), payload)
        except (json.JSONDecodeError, ValueError):
            return None


@dataclass
class Config:
    """Node configuration."""
    identity_path: str = ""
    data_dir: str = "aether_data"
    listen_port: int = 7821
    listen_addr: str = "0.0.0.0"
    max_connections: int = 10000
    max_message_size: int = 64 * 1024 * 1024  # 64 MB
    bootstrap_nodes: List[str] = None
    log_level: int = logging.INFO
    log_file: Optional[str] = None
    enable_tls: bool = False
    auth_token: Optional[str] = None
    
    def __post_init__(self):
        if self.bootstrap_nodes is None:
            self.bootstrap_nodes = [
                "bootstrap.aether.network:7821",
                "archive.org.aether.network:7821",
                "myrient.org.aether.network:7821",
            ]


class Node:
    """Main A.E.T.H.E.R. Node."""
    
    VERSION = "0.1.0"
    NODE_ID_SIZE = 32
    
    def __init__(self, config: Config = None):
        self.config = config or Config()
        self.keypair: Optional[KeyPair] = None
        self.node_id: bytes = b''
        self.peer_manager = PeerManager(self.config.max_connections)
        self.dht: Optional[DHT] = None
        self.server: Optional[socket.socket] = None
        self.running = False
        self._server_thread: Optional[threading.Thread] = None
        self._message_callbacks: List[Callable] = []
        self._peer_callbacks: List[Callable] = []
        
        self._init_crypto()
        self._setup_logging()
    
    def _init_crypto(self):
        """Initialize crypto and load/generate identity."""
        if self.config.identity_path:
            self.keypair = KeyPair.load_or_generate(self.config.identity_path)
        else:
            self.keypair = KeyPair.generate()
        
        self.node_id = Crypto.node_id(self.keypair.public_key)
    
    def _setup_logging(self):
        """Setup logging."""
        handlers = [logging.StreamHandler()]
        if self.config.log_file:
            import os
            os.makedirs(os.path.dirname(self.config.log_file) if os.path.dirname(self.config.log_file) else '.', exist_ok=True)
            handlers.append(logging.FileHandler(self.config.log_file))
        
        for handler in handlers:
            handler.setLevel(self.config.log_level)
            formatter = logging.Formatter('[%(levelname)s] %(asctime)s - %(message)s')
            handler.setFormatter(formatter)
        
        logger.handlers.clear()
        for handler in handlers:
            logger.addHandler(handler)
        logger.setLevel(logging.DEBUG)
    
    def start(self):
        """Start the node."""
        import os
        os.makedirs(self.config.data_dir, exist_ok=True)
        
        endpoint = Endpoint.from_ipv4(self.config.listen_addr, self.config.listen_port)
        self.dht = DHT(self.node_id, endpoint)
        
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((self.config.listen_addr, self.config.listen_port))
        self.server.listen(100)
        self.server.setblocking(False)
        
        self.running = True
        self._server_thread = threading.Thread(target=self._accept_connections, daemon=True)
        self._server_thread.start()
        
        logger.info(f"Node ID: {self.node_id.hex()[:16]}...")
        logger.info(f"Listening on port {self.config.listen_port}")
    
    def _accept_connections(self):
        """Accept incoming connections."""
        while self.running:
            try:
                client_sock, addr = self.server.accept()
                logger.debug(f"New connection from {addr}")
                
                # Handle in separate thread
                thread = threading.Thread(
                    target=self._handle_client,
                    args=(client_sock, addr),
                    daemon=True
                )
                thread.start()
            except BlockingIOError:
                time.sleep(0.1)
            except Exception as e:
                if self.running:
                    logger.error(f"Accept error: {e}")
    
    def _handle_client(self, client_sock: socket.socket, addr):
        """Handle a client connection."""
        peer = Peer(
            node_id=Crypto.random_bytes(self.NODE_ID_SIZE),
            remote_addr=addr[0],
            remote_port=addr[1],
            state=PeerState.HANDSHAKING,
        )
        
        try:
            # Send HELLO first
            hello = Message(MessageType.HELLO, {
                'version': self.VERSION,
                'node_id': self.node_id.hex(),
                'timestamp': time.time(),
                'port': self.config.listen_port
            })
            client_sock.sendall(hello.encode())
            
            # Wait for HELLO_ACK
            client_sock.settimeout(5.0)
            data = self._recv_message(client_sock)
            if data:
                msg = Message.decode(data)
                if msg and msg.type == MessageType.HELLO_ACK:
                    # Verify auth token if configured
                    if self.config.auth_token:
                        client_token = msg.payload.get('auth_token')
                        if client_token != self.config.auth_token:
                            logger.warning(f"Auth failed from {addr}")
                            return
                    
                    peer.state = PeerState.CONNECTED
                    peer.node_id = bytes.fromhex(msg.payload.get('node_id', '00' * 32))
                    self.peer_manager.add_peer(peer)
                    logger.info(f"Peer connected: {peer.node_id.hex()[:8]}... from {addr}")
                    
                    # Handle messages
                    while self.running and peer.state == PeerState.CONNECTED:
                        client_sock.settimeout(30.0)
                        data = self._recv_message(client_sock)
                        if not data:
                            break
                        self._handle_message(peer, msg, client_sock)
        except socket.timeout:
            pass
        except Exception as e:
            logger.debug(f"Connection error: {e}")
        finally:
            peer.state = PeerState.DISCONNECTED
            self.peer_manager.remove_peer(peer.node_id)
            try:
                client_sock.close()
            except:
                pass
    
    def _recv_message(self, sock: socket.socket) -> Optional[bytes]:
        """Receive a complete message."""
        header = b''
        while len(header) < 5:
            chunk = sock.recv(5 - len(header))
            if not chunk:
                return None
            header += chunk
        
        _, payload_len = struct.unpack('>BI', header)
        if payload_len > self.config.max_message_size:
            return None
        
        payload = b''
        while len(payload) < payload_len:
            chunk = sock.recv(min(4096, payload_len - len(payload)))
            if not chunk:
                return None
            payload += chunk
        
        return header + payload
    
    def _handle_message(self, peer: Peer, msg: Message, sock: socket.socket):
        """Handle incoming message."""
        peer.last_activity = time.time()
        
        if msg.type == MessageType.PING:
            pong = Message(MessageType.PONG, {'sequence': msg.payload.get('sequence', 0)})
            sock.sendall(pong.encode())
        
        elif msg.type == MessageType.PONG:
            logger.debug(f"PONG from {peer.node_id.hex()[:8]}...")
        
        elif msg.type == MessageType.STORE_VALUE:
            key = bytes.fromhex(msg.payload['key'])
            value = bytes.fromhex(msg.payload['value'])
            self.dht.store(key, value, peer.node_id, b'')
            logger.debug(f"Stored value for key {key.hex()[:8]}...")
        
        elif msg.type == MessageType.GET_VALUE:
            key = bytes.fromhex(msg.payload['key'])
            value = self.dht.get(key)
            response = Message(MessageType.GET_VALUE_RESPONSE, {
                'found': value is not None,
                'value': value.hex() if value else '',
                'key': msg.payload['key']
            })
            sock.sendall(response.encode())
        
        elif msg.type == MessageType.FIND_NODE:
            target = bytes.fromhex(msg.payload['target'])
            closest = self.dht.find_closest_nodes(target)
            nodes = [{'node_id': e.node_id.hex(), 'addr': e.endpoint.address, 'port': e.endpoint.port}
                    for e in closest[:20]]
            response = Message(MessageType.FIND_NODE_RESPONSE, {'nodes': nodes})
            sock.sendall(response.encode())
        
        elif msg.type == MessageType.PEER_EXCHANGE:
            peers = self.peer_manager.get_random_peers(20)
            peer_list = [{'addr': p.remote_addr, 'port': p.remote_port} for p in peers]
            response = Message(MessageType.PEER_EXCHANGE, {'peers': peer_list})
            sock.sendall(response.encode())
    
    def stop(self):
        """Stop the node."""
        self.running = False
        if self.server:
            self.server.close()
        if self.dht:
            self.dht.cleanup()
        logger.info("Node stopped")
    
    def run(self):
        """Run main loop (blocking)."""
        last_ping = time.time()
        last_cleanup = time.time()
        
        try:
            while self.running:
                time.sleep(1)
                now = time.time()
                
                if now - last_ping >= 30:
                    last_ping = now
                    self._send_pings()
                
                if now - last_cleanup >= 300:
                    last_cleanup = now
                    self.peer_manager.evict_stale()
                    if self.dht:
                        self.dht.cleanup()
        except KeyboardInterrupt:
            pass
    
    def _send_pings(self):
        """Send pings to connected peers."""
        pass
    
    def send(self, target_id: bytes, data: bytes) -> bool:
        """Send message to specific peer."""
        peer = self.peer_manager.get_peer(target_id)
        if not peer:
            return False
        return True
    
    def broadcast(self, data: bytes):
        """Broadcast to all peers."""
        pass
    
    def dht_store(self, key: bytes, value: bytes) -> bool:
        """Store value in DHT."""
        if not self.dht:
            return False
        signature = self.keypair.sign(key + value)
        self.dht.store(key, value, self.node_id, signature)
        return True
    
    def dht_get(self, key: bytes) -> Optional[bytes]:
        """Get value from DHT."""
        if not self.dht:
            return None
        return self.dht.get(key)
    
    def dht_find_node(self, target: bytes, k: int = 20) -> List[dict]:
        """Find closest nodes to target."""
        if not self.dht:
            return []
        entries = self.dht.find_closest_nodes(target, k)
        return [{'node_id': e.node_id.hex(), 'addr': e.endpoint.address, 'port': e.endpoint.port} for e in entries]
    
    def get_peer_count(self) -> int:
        """Get active peer count."""
        return self.peer_manager.active_count()
    
    def get_port(self) -> int:
        """Get listening port."""
        return self.config.listen_port
    
    def get_stats(self) -> dict:
        """Get node statistics."""
        return {
            'node_id': self.node_id.hex(),
            'port': self.config.listen_port,
            'peer_count': self.peer_manager.active_count(),
            'dht_nodes': self.dht.node_count() if self.dht else 0,
            'dht_values': self.dht.value_count() if self.dht else 0,
            'version': self.VERSION
        }
    
    def set_message_callback(self, cb: Callable):
        """Set message callback."""
        self._message_callbacks.append(cb)
    
    def set_peer_callback(self, cb: Callable):
        """Set peer callback."""
        self._peer_callbacks.append(cb)
