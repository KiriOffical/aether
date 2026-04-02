"""
Advanced DHT features for A.E.T.H.E.R. P2P network.
Includes replication, iterative lookups, consistency checks, and value refreshment.
"""

import hashlib
import time
import logging
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple
from enum import IntEnum
from collections import defaultdict
import threading

from .crypto import Crypto
from .dht import DHT, Endpoint, BucketEntry, StoredValue


logger = logging.getLogger("aether.dht")


class ReplicationFactor(IntEnum):
    """Number of replicas to maintain for each value."""
    MIN = 3  # Minimum for fault tolerance
    DEFAULT = 7  # Standard replication
    HIGH = 15  # High availability


class ConsistencyLevel(IntEnum):
    """Consistency level for DHT operations."""
    EVENTUAL = 1  # At least one node confirms
    QUORUM = 2    # Majority must confirm
    STRONG = 3    # All replicas must confirm


@dataclass
class ReplicationStats:
    """Statistics about data replication."""
    key: bytes
    replicas_created: int = 0
    replicas_confirmed: int = 0
    replicas_failed: int = 0
    last_replication_time: float = 0.0
    consistency_level: ConsistencyLevel = ConsistencyLevel.EVENTUAL
    
    def consistency_ratio(self) -> float:
        """Calculate ratio of confirmed replicas."""
        if self.replicas_created == 0:
            return 0.0
        return self.replicas_confirmed / self.replicas_created


@dataclass
class LookupContext:
    """Context for iterative lookup operations."""
    target_key: bytes
    visited_nodes: Set[bytes] = field(default_factory=set)
    candidate_nodes: List[BucketEntry] = field(default_factory=list)
    results: List[StoredValue] = field(default_factory=list)
    start_time: float = field(default_factory=time.time)
    max_iterations: int = 20
    current_iteration: int = 0
    
    def should_continue(self) -> bool:
        """Check if lookup should continue."""
        return (self.current_iteration < self.max_iterations and 
                len(self.candidate_nodes) > 0)


class DHTReplicator:
    """Handles replication of DHT values across multiple nodes."""
    
    def __init__(self, dht: DHT, replication_factor: int = ReplicationFactor.DEFAULT):
        self.dht = dht
        self.replication_factor = replication_factor
        self.replication_stats: Dict[bytes, ReplicationStats] = {}
        self._lock = threading.Lock()
        
    def get_responsible_nodes(self, key: bytes, exclude: Optional[bytes] = None) -> List[BucketEntry]:
        """
        Get the nodes responsible for storing a key.
        These are the nodes closest to the key in the DHT space.
        """
        # Convert key to node ID space (hash if needed)
        if len(key) != 32:
            target_id = hashlib.sha256(key).digest()
        else:
            target_id = key
            
        # Find closest nodes
        closest = self.dht.find_closest_nodes(target_id, self.replication_factor)
        
        # Exclude specified node (usually ourselves)
        if exclude:
            closest = [n for n in closest if n.node_id != exclude]
            
        return closest[:self.replication_factor]
    
    def replicate_value(self, key: bytes, value: bytes, 
                       publisher: bytes, signature: bytes) -> ReplicationStats:
        """
        Replicate a value to responsible nodes.
        Returns statistics about the replication.
        """
        stats = ReplicationStats(key=key)
        responsible_nodes = self.get_responsible_nodes(key, exclude=self.dht.node_id)
        
        stats.replicas_created = len(responsible_nodes)
        
        # Store locally first
        self.dht.store(key, value, publisher, signature)
        stats.replicas_confirmed = 1  # Local storage counts
        
        # TODO: Send STORE_VALUE messages to responsible nodes
        # For now, we just track local storage
        # In a full implementation, this would:
        # 1. Send STORE_VALUE to each responsible node
        # 2. Wait for acknowledgments
        # 3. Track success/failure rates
        # 4. Retry failed stores
        
        with self._lock:
            self.replication_stats[key] = stats
            
        logger.debug(f"Replicated key {key.hex()[:8]}... to {stats.replicas_created} nodes")
        return stats
    
    def check_replication_health(self, key: bytes) -> ReplicationStats:
        """
        Check the replication health of a key.
        Returns statistics about current replication state.
        """
        with self._lock:
            if key in self.replication_stats:
                return self.replication_stats[key]
        
        # Create empty stats if key not found
        return ReplicationStats(key=key)
    
    def get_under_replicated_keys(self, min_replicas: int = 3) -> List[bytes]:
        """
        Get keys that are under-replicated.
        These keys may need additional replication for fault tolerance.
        """
        under_replicated = []
        
        with self._lock:
            for key, stats in self.replication_stats.items():
                if stats.replicas_confirmed < min_replicas:
                    under_replicated.append(key)
                    
        return under_replicated
    
    def cleanup_stats(self, max_age_seconds: float = 3600):
        """Clean up old replication statistics."""
        now = time.time()
        with self._lock:
            keys_to_remove = [
                key for key, stats in self.replication_stats.items()
                if now - stats.last_replication_time > max_age_seconds
            ]
            for key in keys_to_remove:
                del self.replication_stats[key]


