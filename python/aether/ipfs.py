"""
A.E.T.H.E.R. IPFS-like P2P Node - Python Implementation

Features:
- 256KB block chunking with BLAKE2b hashing
- Merkle tree content addressing (CID)
- Kademlia-style DHT over UDP
- TCP block transfer
- HTTP gateway (Mongoose-like via aiohttp)
- LMDB local storage
"""

import asyncio
import hashlib
import json
import os
import socket
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Any
import logging
import threading
import secrets

# Try imports
try:
    import lmdb
    LMDB_AVAILABLE = True
except ImportError:
    LMDB_AVAILABLE = False
    lmdb = None  # Placeholder

try:
    from aiohttp import web
    AIOHTTP_AVAILABLE = True
except ImportError:
    AIOHTTP_AVAILABLE = False
    web = None  # Placeholder

try:
    import nacl.signing
    NACL_AVAILABLE = True
except ImportError:
    NACL_AVAILABLE = False

# STUN servers for NAT traversal
STUN_SERVERS = [
    ("stun.l.google.com", 19302),
    ("stun1.l.google.com", 19302),
    ("stun.stunprotocol.org", 3478),
]

# Local broadcast discovery
BROADCAST_INTERVAL_SECS = 60
BROADCAST_PORT = 4001
BROADCAST_ADDRESS = "255.255.255.255"


# Constants
IPFS_CHUNK_SIZE = 256 * 1024  # 256 KB
IPFS_HASH_SIZE = 32  # BLAKE2b-256
IPFS_DEFAULT_UDP_PORT = 4001
IPFS_DEFAULT_TCP_PORT = 4002
IPFS_DEFAULT_HTTP_PORT = 8080
IPFS_K_BUCKET_SIZE = 20


def blake2b_hash(data: bytes) -> bytes:
    """Compute BLAKE2b-256 hash."""
    return hashlib.blake2b(data, digest_size=IPFS_HASH_SIZE).digest()


def compute_merkle_root(hashes: List[bytes]) -> bytes:
    """Compute Merkle root from list of hashes."""
    if not hashes:
        return b'\x00' * IPFS_HASH_SIZE
    
    if len(hashes) == 1:
        return hashes[0]
    
    # If odd, duplicate last
    if len(hashes) % 2 == 1:
        hashes = hashes + [hashes[-1]]
    
    # Pair and hash
    new_level = []
    for i in range(0, len(hashes), 2):
        combined = hashes[i] + hashes[i + 1]
        new_level.append(blake2b_hash(combined))
    
    return compute_merkle_root(new_level)


def stun_get_public_ip() -> Optional[Tuple[str, int]]:
    """
    Discover public IP and port via STUN.
    Returns (ip, port) tuple or None on failure.
    """
    import socket
    import struct
    
    # STUN message constants
    STUN_MAGIC = 0x2112A442
    STUN_BINDING_REQUEST = 0x0001
    
    for server_host, server_port in STUN_SERVERS:
        try:
            # Create UDP socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(5.0)
            
            # Build STUN binding request
            transaction_id = secrets.token_bytes(12)
            request = struct.pack('>HHI', STUN_BINDING_REQUEST, 0, STUN_MAGIC)
            request += transaction_id
            
            # Send request
            sock.sendto(request, (server_host, server_port))
            
            # Receive response
            response, _ = sock.recvfrom(512)
            
            # Parse response
            if len(response) < 20:
                continue
            
            msg_type, msg_len = struct.unpack('>HH', response[:4])
            if msg_type != 0x0101:  # Binding response
                continue
            
            # Parse attributes
            offset = 20
            while offset < 20 + msg_len:
                attr_type, attr_len = struct.unpack('>HH', response[offset:offset+4])
                
                # XOR-MAPPED-ADDRESS or MAPPED-ADDRESS
                if attr_type in (0x0001, 0x0020):
                    family = response[offset + 4]
                    port = struct.unpack('>H', response[offset+5:offset+7])[0]
                    
                    if attr_type == 0x0020:  # XOR
                        port ^= (STUN_MAGIC >> 16)
                    
                    if family == 0x01:  # IPv4
                        ip_bytes = response[offset+7:offset+11]
                        if attr_type == 0x0020:  # XOR
                            magic_bytes = struct.pack('>I', STUN_MAGIC)
                            ip_bytes = bytes(a ^ b for a, b in zip(ip_bytes, magic_bytes))
                        
                        ip = '.'.join(str(b) for b in ip_bytes)
                        sock.close()
                        logging.info(f"STUN: Public IP {ip}:{port} via {server_host}")
                        return (ip, port)
                
                offset += 4 + attr_len
                # Align to 4-byte boundary
                if attr_len % 4 != 0:
                    offset += 4 - (attr_len % 4)
            
            sock.close()
        except Exception as e:
            logging.debug(f"STUN server {server_host} failed: {e}")
            continue
    
    logging.warning("All STUN servers failed")
    return None


