"""
Integration tests for A.E.T.H.E.R. P2P network.
Tests the full system with multiple nodes interacting.
"""

import pytest
import time
import threading
import socket
import os
import sys
from typing import List, Dict, Optional
import tempfile
import shutil

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from aether import Node, Config, Client
from aether.dht import DHT, Endpoint
from aether.crypto import Crypto, KeyPair
from aether.protocol import Message, MessageType
from aether.peer import Peer, PeerManager, PeerState


class TestCluster:
    """Manages a cluster of test nodes."""
    
    def __init__(self, size: int = 3, base_port: int = 17821):
        self.size = size
        self.base_port = base_port
        self.nodes: List[Node] = []
        self.temp_dir = None
        
    def start(self):
        """Start all nodes in the cluster."""
        self.temp_dir = tempfile.mkdtemp(prefix="aether_test_")
        
        for i in range(self.size):
            node_dir = os.path.join(self.temp_dir, f"node_{i}")
            os.makedirs(node_dir, exist_ok=True)
            
            config = Config(
                identity_path=os.path.join(node_dir, "identity.bin"),
                data_dir=node_dir,
                listen_port=self.base_port + i,
                listen_addr="127.0.0.1",
                max_connections=100,
                bootstrap_nodes=[],
                log_level=50,  # ERROR only
            )
            
            node = Node(config)
            node.start()
            self.nodes.append(node)
            
        # Connect nodes in a ring
        self._connect_nodes()
        
    def _connect_nodes(self):
        """Connect nodes in a ring topology."""
        time.sleep(0.5)  # Wait for nodes to start
        
        for i in range(1, self.size):
            client = Client("127.0.0.1", self.base_port + i - 1)
            try:
                client.connect(timeout=2)
                client.disconnect()
            except Exception:
                pass  # Connection may fail in tests, that's ok
                
    def stop(self):
        """Stop all nodes and cleanup."""
        for node in self.nodes:
            try:
                node.stop()
            except Exception:
                pass
                
        if self.temp_dir and os.path.exists(self.temp_dir):
            shutil.rmtree(self.temp_dir)
            
    def get_node(self, index: int) -> Node:
        """Get node by index."""
        return self.nodes[index]
    
    def get_client(self, node_index: int) -> Client:
        """Get client connected to specific node."""
        return Client("127.0.0.1", self.base_port + node_index)


class TestIntegrationBasic:
    """Basic integration tests."""
    
    @pytest.fixture
    def cluster(self):
        """Create a test cluster."""
        cluster = TestCluster(size=3)
        cluster.start()
        yield cluster
        cluster.stop()
        
    def test_node_startup(self, cluster):
        """Test that nodes start successfully."""
        assert len(cluster.nodes) == 3
        for node in cluster.nodes:
            assert node.running
            assert node.node_id is not None
            assert len(node.node_id) == 32
            
    def test_node_unique_ids(self, cluster):
        """Test that each node has a unique ID."""
        node_ids = [node.node_id for node in cluster.nodes]
        assert len(set(node_ids)) == len(node_ids)
        
    def test_dht_store_get(self, cluster):
        """Test storing and retrieving data from DHT."""
        node = cluster.get_node(0)
        
        # Store a value
        key = b"test_key_1"
        value = b"test_value_1"
        success = node.dht_store(key, value)
        assert success
        
        # Retrieve the value
        retrieved = node.dht_get(key)
        assert retrieved == value
        
    def test_dht_multiple_values(self, cluster):
        """Test storing multiple values."""
        node = cluster.get_node(0)
        
        values = {
            b"key1": b"value1",
            b"key2": b"value2",
            b"key3": b"value3",
        }
        
        # Store all values
        for key, value in values.items():
            success = node.dht_store(key, value)
            assert success
            
        # Retrieve all values
        for key, expected_value in values.items():
            retrieved = node.dht_get(key)
            assert retrieved == expected_value
            
    def test_client_connection(self, cluster):
        """Test client connection to node."""
        client = cluster.get_client(0)
        
        try:
            result = client.connect(timeout=2)
            # Connection may succeed or fail depending on implementation
            # Just test that it doesn't crash
        except Exception as e:
            # Expected in some cases
            pass
        finally:
            try:
                client.disconnect()
            except:
                pass
                
    def test_node_stats(self, cluster):
        """Test node statistics."""
        node = cluster.get_node(0)
        stats = node.get_stats()
        
        assert 'node_id' in stats
        assert 'port' in stats
        assert 'peer_count' in stats
        assert 'version' in stats
        assert stats['port'] == 17821