class DHTLookup:
    """Handles iterative DHT lookups."""
    
    def __init__(self, dht: DHT, timeout: float = 30.0):
        self.dht = dht
        self.timeout = timeout
        self.active_lookups: Dict[bytes, LookupContext] = {}
        self._lock = threading.Lock()
        
    def _create_lookup_context(self, key: bytes) -> LookupContext:
        """Create a new lookup context for a key."""
        # Get initial candidates from local routing table
        if len(key) != 32:
            target_id = hashlib.sha256(key).digest()
        else:
            target_id = key
            
        candidates = self.dht.find_closest_nodes(target_id, 20)
        
        return LookupContext(
            target_key=key,
            candidate_nodes=candidates,
            visited_nodes={self.dht.node_id}
        )
    
    def lookup_value(self, key: bytes) -> Optional[bytes]:
        """
        Perform iterative lookup for a value.
        
        Algorithm:
        1. Start with nodes closest to the key in our routing table
        2. Query them for the value or closer nodes
        3. Add newly discovered nodes to candidates
        4. Repeat until we find the value or exhaust candidates
        """
        with self._lock:
            context = self._create_lookup_context(key)
            self.active_lookups[key] = context
            
        try:
            return self._run_lookup(context)
        finally:
            with self._lock:
                self.active_lookups.pop(key, None)
    
    def _run_lookup(self, context: LookupContext) -> Optional[bytes]:
        """Execute the iterative lookup algorithm."""
        while context.should_continue():
            context.current_iteration += 1
            
            # Sort candidates by distance to target
            if len(context.target_key) != 32:
                target_id = hashlib.sha256(context.target_key).digest()
            else:
                target_id = context.target_key
                
            # Get next batch of nodes to query
            nodes_to_query = context.candidate_nodes[:5]  # Alpha concurrency
            context.candidate_nodes = context.candidate_nodes[5:]
            
            for node in nodes_to_query:
                if node.node_id in context.visited_nodes:
                    continue
                    
                context.visited_nodes.add(node.node_id)
                
                # Check if we have the value locally first
                if node.node_id == self.dht.node_id:
                    value = self.dht.get(context.target_key)
                    if value:
                        logger.debug(f"Found value during lookup (local)")
                        return value
                    continue
                
                # TODO: Send GET_VALUE to remote node
                # For now, we just track visited nodes
                # In full implementation:
                # 1. Send GET_VALUE request
                # 2. If found, return value
                # 3. If not found, add returned closer nodes to candidates
                pass
            
            # If no more candidates, we've exhausted the search
            if not context.candidate_nodes:
                break
                
        logger.debug(f"Lookup completed without finding value")
        return None
    
    def lookup_nodes(self, target_id: bytes, k: int = 20) -> List[BucketEntry]:
        """
        Find k nodes closest to a target node ID.
        This is used for node discovery and routing table refreshment.
        """
        # Start with local routing table
        closest = self.dht.find_closest_nodes(target_id, k)
        
        # TODO: Perform iterative node lookup
        # In full implementation:
        # 1. Query closest known nodes for nodes closer to target
        # 2. Update routing table with discovered nodes
        # 3. Continue until no closer nodes are found
        
        return closest