@dataclass
class ManifestEntry:
    """Single block entry in manifest."""
    index: int
    hash: bytes
    size: int
    
    def to_dict(self) -> dict:
        return {
            'index': self.index,
            'hash': self.hash.hex(),
            'size': self.size
        }
    
    @classmethod
    def from_dict(cls, d: dict) -> 'ManifestEntry':
        return cls(
            index=d['index'],
            hash=bytes.fromhex(d['hash']),
            size=d['size']
        )


@dataclass
class Manifest:
    """File manifest with block list."""
    cid: bytes
    filename: str
    total_size: int
    block_size: int = IPFS_CHUNK_SIZE
    entries: List[ManifestEntry] = field(default_factory=list)
    
    @property
    def block_count(self) -> int:
        return len(self.entries)
    
    def to_dict(self) -> dict:
        return {
            'cid': self.cid.hex(),
            'filename': self.filename,
            'total_size': self.total_size,
            'block_count': self.block_count,
            'block_size': self.block_size,
            'blocks': [e.to_dict() for e in self.entries]
        }
    
    def to_json(self) -> str:
        return json.dumps(self.to_dict())
    
    @classmethod
    def from_dict(cls, d: dict) -> 'Manifest':
        return cls(
            cid=bytes.fromhex(d['cid']),
            filename=d['filename'],
            total_size=d['total_size'],
            block_size=d.get('block_size', IPFS_CHUNK_SIZE),
            entries=[ManifestEntry.from_dict(e) for e in d.get('blocks', [])]
        )
    
    @classmethod
    def from_json(cls, json_str: str) -> 'Manifest':
        return cls.from_dict(json.loads(json_str))


def chunk_file(filepath: str) -> Manifest:
    """Chunk a file into 256KB blocks and create manifest."""
    filepath = Path(filepath)
    
    # Read file
    data = filepath.read_bytes()
    total_size = len(data)
    
    # Create entries
    entries = []
    hashes = []
    
    for i in range(0, total_size, IPFS_CHUNK_SIZE):
        chunk = data[i:i + IPFS_CHUNK_SIZE]
        chunk_hash = blake2b_hash(chunk)
        entries.append(ManifestEntry(
            index=len(entries),
            hash=chunk_hash,
            size=len(chunk)
        ))
        hashes.append(chunk_hash)
    
    # Compute Merkle root (CID)
    cid = compute_merkle_root(hashes)
    
    return Manifest(
        cid=cid,
        filename=filepath.name,
        total_size=total_size,
        entries=entries
    )


class Storage:
    """Local block storage with LMDB indexing."""
    
    def __init__(self, base_path: str = None):
        if base_path:
            self.base_path = Path(base_path)
        else:
            home = os.environ.get('USERPROFILE') or os.environ.get('HOME') or '.'
            self.base_path = Path(home) / '.my_ipfs'
        
        self.blocks_path = self.base_path / 'blocks'
        self.manifests_path = self.base_path / 'manifests'
        self.db_path = self.base_path / 'index.db'
        
        self.blocks_path.mkdir(parents=True, exist_ok=True)
        self.manifests_path.mkdir(parents=True, exist_ok=True)
        
        self.env: Optional[lmdb.Environment] = None
        self.dbi = None
        
        if LMDB_AVAILABLE:
            self._init_lmdb()
    
    def _init_lmdb(self):
        """Initialize LMDB."""
        self.env = lmdb.open(
            str(self.db_path),
            map_size=1024 * 1024 * 1024,  # 1GB
            subdir=False  # Don't create subdirectory
        )
        self.dbi = self.env.open_db(None)
    
    def _hash_to_path(self, hash_bytes: bytes) -> Path:
        """Convert hash to sharded file path."""
        hash_hex = hash_bytes.hex()
        return self.blocks_path / hash_hex[:2] / hash_hex
    
    def put(self, hash_bytes: bytes, data: bytes) -> bool:
        """Store a block."""
        path = self._hash_to_path(hash_bytes)
        path.parent.mkdir(exist_ok=True)
        
        if path.exists():
            return True
        
        path.write_bytes(data)
        
        # Index in LMDB
        if self.env:
            with self.env.begin(write=True) as txn:
                txn.put(hash_bytes, str(path).encode(), db=self.dbi)
        
        return True
    
    def get(self, hash_bytes: bytes) -> Optional[bytes]:
        """Retrieve a block."""
        path = self._hash_to_path(hash_bytes)
        if not path.exists():
            return None
        return path.read_bytes()
    
    def has(self, hash_bytes: bytes) -> bool:
        """Check if block exists."""
        return self._hash_to_path(hash_bytes).exists()
    
    def put_manifest(self, manifest: Manifest) -> bool:
        """Store a manifest."""
        path = self.manifests_path / f"{manifest.cid.hex()}.json"
        path.write_text(manifest.to_json())
        return True
    
    def get_manifest(self, cid: bytes) -> Optional[Manifest]:
        """Retrieve a manifest."""
        path = self.manifests_path / f"{cid.hex()}.json"
        if not path.exists():
            return None
        return Manifest.from_json(path.read_text())


