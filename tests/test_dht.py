"""
Tests for Distributed Hash Table (DHT).
"""

import sys
import os
import time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from aether.dht import DHT, DHTEntry, DHTValue, KBucket, K_BUCKET_SIZE, DHT_VALUE_TTL_SECS
from aether.crypto import Crypto


class TestKBucket:
    """Test KBucket class"""

    def test_add_entry(self):
        """Test adding entry to bucket"""
        bucket = KBucket(size=5)
        entry = DHTEntry(
            node_id=Crypto.random_bytes(32),
            address="127.0.0.1",
            port=8080
        )
        assert bucket.add(entry)
        assert len(bucket.get_entries()) == 1

    def test_add_duplicate_updates(self):
        """Test adding duplicate entry updates it"""
        bucket = KBucket(size=5)
        node_id = Crypto.random_bytes(32)
        entry1 = DHTEntry(node_id=node_id, address="127.0.0.1", port=8080)
        entry2 = DHTEntry(node_id=node_id, address="127.0.0.1", port=9090)
        
        bucket.add(entry1)
        bucket.add(entry2)
        
        entries = bucket.get_entries()
        assert len(entries) == 1
        assert entries[0].port == 9090

    def test_bucket_full_removes_oldest(self):
        """Test bucket removes oldest when full"""
        bucket = KBucket(size=3)
        for i in range(5):
            entry = DHTEntry(
                node_id=Crypto.random_bytes(32),
                address="127.0.0.1",
                port=8080 + i
            )
            bucket.add(entry)
        
        entries = bucket.get_entries()
        assert len(entries) == 3

    def test_remove_entry(self):
        """Test removing entry from bucket"""
        bucket = KBucket(size=5)
        node_id = Crypto.random_bytes(32)
        entry = DHTEntry(node_id=node_id, address="127.0.0.1", port=8080)
        
        bucket.add(entry)
        bucket.remove(node_id)
        
        assert len(bucket.get_entries()) == 0


class TestDHT:
    """Test DHT class"""

    def test_create_dht(self):
        """Test DHT creation"""
        node_id = Crypto.random_bytes(32)
        dht = DHT(node_id)
        assert dht.node_id == node_id
        assert dht.node_count() == 0

    def test_add_node(self):
        """Test adding node to DHT"""
        node_id = Crypto.random_bytes(32)
        dht = DHT(node_id)
        
        target_id = Crypto.random_bytes(32)
        dht.add_node(target_id, "127.0.0.1", 8080)
        
        assert dht.node_count() == 1

    def test_add_self_ignored(self):
        """Test adding self node is ignored"""
        node_id = Crypto.random_bytes(32)
        dht = DHT(node_id)
        
        dht.add_node(node_id, "127.0.0.1", 8080)
        
        assert dht.node_count() == 0

    def test_find_closest(self):
        """Test finding closest nodes"""
        node_id = Crypto.random_bytes(32)
        dht = DHT(node_id)
        
        # Add some nodes
        for i in range(10):
            target_id = Crypto.random_bytes(32)
            dht.add_node(target_id, "127.0.0.1", 8080 + i)
        
        # Find closest to random target
        search_id = Crypto.random_bytes(32)
        closest = dht.find_closest(search_id, k=5)
        
        assert len(closest) == 5

    def test_store_get(self):
        """Test storing and retrieving values"""
        node_id = Crypto.random_bytes(32)
        dht = DHT(node_id)
        
        key = b"test_key"
        value = b"test_value"
        publisher = Crypto.random_bytes(32)
        signature = Crypto.random_bytes(64)
        
        dht.store(key, value, publisher, signature)
        
        retrieved = dht.get(key)
        assert retrieved == value

    def test_get_nonexistent(self):
        """Test getting nonexistent key"""
        node_id = Crypto.random_bytes(32)
        dht = DHT(node_id)
        
        retrieved = dht.get(b"nonexistent")
        assert retrieved is None

    def test_value_count(self):
        """Test value count"""
        node_id = Crypto.random_bytes(32)
        dht = DHT(node_id)
        
        assert dht.value_count() == 0
        
        for i in range(5):
            key = f"key{i}".encode()
            value = f"value{i}".encode()
            dht.store(key, value, Crypto.random_bytes(32), Crypto.random_bytes(64))
        
        assert dht.value_count() == 5

    def test_cleanup_expired(self):
        """Test cleanup of expired values"""
        node_id = Crypto.random_bytes(32)
        dht = DHT(node_id)
        
        # Store with very short TTL
        key = b"short_lived"
        value = b"value"
        now = time.time()
        
        dht_value = DHTValue(
            key=key,
            value=value,
            publisher=Crypto.random_bytes(32),
            created_at=now - 100,
            expires_at=now - 1,  # Already expired
            signature=Crypto.random_bytes(64)
        )
        dht.storage[key] = dht_value
        
        dht.cleanup()
        
        assert dht.get(key) is None