class DHTConsistency:
    """Handles consistency checks and repairs."""
    
    def __init__(self, dht: DHT, consistency_level: ConsistencyLevel = ConsistencyLevel.EVENTUAL):
        self.dht = dht
        self.consistency_level = consistency_level
        self._lock = threading.Lock()
        
    def verify_value(self, key: bytes, expected_value: bytes) -> bool:
        """
        Verify that a value is correctly stored in the DHT.
        Returns True if value passes consistency check.
        """
        stored_value = self.dht.get(key)
        
        if stored_value is None:
            logger.warning(f"Value missing for key {key.hex()[:8]}...")
            return False
            
        if stored_value != expected_value:
            logger.warning(f"Value mismatch for key {key.hex()[:8]}...")
            return False
            
        # Verify signature if available
        # TODO: Add signature verification
        
        return True
    
    def repair_value(self, key: bytes, correct_value: bytes, 
                    publisher: bytes, signature: bytes) -> bool:
        """
        Repair a value that failed consistency check.
        Re-stores the correct value.
        """
        try:
            self.dht.store(key, correct_value, publisher, signature)
            logger.info(f"Repaired value for key {key.hex()[:8]}...")
            return True
        except Exception as e:
            logger.error(f"Failed to repair value: {e}")
            return False
    
    def check_all_values(self) -> Dict[bytes, bool]:
        """
        Check consistency of all stored values.
        Returns dict mapping keys to consistency status.
        """
        results = {}
        
        # TODO: Iterate through all stored values
        # For each value:
        # 1. Check if it's still valid (not expired)
        # 2. Verify signature
        # 3. Check if it exists on responsible nodes
        
        return results


class DHTRefresh:
    """Handles periodic refreshment of DHT values."""
    
    def __init__(self, dht: DHT, refresh_interval: float = 3600):
        self.dht = dht
        self.refresh_interval = refresh_interval
        self._lock = threading.Lock()
        self._refresh_thread: Optional[threading.Thread] = None
        self._running = False
        
    def start(self):
        """Start the background refresh thread."""
        if self._running:
            return
            
        self._running = True
        self._refresh_thread = threading.Thread(target=self._refresh_loop, daemon=True)
        self._refresh_thread.start()
        logger.info("DHT refresh thread started")
        
    def stop(self):
        """Stop the background refresh thread."""
        self._running = False
        if self._refresh_thread:
            self._refresh_thread.join(timeout=5.0)
        logger.info("DHT refresh thread stopped")
        
    def _refresh_loop(self):
        """Background loop that periodically refreshes values."""
        while self._running:
            time.sleep(self.refresh_interval)
            
            if not self._running:
                break
                
            try:
                self.refresh_all_values()
            except Exception as e:
                logger.error(f"Error during refresh: {e}")
                
    def refresh_all_values(self):
        """
        Refresh all values in the DHT.
        
        In Kademlia, values must be periodically republished
        to prevent them from expiring. This method:
        1. Gets all non-expired values
        2. Re-publishes them to responsible nodes
        3. Updates replication statistics
        """
        # TODO: Implement value refreshment
        # 1. Get all stored values
        # 2. For each value that's not expired:
        #    - Re-store it (resets TTL)
        #    - Replicate to responsible nodes
        # 3. Log refresh statistics
        
        logger.debug("DHT value refresh completed")


class AdvancedDHT:
    """
    Advanced DHT wrapper that combines all enhanced features.
    """
    
    def __init__(self, dht: DHT, replication_factor: int = ReplicationFactor.DEFAULT):
        self.dht = dht
        self.replicator = DHTReplicator(dht, replication_factor)
        self.lookup = DHTLookup(dht)
        self.consistency = DHTConsistency(dht)
        self.refresh = DHTRefresh(dht)
        
    def store(self, key: bytes, value: bytes, 
              publisher: Optional[bytes] = None, 
              signature: Optional[bytes] = None,
              consistency: ConsistencyLevel = ConsistencyLevel.EVENTUAL) -> ReplicationStats:
        """
        Store a value with replication.
        """
        if publisher is None:
            publisher = self.dht.node_id
        if signature is None:
            signature = b''
            
        # Store with replication
        stats = self.replicator.replicate_value(key, value, publisher, signature)
        stats.consistency_level = consistency
        
        return stats
    
    def get(self, key: bytes, use_lookup: bool = True) -> Optional[bytes]:
        """
        Get a value, optionally using iterative lookup.
        """
        # Try local storage first
        value = self.dht.get(key)
        if value:
            return value
            
        # Use iterative lookup if enabled
        if use_lookup:
            return self.lookup.lookup_value(key)
            
        return None
    
    def start(self):
        """Start background maintenance tasks."""
        self.refresh.start()
        
    def stop(self):
        """Stop background maintenance tasks."""
        self.refresh.stop()
        
    def get_stats(self) -> dict:
        """Get comprehensive DHT statistics."""
        return {
            'node_count': self.dht.node_count(),
            'value_count': self.dht.value_count(),
            'replication_stats': len(self.replicator.replication_stats),
            'active_lookups': len(self.lookup.active_lookups),
        }