@dataclass
class Endpoint:
    """Network endpoint with NAT traversal support."""
    addr: str
    udp_port: int
    tcp_port: int
    public_addr: Optional[str] = None  # Public IP (NAT traversal)
    public_udp_port: Optional[int] = None
    public_tcp_port: Optional[int] = None
    
    def __post_init__(self):
        if self.public_addr is None:
            self.public_addr = self.addr
        if self.public_udp_port is None:
            self.public_udp_port = self.udp_port
        if self.public_tcp_port is None:
            self.public_tcp_port = self.tcp_port


@dataclass
class Peer:
    """Peer information."""
    node_id: bytes
    endpoint: Endpoint
    last_seen: float = field(default_factory=time.time)
    is_alive: bool = True


class KBucket:
    """K-bucket for routing table."""
    
    def __init__(self):
        self.peers: List[Peer] = []
    
    def add(self, peer: Peer) -> bool:
        """Add peer to bucket."""
        for i, p in enumerate(self.peers):
            if p.node_id == peer.node_id:
                self.peers[i] = peer
                return True
        
        if len(self.peers) < IPFS_K_BUCKET_SIZE:
            self.peers.append(peer)
            return True
        return False
    
    def remove(self, node_id: bytes):
        """Remove peer from bucket."""
        self.peers = [p for p in self.peers if p.node_id != node_id]


class RoutingTable:
    """Kademlia routing table."""
    
    def __init__(self, node_id: bytes):
        self.node_id = node_id
        self.buckets: List[KBucket] = [KBucket() for _ in range(256)]
        self.total_peers = 0
    
    def _bucket_index(self, target_id: bytes) -> int:
        """Get bucket index for target."""
        for i in range(32):
            if self.node_id[i] != target_id[i]:
                return i * 8 + (self.node_id[i] ^ target_id[i]).bit_length() - 1
        return 255
    
    def add_peer(self, peer: Peer) -> bool:
        """Add peer to routing table."""
        if peer.node_id == self.node_id:
            return False
        
        idx = self._bucket_index(peer.node_id)
        if idx >= len(self.buckets):
            idx = 0
        
        if self.buckets[idx].add(peer):
            self.total_peers = sum(len(b.peers) for b in self.buckets)
            return True
        return False
    
    def find_closest(self, target_id: bytes, k: int = IPFS_K_BUCKET_SIZE) -> List[Peer]:
        """Find k closest peers to target."""
        all_peers = []
        for bucket in self.buckets:
            all_peers.extend(bucket.peers)
        
        # Sort by XOR distance
        def xor_distance(peer):
            dist = 0
            for i in range(32):
                dist = (dist << 8) | (peer.node_id[i] ^ target_id[i])
            return dist
        
        all_peers.sort(key=xor_distance)
        return all_peers[:k]


