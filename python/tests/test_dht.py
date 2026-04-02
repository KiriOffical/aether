"""Tests for DHT module."""

import pytest
from aether.dht import DHT, Endpoint, BucketEntry, StoredValue, KBucket, RoutingTable
from aether.crypto import Crypto


class TestKBucket:
    """Test KBucket class."""
    
    def test_add_entry(self):
        """Test adding entries to bucket."""
        bucket = KBucket(max_size=5)
        node_id = Crypto.random_bytes(32)
        endpoint = Endpoint.from_ipv4("127.0.0.1", 8080)
        entry = BucketEntry(node_id, endpoint)
        
        result = bucket.add(entry)
        assert result is None
        assert len(bucket) == 1
    
    def test_add_duplicate(self):
        """Test adding duplicate entry updates existing."""
        bucket = KBucket(max_size=5)
        node_id = Crypto.random_bytes(32)
        endpoint1 = Endpoint.from_ipv4("127.0.0.1", 8080)
        endpoint2 = Endpoint.from_ipv4("127.0.0.1", 9090)
        
        entry1 = BucketEntry(node_id, endpoint1)
        entry2 = BucketEntry(node_id, endpoint2)
        
        bucket.add(entry1)
        result = bucket.add(entry2)
        
        assert result is None
        assert len(bucket) == 1
        assert bucket.entries[0].endpoint.port == 9090
    
    def test_bucket_full(self):
        """Test bucket returns oldest when full."""
        bucket = KBucket(max_size=2)
        
        node_id1 = Crypto.random_bytes(32)
        node_id2 = Crypto.random_bytes(32)
        node_id3 = Crypto.random_bytes(32)
        
        endpoint = Endpoint.from_ipv4("127.0.0.1", 8080)
        
        bucket.add(BucketEntry(node_id1, endpoint))
        import time
        time.sleep(0.01)
        bucket.add(BucketEntry(node_id2, endpoint))
        
        result = bucket.add(BucketEntry(node_id3, endpoint))
        
        assert result is not None
        assert result.node_id == node_id1


class TestDHT:
    """Test DHT class."""
    
    def test_store_get(self):
        """Test storing and retrieving values."""
        node_id = Crypto.random_bytes(32)
        endpoint = Endpoint.from_ipv4("127.0.0.1", 7821)
        dht = DHT(node_id, endpoint)
        
        key = b"test_key"
        value = b"test_value"
        publisher = Crypto.random_bytes(32)
        signature = b"signature"
        
        dht.store(key, value, publisher, signature)
        result = dht.get(key)
        
        assert result == value
    
    def test_get_nonexistent(self):
        """Test getting nonexistent key."""
        node_id = Crypto.random_bytes(32)
        endpoint = Endpoint.from_ipv4("127.0.0.1", 7821)
        dht = DHT(node_id, endpoint)
        
        result = dht.get(b"nonexistent")
        assert result is None
    
    def test_add_node(self):
        """Test adding nodes to routing table."""
        node_id = Crypto.random_bytes(32)
        endpoint = Endpoint.from_ipv4("127.0.0.1", 7821)
        dht = DHT(node_id, endpoint)
        
        other_node_id = Crypto.random_bytes(32)
        other_endpoint = Endpoint.from_ipv4("127.0.0.1", 8080)
        
        result = dht.add_node(other_node_id, other_endpoint)
        assert result is True
        assert dht.node_count() == 1
    
    def test_find_closest_nodes(self):
        """Test finding closest nodes."""
        node_id = Crypto.random_bytes(32)
        endpoint = Endpoint.from_ipv4("127.0.0.1", 7821)
        dht = DHT(node_id, endpoint)
        
        # Add some nodes
        for i in range(10):
            other_id = Crypto.random_bytes(32)
            other_ep = Endpoint.from_ipv4(f"127.0.0.{i}", 8080)
            dht.add_node(other_id, other_ep)
        
        target = Crypto.random_bytes(32)
        closest = dht.find_closest_nodes(target, k=5)
        
        assert len(closest) == 5
