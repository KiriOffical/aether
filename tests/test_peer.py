"""
Tests for peer management.
"""

import sys
import os
import time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from aether.peer import Peer, PeerManager, PeerState
from aether.crypto import Crypto


class TestPeer:
    """Test Peer dataclass"""

    def test_create_peer(self):
        """Test peer creation"""
        node_id = Crypto.random_bytes(32)
        peer = Peer(node_id=node_id)
        
        assert peer.node_id == node_id
        assert peer.state == PeerState.CONNECTING
        assert peer.trust_score == 50

    def test_to_dict(self):
        """Test peer to_dict conversion"""
        node_id = Crypto.random_bytes(32)
        peer = Peer(
            node_id=node_id,
            remote_addr="127.0.0.1",
            remote_port=8080,
            state=PeerState.CONNECTED,
            trust_score=75
        )
        
        d = peer.to_dict()
        assert d['node_id'] == node_id.hex()
        assert d['remote_addr'] == "127.0.0.1"
        assert d['remote_port'] == 8080
        assert d['state'] == PeerState.CONNECTED.value
        assert d['trust_score'] == 75


class TestPeerManager:
    """Test PeerManager class"""

    def test_add_peer(self):
        """Test adding peer"""
        manager = PeerManager(max_connections=10)
        node_id = Crypto.random_bytes(32)
        peer = Peer(node_id=node_id)
        
        assert manager.add(peer)
        assert manager.active_count() == 0  # Not CONNECTED yet
        
        peer.state = PeerState.CONNECTED
        assert manager.active_count() == 1

    def test_add_blacklisted(self):
        """Test adding blacklisted peer fails"""
        manager = PeerManager()
        node_id = Crypto.random_bytes(32)
        peer = Peer(node_id=node_id)
        
        manager.blacklist_node(node_id)
        assert not manager.add(peer)

    def test_add_at_limit(self):
        """Test adding peer at connection limit"""
        manager = PeerManager(max_connections=2)
        
        for i in range(2):
            peer = Peer(node_id=Crypto.random_bytes(32))
            manager.add(peer)
        
        # Should fail at limit
        peer = Peer(node_id=Crypto.random_bytes(32))
        assert not manager.add(peer)

    def test_remove_peer(self):
        """Test removing peer"""
        manager = PeerManager()
        node_id = Crypto.random_bytes(32)
        peer = Peer(node_id=node_id)
        
        manager.add(peer)
        assert manager.get(node_id) is not None
        
        manager.remove(node_id)
        assert manager.get(node_id) is None

    def test_get_peer(self):
        """Test getting peer by ID"""
        manager = PeerManager()
        node_id = Crypto.random_bytes(32)
        peer = Peer(node_id=node_id)
        
        manager.add(peer)
        
        retrieved = manager.get(node_id)
        assert retrieved is not None
        assert retrieved.node_id == node_id

    def test_get_nonexistent_peer(self):
        """Test getting nonexistent peer"""
        manager = PeerManager()
        result = manager.get(Crypto.random_bytes(32))
        assert result is None

    def test_blacklist(self):
        """Test blacklisting peer"""
        manager = PeerManager()
        node_id = Crypto.random_bytes(32)
        
        assert not manager.is_blacklisted(node_id)
        manager.blacklist_node(node_id)
        assert manager.is_blacklisted(node_id)

    def test_get_random_peers(self):
        """Test getting random peers"""
        manager = PeerManager()
        
        # Add some connected peers
        for i in range(10):
            peer = Peer(
                node_id=Crypto.random_bytes(32),
                state=PeerState.CONNECTED,
                remote_addr="127.0.0.1",
                remote_port=8080 + i
            )
            manager.add(peer)
        
        peers = manager.get_random_peers(limit=5)
        assert len(peers) == 5

    def test_get_random_peers_more_than_available(self):
        """Test getting more peers than available"""
        manager = PeerManager()
        
        for i in range(3):
            peer = Peer(
                node_id=Crypto.random_bytes(32),
                state=PeerState.CONNECTED
            )
            manager.add(peer)
        
        peers = manager.get_random_peers(limit=10)
        assert len(peers) == 3

    def test_cleanup_stale(self):
        """Test cleaning up stale peers"""
        manager = PeerManager()
        
        # Add a stale peer
        stale_peer = Peer(
            node_id=Crypto.random_bytes(32),
            state=PeerState.CONNECTED,
            last_activity=time.time() - 600  # 10 minutes ago
        )
        manager.add(stale_peer)
        
        # Add a fresh peer
        fresh_peer = Peer(
            node_id=Crypto.random_bytes(32),
            state=PeerState.CONNECTED,
            last_activity=time.time()
        )
        manager.add(fresh_peer)
        
        manager.cleanup_stale(timeout_secs=300)
        
        assert manager.get(stale_peer.node_id) is None
        assert manager.get(fresh_peer.node_id) is not None

    def test_active_count(self):
        """Test active peer count"""
        manager = PeerManager()
        
        # Add peers in different states
        for i in range(5):
            state = PeerState.CONNECTED if i < 3 else PeerState.DISCONNECTED
            peer = Peer(
                node_id=Crypto.random_bytes(32),
                state=state
            )
            manager.add(peer)
        
        assert manager.active_count() == 3