class Network:
    """P2P network layer (UDP + TCP) - Compatible with C implementation."""
    
    # RPC types (must match C implementation)
    PING = 1
    PONG = 2
    FIND_NODE = 3
    FIND_VALUE = 4
    PROVIDE = 5
    GET_BLOCK = 6
    
    MAGIC = 0x49504653  # "IPFS"
    VERSION = 1
    
    def __init__(self, node_id: bytes, udp_port: int = IPFS_DEFAULT_UDP_PORT,
                 tcp_port: int = IPFS_DEFAULT_TCP_PORT):
        self.node_id = node_id
        self.udp_port = udp_port
        self.tcp_port = tcp_port
        self.routing_table = RoutingTable(node_id)
        
        self.udp_socket: Optional[socket.socket] = None
        self.tcp_socket: Optional[socket.socket] = None
        self.running = False
        self._udp_thread: Optional[threading.Thread] = None
        self._tcp_thread: Optional[threading.Thread] = None
        
        # NAT traversal - discovered via STUN
        self.public_ip: Optional[str] = None
        self.public_udp_port: Optional[int] = None
        self.public_tcp_port: Optional[int] = None
        
        # Local broadcast discovery
        self.broadcast_enabled = True
        self.last_broadcast: float = 0
        self.broadcast_interval = BROADCAST_INTERVAL_SECS
        self.broadcast_socket: Optional[socket.socket] = None
        
        # Block storage callback (set by Node)
        self.storage = None
        self._known_providers: Dict[bytes, List[Endpoint]] = {}
    
    def start(self):
        """Start network."""
        # UDP socket
        self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.udp_socket.bind(('0.0.0.0', self.udp_port))
        self.udp_socket.setblocking(False)
        
        # Broadcast socket for local discovery
        self.broadcast_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.broadcast_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.broadcast_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.broadcast_socket.bind(('0.0.0.0', self.udp_port))
        self.broadcast_socket.setblocking(False)
        
        # TCP socket
        self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp_socket.bind(('0.0.0.0', self.tcp_port))
        self.tcp_socket.listen(10)
        self.tcp_socket.setblocking(False)
        
        self.running = True
        self._udp_thread = threading.Thread(target=self._udp_loop, daemon=True)
        self._udp_thread.start()
        self._tcp_thread = threading.Thread(target=self._tcp_loop, daemon=True)
        self._tcp_thread.start()
        
        # Discover public IP via STUN (non-blocking, best effort)
        logging.info("[NAT] Discovering public IP via STUN...")
        result = stun_get_public_ip()
        if result:
            self.public_ip, self.public_udp_port = result
            self.public_tcp_port = self.tcp_port  # Assume same for TCP
            logging.info(f"[NAT] Public IP: {self.public_ip}:{self.public_udp_port} (UDP), {self.public_ip}:{self.public_tcp_port} (TCP)")
        else:
            logging.warning("[NAT] STUN failed, using local addresses only")
        
        # Send initial HELLO broadcast
        if self.broadcast_enabled:
            self._send_helo_broadcast()
        
        logging.info(f"Network started - UDP:{self.udp_port} TCP:{self.tcp_port}")
    
    def stop(self):
        """Stop network."""
        self.running = False
        if self.udp_socket:
            self.udp_socket.close()
        if self.tcp_socket:
            self.tcp_socket.close()
        if self.broadcast_socket:
            self.broadcast_socket.close()
    
    def _send_helo_broadcast(self):
        """Send HELLO broadcast packet for local discovery."""
        if not self.broadcast_enabled or not self.broadcast_socket:
            return
        
        # Build HELLO packet (same format as C)
        packet = struct.pack('>IBB', self.MAGIC, self.VERSION, 7)  # HELO = 7
        packet += self.node_id
        packet += struct.pack('>I', 0)  # No payload
        
        try:
            self.broadcast_socket.sendto(packet, (BROADCAST_ADDRESS, BROADCAST_PORT))
            self.last_broadcast = time.time()
            logging.info("[DISCOVERY] HELLO broadcast sent")
        except Exception as e:
            logging.debug(f"Broadcast failed: {e}")
    
    def _handle_helo(self, addr):
        """Handle incoming HELLO broadcast."""
        logging.info(f"[DISCOVERY] Received HELLO from {addr}")
        # Send HELLO_ACK back
        self._send_packet(addr, 8, b'')  # HELO_ACK = 8
    
    def _handle_helo_ack(self, addr):
        """Handle HELLO_ACK response."""
        logging.info(f"[DISCOVERY] Received HELLO_ACK from {addr}")
        # Peer already added to routing table
    
    def _udp_loop(self):
        """UDP message loop."""
        while self.running:
            try:
                # Check for periodic broadcast
                if self.broadcast_enabled and time.time() - self.last_broadcast > self.broadcast_interval:
                    self._send_helo_broadcast()
                
                data, addr = self.udp_socket.recvfrom(4096)
                self._handle_udp(data, addr)
            except BlockingIOError:
                time.sleep(0.1)
            except Exception as e:
                if self.running:
                    logging.error(f"UDP error: {e}")
    
    def _handle_udp(self, data: bytes, addr):
        """Handle UDP packet - compatible with C implementation."""
        if len(data) < 41:  # Header (6) + node_id (32) + payload_len (4)
            return
        
        # Parse header (matches C struct)
        magic, version, rpc_type = struct.unpack('>IBB', data[:6])
        if magic != self.MAGIC:
            return
        
        if version != self.VERSION:
            logging.warning(f"Version mismatch: {version}")
            return
        
        sender_id = data[6:38]
        payload_len = struct.unpack('>I', data[38:42])[0] if len(data) >= 42 else 0
        
        # Add sender to routing table
        endpoint = Endpoint(addr[0], addr[1], self.tcp_port)
        peer = Peer(sender_id, endpoint)
        self.routing_table.add_peer(peer)
        
        # Handle RPC based on type
        if rpc_type == self.PING:
            self._send_pong(addr)
        elif rpc_type == self.PONG:
            logging.debug(f"PONG from {sender_id.hex()[:8]}...")
        elif rpc_type == 7:  # HELO
            self._handle_helo(addr)
        elif rpc_type == 8:  # HELO_ACK
            self._handle_helo_ack(addr)
        elif rpc_type == self.FIND_NODE:
            if len(data) >= 70:
                target = data[42:74]
                self._handle_find_node(target, addr)
        elif rpc_type == self.FIND_VALUE:
            if len(data) >= 74:
                hash_bytes = data[42:74]
                self._handle_find_value(hash_bytes, addr)
        elif rpc_type == self.PROVIDE:
            if len(data) >= 74:
                hash_bytes = data[42:74]
                self._known_providers.setdefault(hash_bytes, []).append(endpoint)
                logging.info(f"Provider for {hash_bytes.hex()[:8]}...: {addr}")
    
    def _send_packet(self, addr, rpc_type: int, payload: bytes = b''):
        """Send UDP packet."""
        packet = struct.pack('>IBB', self.MAGIC, self.VERSION, rpc_type)
        packet += self.node_id
        packet += struct.pack('>I', len(payload))
        packet += payload
        self.udp_socket.sendto(packet, addr)
    
    def _send_pong(self, addr):
        """Send PONG response."""
        self._send_packet(addr, self.PONG)
    
    def _handle_find_node(self, target: bytes, addr):
        """Handle FIND_NODE request."""
        closest = self.routing_table.find_closest(target, 20)
        # Send back node list (simplified - just send count for now)
        response = struct.pack('>I', len(closest))
        for peer in closest[:5]:  # Send up to 5
            response += peer.node_id
            response += peer.endpoint.addr.encode().ljust(64, b'\x00')
            response += struct.pack('>HH', peer.endpoint.udp_port, peer.endpoint.tcp_port)
        self._send_packet(addr, self.FIND_NODE, response)
    
    def _handle_find_value(self, hash_bytes: bytes, addr):
        """Handle FIND_VALUE request."""
        # Check if we have the block
        if self.storage and self.storage.has(hash_bytes):
            # Send PROVIDE response
            self._send_packet(addr, self.PROVIDE, hash_bytes)
        else:
            # Return closest nodes
            closest = self.routing_table.find_closest(hash_bytes, 20)
            response = struct.pack('>I', len(closest))
            for peer in closest[:5]:
                response += peer.node_id
                response += peer.endpoint.addr.encode().ljust(64, b'\x00')
                response += struct.pack('>HH', peer.endpoint.udp_port, peer.endpoint.tcp_port)
            self._send_packet(addr, self.FIND_NODE, response)
    
    def _tcp_loop(self):
        """TCP accept loop."""
        while self.running:
            try:
                client_sock, addr = self.tcp_socket.accept()
                logging.debug(f"TCP connection from {addr}")
                thread = threading.Thread(
                    target=self._handle_tcp_client,
                    args=(client_sock, addr),
                    daemon=True
                )
                thread.start()
            except BlockingIOError:
                time.sleep(0.1)
            except Exception as e:
                if self.running:
                    logging.error(f"TCP accept error: {e}")
    
    def _handle_tcp_client(self, client_sock: socket.socket, addr):
        """Handle TCP client for block requests."""
        try:
            client_sock.settimeout(5.0)
            
            # Read request line: REQUEST_BLOCK:<hash>\n
            request = b''
            while b'\n' not in request:
                chunk = client_sock.recv(1)
                if not chunk:
                    return
                request += chunk
            
            request_str = request.decode().strip()
            if not request_str.startswith('REQUEST_BLOCK:'):
                return
            
            hash_hex = request_str[14:]
            try:
                hash_bytes = bytes.fromhex(hash_hex)
            except ValueError:
                return
            
            # Get block from storage
            if self.storage:
                data = self.storage.get(hash_bytes)
                if data:
                    # Send response: BLOCK:<size>\n<data>
                    response = f"BLOCK:{len(data)}\n".encode()
                    client_sock.sendall(response + data)
                    logging.debug(f"Sent block {hash_hex[:8]}... to {addr}")
        except Exception as e:
            logging.debug(f"TCP handler error: {e}")
        finally:
            try:
                client_sock.close()
            except:
                pass
    
    def find_value(self, hash_bytes: bytes) -> List[Endpoint]:
        """Find providers for a hash via DHT."""
        # Send FIND_VALUE to closest nodes
        closest = self.routing_table.find_closest(hash_bytes, 20)
        providers = []
        
        for peer in closest:
            try:
                packet = struct.pack('>IBB', self.MAGIC, self.VERSION, self.FIND_VALUE)
                packet += self.node_id
                packet += struct.pack('>I', IPFS_HASH_SIZE)
                packet += hash_bytes
                self.udp_socket.sendto(packet, (peer.endpoint.addr, peer.endpoint.udp_port))
            except Exception:
                pass
        
        # Check known providers
        return self._known_providers.get(hash_bytes, [])
    
    def provide(self, hash_bytes: bytes):
        """Announce we have a block."""
        closest = self.routing_table.find_closest(hash_bytes, 20)
        for peer in closest[:10]:  # Announce to 10 closest
            try:
                packet = struct.pack('>IBB', self.MAGIC, self.VERSION, self.PROVIDE)
                packet += self.node_id
                packet += struct.pack('>I', IPFS_HASH_SIZE)
                packet += hash_bytes
                self.udp_socket.sendto(packet, (peer.endpoint.addr, peer.endpoint.udp_port))
            except Exception:
                pass
    
    def get_block(self, endpoint: Endpoint, hash_bytes: bytes) -> Optional[bytes]:
        """Fetch block via TCP."""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5.0)
            sock.connect((endpoint.addr, endpoint.tcp_port))
            
            # Send request
            request = f"REQUEST_BLOCK:{hash_bytes.hex()}\n".encode()
            sock.sendall(request)
            
            # Read response header
            response = b''
            while b'\n' not in response:
                chunk = sock.recv(1)
                if not chunk:
                    return None
                response += chunk
            
            # Parse size
            if not response.startswith(b'BLOCK:'):
                return None
            
            size = int(response[6:].strip())
            
            # Read block
            data = b''
            while len(data) < size:
                chunk = sock.recv(min(4096, size - len(data)))
                if not chunk:
                    return None
                data += chunk
            
            sock.close()
            
            # Verify
            if blake2b_hash(data) != hash_bytes:
                return None
            
            return data
        except Exception as e:
            logging.error(f"get_block error: {e}")
            return None


