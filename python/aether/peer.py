"""
Peer management for A.E.T.H.E.R.
"""

import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set
from enum import Enum
import random

from .crypto import Crypto
from .dht import Endpoint


class PeerState(Enum):
    """Peer connection state."""
    CONNECTING = 0
    HANDSHAKING = 1
    CONNECTED = 2
    DISCONNECTING = 3
    DISCONNECTED = 4


class DisconnectReason(Enum):
    """Disconnect reason."""
    GRACEFUL = 0
    TIMEOUT = 1
    PROTOCOL_ERROR = 2
    BLACKLISTED = 3
    MAX_CONNECTIONS = 4
    OTHER = 5


@dataclass
class Peer:
    """Peer information."""
    node_id: bytes
    remote_addr: str
    remote_port: int
    listen_addr: Optional[str] = None
    listen_port: Optional[int] = None
    protocols: List[str] = field(default_factory=list)
    state: PeerState = PeerState.CONNECTING
    connected_at: Optional[float] = None
    last_activity: float = field(default_factory=time.time)
    latency: Optional[float] = None  # in milliseconds
    trust_score: int = 50  # 0-100
    
    DEFAULT_TTL = 24 * 60 * 60  # 24 hours
    
    def is_active(self) -> bool:
        return self.state == PeerState.CONNECTED
    
    def is_stale(self) -> bool:
        return (time.time() - self.last_activity) > self.DEFAULT_TTL
    
    def mark_active(self):
        self.last_activity = time.time()
    
    def set_connected(self, protocols: List[str], 
                      listen_addr: Optional[str] = None,
                      listen_port: Optional[int] = None):
        self.state = PeerState.CONNECTED
        self.connected_at = time.time()
        self.protocols = protocols
        self.listen_addr = listen_addr
        self.listen_port = listen_port
    
    def to_endpoint(self) -> Optional[Endpoint]:
        """Convert to Endpoint for PEX."""
        if self.listen_addr and self.listen_port:
            return Endpoint.from_ipv4(self.listen_addr, self.listen_port)
        if self.remote_addr and self.remote_port:
            return Endpoint.from_ipv4(self.remote_addr, self.remote_port)
        return None


class PeerManager:
    """Peer manager for tracking all known peers."""
    
    def __init__(self, max_connections: int = 10000):
        self.peers: Dict[str, Peer] = {}  # keyed by node_id hex
        self.by_address: Dict[str, str] = {}  # addr -> node_id
        self.blacklist: Set[str] = set()
        self.blacklist_addr: Set[str] = set()
        self.max_connections = max_connections
    
    @staticmethod
    def _node_id_to_hex(node_id: bytes) -> str:
        return node_id.hex()
    
    def add_peer(self, peer: Peer) -> None:
        """Add or update a peer."""
        node_id_hex = self._node_id_to_hex(peer.node_id)
        
        # Remove old address mapping if exists
        if node_id_hex in self.peers:
            old_peer = self.peers[node_id_hex]
            old_addr = f"{old_peer.remote_addr}:{old_peer.remote_port}"
            if old_addr in self.by_address:
                del self.by_address[old_addr]
        
        # Add/update peer
        self.peers[node_id_hex] = peer
        
        # Update address mapping
        addr_key = f"{peer.remote_addr}:{peer.remote_port}"
        self.by_address[addr_key] = node_id_hex
    
    def get_peer(self, node_id: bytes) -> Optional[Peer]:
        """Get peer by node ID."""
        return self.peers.get(self._node_id_to_hex(node_id))
    
    def get_peer_by_addr(self, addr: str, port: int) -> Optional[Peer]:
        """Get peer by address."""
        addr_key = f"{addr}:{port}"
        node_id_hex = self.by_address.get(addr_key)
        if node_id_hex:
            return self.peers.get(node_id_hex)
        return None
    
    def remove_peer(self, node_id: bytes) -> Optional[Peer]:
        """Remove peer by node ID."""
        node_id_hex = self._node_id_to_hex(node_id)
        peer = self.peers.pop(node_id_hex, None)
        if peer:
            addr_key = f"{peer.remote_addr}:{peer.remote_port}"
            self.by_address.pop(addr_key, None)
        return peer
    
    def disconnect_peer(self, node_id: bytes) -> None:
        """Mark peer as disconnected."""
        peer = self.get_peer(node_id)
        if peer:
            peer.state = PeerState.DISCONNECTED
            peer.connected_at = None
    
    def blacklist(self, node_id: bytes) -> None:
        """Add node to blacklist."""
        self.blacklist.add(self._node_id_to_hex(node_id))
    
    def blacklist_address(self, addr: str) -> None:
        """Add address to blacklist."""
        self.blacklist_addr.add(addr)
    
    def is_blacklisted(self, node_id: bytes) -> bool:
        """Check if node is blacklisted."""
        return self._node_id_to_hex(node_id) in self.blacklist
    
    def is_address_blacklisted(self, addr: str) -> bool:
        """Check if address is blacklisted."""
        return addr in self.blacklist_addr
    
    def active_count(self) -> int:
        """Get count of active connections."""
        return sum(1 for p in self.peers.values() if p.is_active())
    
    def can_accept(self) -> bool:
        """Check if we can accept more connections."""
        return self.active_count() < self.max_connections
    
    def get_random_peers(self, limit: int) -> List[Endpoint]:
        """Get random active peers for PEX."""
        active = [p for p in self.peers.values() if p.is_active()]
        if not active:
            return []
        
        selected = random.sample(active, min(limit, len(active)))
        endpoints = []
        for peer in selected:
            ep = peer.to_endpoint()
            if ep:
                endpoints.append(ep)
        return endpoints
    
    def get_closest_peers(self, target: bytes, k: int) -> List[Peer]:
        """Get closest peers to a target node ID."""
        active = [p for p in self.peers.values() if p.is_active()]
        
        # Calculate distances
        peers_with_dist = []
        for peer in active:
            dist = Crypto.distance(peer.node_id, target)
            peers_with_dist.append((peer, dist))
        
        # Sort by distance
        peers_with_dist.sort(key=lambda x: x[1])
        
        return [p for p, _ in peers_with_dist[:k]]
    
    def active_peers(self) -> List[Peer]:
        """Get all active peers."""
        return [p for p in self.peers.values() if p.is_active()]
    
    def evict_stale(self) -> List[bytes]:
        """Evict stale peers. Returns list of evicted node IDs."""
        evicted = []
        for node_id_hex, peer in list(self.peers.items()):
            if peer.is_stale() and not peer.is_active():
                evicted.append(peer.node_id)
                self.remove_peer(peer.node_id)
        return evicted
    
    def update_latency(self, node_id: bytes, latency: float) -> None:
        """Update peer latency measurement."""
        peer = self.get_peer(node_id)
        if peer:
            peer.latency = latency
    
    def adjust_trust(self, node_id: bytes, delta: int) -> None:
        """Adjust trust score."""
        peer = self.get_peer(node_id)
        if peer:
            peer.trust_score = max(0, min(100, peer.trust_score + delta))