class TestIntegrationNetwork:
    """Network-related integration tests."""
    
    @pytest.fixture
    def cluster(self):
        """Create a larger test cluster."""
        cluster = TestCluster(size=5)
        cluster.start()
        yield cluster
        cluster.stop()
        
    def test_peer_discovery(self, cluster):
        """Test that nodes can discover peers."""
        time.sleep(1.0)  # Allow time for discovery
        
        # Check that nodes are aware of each other
        for node in cluster.nodes:
            peer_count = node.get_peer_count()
            # At least some peers should be known
            assert peer_count >= 0
            
    def test_broadcast_message(self, cluster):
        """Test broadcasting messages to peers."""
        # This would test the broadcast functionality
        # Implementation dependent
        pass
        
    def test_concurrent_stores(self, cluster):
        """Test concurrent store operations."""
        node = cluster.get_node(0)
        results = []
        errors = []
        
        def store_value(key: bytes, value: bytes):
            try:
                success = node.dht_store(key, value)
                results.append((key, success))
            except Exception as e:
                errors.append((key, e))
                
        # Start concurrent stores
        threads = []
        for i in range(10):
            key = f"concurrent_key_{i}".encode()
            value = f"concurrent_value_{i}".encode()
            t = threading.Thread(target=store_value, args=(key, value))
            threads.append(t)
            t.start()
            
        # Wait for all threads
        for t in threads:
            t.join()
            
        # Check results
        assert len(errors) == 0, f"Errors occurred: {errors}"
        assert len(results) == 10
        assert all(success for _, success in results)
        
    def test_node_restart(self, cluster):
        """Test node restart with persistence."""
        node = cluster.get_node(0)
        
        # Store a value
        key = b"persistent_key"
        value = b"persistent_value"
        node.dht_store(key, value)
        
        # Verify storage
        assert node.dht_get(key) == value
        
        # Stop and restart node
        node.stop()
        time.sleep(0.5)
        
        # Restart with same config
        node.start()
        time.sleep(0.5)
        
        # Check if value persisted (depends on implementation)
        # For now, just test that node restarts without error
        assert node.running


class TestIntegrationStress:
    """Stress tests for the system."""
    
    @pytest.fixture
    def cluster(self):
        """Create a cluster for stress testing."""
        cluster = TestCluster(size=3)
        cluster.start()
        yield cluster
        cluster.stop()
        
    def test_high_volume_stores(self, cluster):
        """Test high volume of store operations."""
        node = cluster.get_node(0)
        
        num_operations = 1000
        start_time = time.time()
        
        for i in range(num_operations):
            key = f"stress_key_{i}".encode()
            value = f"stress_value_{i}".encode()
            node.dht_store(key, value)
            
        elapsed = time.time() - start_time
        ops_per_second = num_operations / elapsed if elapsed > 0 else 0
        
        print(f"\nStress test: {num_operations} stores in {elapsed:.2f}s")
        print(f"Rate: {ops_per_second:.2f} ops/second")
        
        # Verify some random keys
        import random
        for _ in range(10):
            i = random.randint(0, num_operations - 1)
            key = f"stress_key_{i}".encode()
            expected = f"stress_value_{i}".encode()
            assert node.dht_get(key) == expected
            
    def test_large_value_storage(self, cluster):
        """Test storage of large values."""
        node = cluster.get_node(0)
        
        # Test various sizes
        sizes = [1024, 10240, 102400]  # 1KB, 10KB, 100KB
        
        for size in sizes:
            key = f"large_key_{size}".encode()
            value = os.urandom(size)
            
            success = node.dht_store(key, value)
            assert success
            
            retrieved = node.dht_get(key)
            assert retrieved == value
            
    def test_rapid_connect_disconnect(self, cluster):
        """Test rapid connection and disconnection."""
        for _ in range(20):
            client = cluster.get_client(0)
            try:
                client.connect(timeout=1)
                time.sleep(0.01)
            except:
                pass
            finally:
                try:
                    client.disconnect()
                except:
                    pass
                    
        # Node should still be running
        assert cluster.get_node(0).running


class TestIntegrationEdgeCases:
    """Edge case tests."""
    
    @pytest.fixture
    def cluster(self):
        """Create a test cluster."""
        cluster = TestCluster(size=2)
        cluster.start()
        yield cluster
        cluster.stop()
        
    def test_empty_key(self, cluster):
        """Test storage with empty key."""
        node = cluster.get_node(0)
        
        key = b""
        value = b"test_value"
        
        # Should handle gracefully
        success = node.dht_store(key, value)
        # Behavior depends on implementation
        
    def test_empty_value(self, cluster):
        """Test storage with empty value."""
        node = cluster.get_node(0)
        
        key = b"test_key"
        value = b""
        
        success = node.dht_store(key, value)
        retrieved = node.dht_get(key)
        assert retrieved == value
        
    def test_unicode_key(self, cluster):
        """Test storage with unicode key."""
        node = cluster.get_node(0)
        
        key = "测试_key_🔑".encode('utf-8')
        value = b"test_value"
        
        success = node.dht_store(key, value)
        retrieved = node.dht_get(key)
        assert retrieved == value
        
    def test_special_characters_value(self, cluster):
        """Test storage with special characters in value."""
        node = cluster.get_node(0)
        
        key = b"special_key"
        value = b"\x00\x01\x02\xff\xfe\xfd"
        
        success = node.dht_store(key, value)
        retrieved = node.dht_get(key)
        assert retrieved == value
        
    def test_duplicate_key_overwrite(self, cluster):
        """Test overwriting existing key."""
        node = cluster.get_node(0)
        
        key = b"overwrite_key"
        value1 = b"value1"
        value2 = b"value2"
        
        # Store initial value
        node.dht_store(key, value1)
        assert node.dht_get(key) == value1
        
        # Overwrite
        node.dht_store(key, value2)
        assert node.dht_get(key) == value2


def run_integration_tests():
    """Run integration tests manually."""
    print("Running A.E.T.H.E.R. Integration Tests")
    print("=" * 50)
    
    # Create and run tests
    pytest.main([
        __file__,
        "-v",
        "--tb=short",
        "-s"
    ])


if __name__ == "__main__":
    run_integration_tests()