class HTTPGateway:
    """HTTP gateway for browser access."""
    
    def __init__(self, storage: Storage, network: Network, 
                 host: str = '127.0.0.1', port: int = IPFS_DEFAULT_HTTP_PORT):
        self.storage = storage
        self.network = network
        self.host = host
        self.port = port
        self.node: Optional[Node] = None  # Set by Node.start()
        self.app: Optional[Any] = None
        self.runner: Optional[Any] = None
        self.site: Optional[Any] = None
    
    async def start(self):
        """Start HTTP server."""
        if not AIOHTTP_AVAILABLE:
            logging.warning("aiohttp not available, HTTP gateway disabled. Install with: pip install aiohttp")
            return
        
        self.app = web.Application()
        self.app.router.add_get('/ipfs/{cid}', self.handle_get)
        self.app.router.add_get('/status', self.handle_status)
        self.app.router.add_post('/api/v0/add', self.handle_add)  # IPFS-compatible add API
        self.app.router.add_get('/api/v0/cat', self.handle_cat)    # IPFS-compatible cat API
        
        self.runner = web.AppRunner(self.app)
        await self.runner.setup()
        
        self.site = web.TCPSite(self.runner, self.host, self.port)
        await self.site.start()
        
        logging.info(f"HTTP gateway on http://{self.host}:{self.port}")
    
    async def stop(self):
        """Stop HTTP server."""
        if self.runner:
            await self.runner.cleanup()
    
    async def handle_get(self, request: Any) -> Any:
        """Handle /ipfs/<cid> request."""
        if not AIOHTTP_AVAILABLE:
            return None
        
        cid_hex = request.match_info['cid']
        
        try:
            cid = bytes.fromhex(cid_hex)
        except ValueError:
            return web.Response(status=400, text="Invalid CID")
        
        # Get manifest
        manifest = self.storage.get_manifest(cid)
        if not manifest:
            return web.Response(status=404, text="Not found")
        
        # Assemble file
        data = b''
        for entry in manifest.entries:
            block = self.storage.get(entry.hash)
            if not block:
                # Would fetch from network here
                return web.Response(status=503, text="Block not available")
            data += block
        
        # Set headers with filename and content type detection
        headers = {
            'Content-Type': 'application/octet-stream',
            'Content-Length': str(len(data)),
            'Content-Disposition': f'attachment; filename="{manifest.filename}"',
            'X-IPFS-CID': cid_hex,
            'X-IPFS-Filename': manifest.filename,
            'Access-Control-Allow-Origin': '*',
            'X-Content-Type-Options': 'nosniff',
            'Connection': 'close',
        }
        
        # Detect content type from extension
        import mimetypes
        content_type, _ = mimetypes.guess_type(manifest.filename)
        if content_type:
            headers['Content-Type'] = content_type
        
        return web.Response(
            body=data,
            headers=headers
        )
    
    async def handle_status(self, request: Any) -> Any:
        """Handle /status request."""
        if not AIOHTTP_AVAILABLE:
            return None
        
        status = {
            'node_id': self.network.node_id.hex()[:16] + '...',
            'peers': self.network.routing_table.total_peers,
            'http_port': self.port
        }
        return web.json_response(status)
    
    async def handle_add(self, request: Any) -> Any:
        """Handle file upload (IPFS-compatible API)."""
        if not AIOHTTP_AVAILABLE:
            return None
        
        # Get uploaded file
        reader = await request.multipart()
        field = await reader.next()
        
        if not field:
            return web.Response(status=400, text="No file provided")
        
        # Get original filename
        original_filename = field.filename or 'upload'
        
        # Save to temp file and add
        import tempfile
        import shutil
        
        with tempfile.NamedTemporaryFile(delete=False, suffix=f'_{original_filename}') as f:
            while True:
                chunk = await field.read_chunk()
                if not chunk:
                    break
                f.write(chunk)
            temp_path = f.name
        
        try:
            if self.node:
                # Use node's add method but override filename in manifest
                manifest = chunk_file(temp_path)
                manifest.filename = original_filename  # Preserve original name
                
                # Store blocks
                data = Path(temp_path).read_bytes()
                for entry in manifest.entries:
                    start = entry.index * IPFS_CHUNK_SIZE
                    end = start + entry.size
                    self.storage.put(entry.hash, data[start:end])
                self.storage.put_manifest(manifest)
                
                # Announce if network is running
                if self.network.running:
                    self.network.provide(manifest.cid)
                
                cid = manifest.cid
            else:
                # Fallback without network announcement
                manifest = chunk_file(temp_path)
                manifest.filename = original_filename  # Preserve original name
                
                data = Path(temp_path).read_bytes()
                for entry in manifest.entries:
                    start = entry.index * IPFS_CHUNK_SIZE
                    end = start + entry.size
                    self.storage.put(entry.hash, data[start:end])
                self.storage.put_manifest(manifest)
                cid = manifest.cid
            
            # Return IPFS-compatible response
            response = {
                'Name': original_filename,
                'Hash': cid.hex(),
                'Size': '0'  # Would need to track
            }
            return web.json_response(response)
        finally:
            Path(temp_path).unlink(missing_ok=True)
    
    async def handle_cat(self, request: Any) -> Any:
        """Handle get file (IPFS-compatible API)."""
        if not AIOHTTP_AVAILABLE:
            return None
        
        arg = request.query.get('arg')
        if not arg:
            return web.Response(status=400, text="Missing 'arg' parameter (CID)")
        
        try:
            cid = bytes.fromhex(arg)
        except ValueError:
            return web.Response(status=400, text="Invalid CID")
        
        manifest = self.storage.get_manifest(cid)
        if not manifest:
            return web.Response(status=404, text="Not found")
        
        data = b''
        for entry in manifest.entries:
            block = self.storage.get(entry.hash)
            if not block:
                return web.Response(status=503, text="Block not available")
            data += block
        
        return web.Response(
            body=data,
            headers={'Content-Type': 'application/octet-stream'}
        )


