"""
Distributed Hash Table (DHT) implementation using Kademlia-style routing.
"""

import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple
from collections import OrderedDict


@dataclass
class Endpoint:
    """Network endpoint (IP address + port)."""
    address: str
    port: int
    is_ipv6: bool = False
    
    @classmethod
    def from_ipv4(cls, ip: str, port: int) -> 'Endpoint':
        return cls(address=ip, port=port, is_ipv6=False)
    
    @classmethod
    def from_ipv6(cls, ip: str, port: int) -> 'Endpoint':
        return cls(address=ip, port=port, is_ipv6=True)
    
    def __str__(self) -> str:
        if self.is_ipv6:
            return f"[{self.address}]:{self.port}"
        return f"{self.address}:{self.port}"


@dataclass
class BucketEntry:
    """Entry in a K-bucket."""
    node_id: bytes
    endpoint: Endpoint
    last_seen: float = field(default_factory=time.time)
    is_self: bool = False
    
    def mark_seen(self):
        self.last_seen = time.time()


@dataclass
class StoredValue:
    """Value stored in the DHT."""
    key: bytes
    value: bytes
    publisher: bytes
    signature: bytes
    created_at: float = field(default_factory=time.time)
    expires_at: float = field(default_factory=lambda: time.time() + 24 * 60 * 60)  # 24 hours
    
    def is_expired(self) -> bool:
        return time.time() > self.expires_at


class KBucket:
    """A K-bucket containing up to K node entries."""
    
    def __init__(self, max_size: int = 20):
        self.entries: List[BucketEntry] = []
        self.max_size = max_size
    
    def add(self, entry: BucketEntry) -> Optional[BucketEntry]:
        """
        Add or update an entry.
        Returns the oldest entry if bucket is full, None otherwise.
        """
        # Check if entry already exists
        for existing in self.entries:
            if existing.node_id == entry.node_id:
                existing.mark_seen()
                existing.endpoint = entry.endpoint
                return None
        
        # If bucket is full, return the oldest entry
        if len(self.entries) >= self.max_size:
            return self.oldest()
        
        self.entries.append(entry)
        return None
    
    def remove(self, node_id: bytes) -> Optional[BucketEntry]:
        """Remove an entry by node ID."""
        for i, entry in enumerate(self.entries):
            if entry.node_id == node_id:
                return self.entries.pop(i)
        return None
    
    def oldest(self) -> Optional[BucketEntry]:
        """Get the oldest entry."""
        if not self.entries:
            return None
        return min(self.entries, key=lambda e: e.last_seen)
    
    def is_full(self) -> bool:
        return len(self.entries) >= self.max_size
    
    def __len__(self) -> int:
        return len(self.entries)


class RoutingTable:
    """Kademlia-style DHT routing table."""
    
    ID_BITS = 256
    
    def __init__(self, node_id: bytes, endpoint: Endpoint):
        self.node_id = node_id
        self.buckets: List[KBucket] = [KBucket(20) for _ in range(self.ID_BITS)]
        self.self_entry = BucketEntry(node_id, endpoint, is_self=True)
    
    @staticmethod
    def _bucket_index(distance: bytes) -> int:
        """Get the bucket index for a given distance."""
        for i in range(256):
            byte_idx = i // 8
            bit_idx = 7 - (i % 8)
            if distance[byte_idx] & (1 << bit_idx):
                return i
        return 0
    
    def add_node(self, node_id: bytes, endpoint: Endpoint) -> bool:
        """Add a node to the routing table."""
        if node_id == self.node_id:
            return True  # Don't add self
        
        from .crypto import Crypto
        distance = Crypto.distance(self.node_id, node_id)
        index = self._bucket_index(distance)
        
        entry = BucketEntry(node_id, endpoint)
        return self.buckets[index].add(entry) is None
    
    def remove_node(self, node_id: bytes) -> None:
        """Remove a node from the routing table."""
        if node_id == self.node_id:
            return
        
        from .crypto import Crypto
        distance = Crypto.distance(self.node_id, node_id)
        index = self._bucket_index(distance)
        self.buckets[index].remove(node_id)
    
    def find_closest(self, target: bytes, k: int) -> List[BucketEntry]:
        """Find the k closest nodes to a target."""
        from .crypto import Crypto
        
        all_nodes: List[Tuple[BucketEntry, bytes]] = []
        
        for bucket in self.buckets:
            for entry in bucket.entries:
                distance = Crypto.distance(entry.node_id, target)
                all_nodes.append((entry, distance))
        
        # Sort by distance
        all_nodes.sort(key=lambda x: x[1])
        
        # Return top k
        return [entry for entry, _ in all_nodes[:k]]
    
    def all_nodes(self) -> List[BucketEntry]:
        """Get all nodes in routing table."""
        nodes = []
        for bucket in self.buckets:
            nodes.extend(bucket.entries)
        return nodes
    
    def node_count(self) -> int:
        """Get count of nodes in routing table."""
        return sum(len(bucket) for bucket in self.buckets)


class DHTStorage:
    """DHT storage for key-value pairs."""
    
    def __init__(self, max_values: int = 100000):
        self.values: Dict[bytes, StoredValue] = {}
        self.max_values = max_values
    
    def store(self, value: StoredValue) -> None:
        """Store a value."""
        # Check if we need to evict
        if len(self.values) >= self.max_values and value.key not in self.values:
            # Evict expired values first
            expired = [k for k, v in self.values.items() if v.is_expired()]
            for key in expired:
                del self.values[key]
            
            # If still full, evict oldest
            if len(self.values) >= self.max_values:
                oldest_key = min(self.values.keys(), key=lambda k: self.values[k].created_at)
                del self.values[oldest_key]
        
        self.values[value.key] = value
    
    def get(self, key: bytes) -> Optional[StoredValue]:
        """Get a value by key."""
        value = self.values.get(key)
        if value and not value.is_expired():
            return value
        return None
    
    def cleanup(self) -> int:
        """Remove expired values. Returns count of removed values."""
        expired = [k for k, v in self.values.items() if v.is_expired()]
        for key in expired:
            del self.values[key]
        return len(expired)
    
    def __len__(self) -> int:
        return len(self.values)


class DHT:
    """Main DHT class combining routing and storage."""
    
    def __init__(self, node_id: bytes, endpoint: Endpoint, max_values: int = 100000):
        self.routing_table = RoutingTable(node_id, endpoint)
        self.storage = DHTStorage(max_values)
    
    def store(self, key: bytes, value: bytes, publisher: bytes, signature: bytes) -> None:
        """Store a value locally."""
        stored = StoredValue(key, value, publisher, signature)
        self.storage.store(stored)
    
    def get(self, key: bytes) -> Optional[bytes]:
        """Get a value locally."""
        stored = self.storage.get(key)
        return stored.value if stored else None
    
    def find_closest_nodes(self, target: bytes, k: int) -> List[BucketEntry]:
        """Find closest nodes to a target."""
        return self.routing_table.find_closest(target, k)
    
    def add_node(self, node_id: bytes, endpoint: Endpoint) -> bool:
        """Add a node to routing table."""
        return self.routing_table.add_node(node_id, endpoint)
    
    def remove_node(self, node_id: bytes) -> None:
        """Remove a node from routing table."""
        self.routing_table.remove_node(node_id)
    
    def cleanup(self) -> int:
        """Cleanup expired values."""
        return self.storage.cleanup()
    
    def node_count(self) -> int:
        """Get node count."""
        return self.routing_table.node_count()
    
    def value_count(self) -> int:
        """Get value count."""
        return len(self.storage)