class Node:
    """Main IPFS-like node - Compatible with C implementation."""
    
    def __init__(self, data_dir: str = None, udp_port: int = IPFS_DEFAULT_UDP_PORT,
                 tcp_port: int = IPFS_DEFAULT_TCP_PORT, 
                 http_port: int = IPFS_DEFAULT_HTTP_PORT,
                 http_host: str = '127.0.0.1'):
        # Generate node ID
        self.node_id = secrets.token_bytes(32)
        
        # Initialize components
        self.storage = Storage(data_dir)
        self.network = Network(self.node_id, udp_port, tcp_port)
        self.network.storage = self.storage  # Connect storage to network
        self.http = HTTPGateway(self.storage, self.network, http_host, http_port)
        self.runner = None  # For HTTP cleanup
        
        self.running = False
    
    def start(self):
        """Start all components."""
        logging.info(f"Node ID: {self.node_id.hex()[:16]}...")
        logging.info(f"Data directory: {self.storage.base_path}")
        logging.info(f"UDP port: {self.network.udp_port}, TCP port: {self.network.tcp_port}")
        
        self.network.start()
        
        # Start HTTP in event loop - pass node reference for API access
        if AIOHTTP_AVAILABLE:
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            self.http.node = self  # Give HTTP access to full node
            loop.run_until_complete(self.http.start())
            
            def run_loop():
                loop.run_forever()
            
            threading.Thread(target=run_loop, daemon=True).start()
        
        self.running = True
        logging.info("Node started - ready for P2P connections")
    
    def stop(self):
        """Stop all components."""
        self.running = False
        self.network.stop()
        
        if AIOHTTP_AVAILABLE and self.runner:
            try:
                loop = asyncio.get_event_loop()
                if loop.is_running():
                    # Schedule stop for later
                    asyncio.ensure_future(self.http.stop())
                else:
                    loop.run_until_complete(self.http.stop())
            except RuntimeError:
                # No running loop
                pass
        
        logging.info("Node stopped")
    
    def add(self, filepath: str) -> bytes:
        """Add a file to the node and announce to P2P network."""
        logging.info(f"Adding file: {filepath}")
        
        # Chunk and hash
        manifest = chunk_file(filepath)
        logging.info(f"Chunked into {manifest.block_count} blocks")
        logging.info(f"CID: {manifest.cid.hex()}")
        
        # Store blocks
        data = Path(filepath).read_bytes()
        for i, entry in enumerate(manifest.entries):
            start = i * IPFS_CHUNK_SIZE
            end = start + entry.size
            chunk = data[start:end]
            self.storage.put(entry.hash, chunk)
        
        # Store manifest
        self.storage.put_manifest(manifest)
        
        # Announce to P2P network
        if self.network.running:
            logging.info("Announcing blocks to P2P network...")
            for entry in manifest.entries:
                self.network.provide(entry.hash)
            self.network.provide(manifest.cid)  # Also announce manifest
        
        return manifest.cid
    
    def get(self, cid: bytes, output_path: str) -> bool:
        """Get a file by CID - fetches from P2P network if not local."""
        logging.info(f"Getting CID: {cid.hex()[:16]}...")
        
        # Get manifest
        manifest = self.storage.get_manifest(cid)
        if not manifest:
            logging.error("Manifest not found locally")
            # Would fetch manifest from network here
            return False
        
        logging.info(f"Loading {manifest.block_count} blocks...")
        
        # Assemble file
        data = b''
        for entry in manifest.entries:
            # Try local storage first
            block = self.storage.get(entry.hash)
            if block:
                logging.debug(f"Block {entry.index} found locally")
                data += block
            else:
                # Fetch from P2P network
                logging.info(f"Block {entry.index} not local, fetching from network...")
                
                # Find providers via DHT
                providers = self.network.find_value(entry.hash)
                
                # Try each provider
                fetched = False
                for provider in providers:
                    block = self.network.get_block(provider, entry.hash)
                    if block:
                        # Verify hash
                        if blake2b_hash(block) == entry.hash:
                            logging.debug(f"Block {entry.index} fetched from {provider.addr}")
                            self.storage.put(entry.hash, block)  # Cache locally
                            data += block
                            fetched = True
                            break
                
                if not fetched:
                    logging.error(f"Block {entry.index} not available from network")
                    return False
        
        # Write output
        Path(output_path).write_bytes(data)
        logging.info(f"File saved to: {output_path}")
        return True
    
    def run(self):
        """Run main loop."""
        try:
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            pass


def main():
    """CLI entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description='A.E.T.H.E.R. IPFS Node')
    subparsers = parser.add_subparsers(dest='command')
    
    # Daemon
    daemon_parser = subparsers.add_parser('daemon', help='Start daemon')
    daemon_parser.add_argument('--port', type=int, default=8080)
    daemon_parser.add_argument('--udp-port', type=int, default=4001)
    daemon_parser.add_argument('--tcp-port', type=int, default=4002)
    daemon_parser.add_argument('--data-dir', type=str)
    
    # Add
    add_parser = subparsers.add_parser('add', help='Add file')
    add_parser.add_argument('file', type=str)
    
    # Get
    get_parser = subparsers.add_parser('get', help='Get file')
    get_parser.add_argument('cid', type=str)
    get_parser.add_argument('output', type=str, nargs='?')
    
    # Connect to peer
    connect_parser = subparsers.add_parser('connect', help='Connect to peer')
    connect_parser.add_argument('host', type=str)
    connect_parser.add_argument('--udp-port', type=int, default=4001)
    connect_parser.add_argument('--tcp-port', type=int, default=4002)
    
    # Bootstrap (manual peer addition)
    bootstrap_parser = subparsers.add_parser('bootstrap', help='Manually add peer to routing table')
    bootstrap_parser.add_argument('peer', type=str, help='Peer address (IP:port)')
    
    # Help
    subparsers.add_parser('help', help='Show help')
    
    args = parser.parse_args()
    
    if args.command == 'daemon':
        node = Node(
            data_dir=args.data_dir,
            udp_port=args.udp_port,
            tcp_port=args.tcp_port,
            http_port=args.port
        )
        node.start()
        print(f"\nNode running. HTTP: http://127.0.0.1:{args.port}")
        print("Press Ctrl+C to stop.\n")
        node.run()
        node.stop()
    
    elif args.command == 'add':
        node = Node()
        node.start()
        cid = node.add(args.file)
        print(f"Added file. CID: {cid.hex()}")
        node.stop()
    
    elif args.command == 'get':
        node = Node()
        node.start()
        cid = bytes.fromhex(args.cid)
        output = args.output or 'output'
        success = node.get(cid, output)
        node.stop()
        sys.exit(0 if success else 1)
    
    elif args.command == 'connect':
        # Connect to a peer (e.g., C implementation node)
        node = Node(udp_port=40011, tcp_port=40012)  # Use different ports for client
        node.start()
        
        # Add peer to routing table
        from aether.ipfs import Peer, Endpoint
        peer_id = secrets.token_bytes(32)  # We'll learn real ID from PONG
        endpoint = Endpoint(args.host, args.udp_port, args.tcp_port)
        peer = Peer(peer_id, endpoint)
        node.network.routing_table.add_peer(peer)
        
        # Send PING to discover
        logging.info(f"Pinging {args.host}:{args.udp_port}...")
        node.network._send_packet((args.host, args.udp_port), node.network.PING)
        
        print(f"Connected to peer at {args.host}:{args.udp_port}")
        print(f"Local node ID: {node.node_id.hex()[:16]}...")
        print(f"Peers in routing table: {node.network.routing_table.total_peers}")
        print("\nPress Ctrl+C to disconnect")
        
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        
        node.stop()
    
    elif args.command == 'bootstrap':
        # Manual bootstrap - add peer directly to routing table
        node = Node(udp_port=40011, tcp_port=40012)  # Use different ports
        node.start()
        
        # Parse peer address
        peer_addr = args.peer
        if ':' in peer_addr:
            host, port = peer_addr.rsplit(':', 1)
            port = int(port)
        else:
            host = peer_addr
            port = 4001
        
        # Add peer to routing table
        from aether.ipfs import Peer, Endpoint
        peer_id = secrets.token_bytes(32)
        endpoint = Endpoint(host, port, node.network.tcp_port)
        peer = Peer(peer_id, endpoint)
        node.network.routing_table.add_peer(peer)
        
        # Send HELLO to initiate connection
        node.network._send_helo_broadcast()
        
        print(f"Bootstrap peer added: {host}:{port}")
        print(f"Local node ID: {node.node_id.hex()[:16]}...")
        print(f"Peers in routing table: {node.network.routing_table.total_peers}")
        print("\nPress Ctrl+C to disconnect")
        
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        
        node.stop()
    
    else:
        parser.print_help()
        print("\nCommands:")
        print("  daemon              Start the IPFS node daemon")
        print("  add <file>          Add a file to the node")
        print("  get <cid> [output]  Get a file by CID")
        print("  connect <host>      Connect to a peer")
        print("  bootstrap <ip:port> Manually add peer to routing table")
        print("  help                Show this help")


if __name__ == '__main__':
    main()
