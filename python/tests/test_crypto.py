"""Tests for crypto module."""

import pytest
from aether.crypto import Crypto, KeyPair


class TestKeyPair:
    """Test KeyPair class."""
    
    def test_generate(self):
        """Test keypair generation."""
        kp = KeyPair.generate()
        assert len(kp.public_key) == 32
        assert len(kp.secret_key) == 64
    
    def test_sign_verify(self):
        """Test signing and verification."""
        kp = KeyPair.generate()
        data = b"test message"
        signature = kp.sign(data)
        assert KeyPair.verify(kp.public_key, data, signature)
    
    def test_sign_verify_wrong_data(self):
        """Test verification fails with wrong data."""
        kp = KeyPair.generate()
        data = b"test message"
        wrong_data = b"wrong message"
        signature = kp.sign(data)
        assert not KeyPair.verify(kp.public_key, wrong_data, signature)


class TestCrypto:
    """Test Crypto class."""
    
    def test_sha256(self):
        """Test SHA-256 hashing."""
        data = b"test"
        hash1 = Crypto.sha256(data)
        hash2 = Crypto.sha256(data)
        assert hash1 == hash2
        assert len(hash1) == 32
    
    def test_node_id(self):
        """Test node ID computation."""
        public_key = Crypto.random_bytes(32)
        node_id = Crypto.node_id(public_key)
        assert len(node_id) == 32
    
    def test_distance(self):
        """Test XOR distance."""
        a = bytes([0x00, 0x01, 0x02, 0x03])
        b = bytes([0x00, 0x01, 0x02, 0x04])
        dist = Crypto.distance(a, b)
        assert dist == bytes([0x00, 0x00, 0x00, 0x07])
    
    def test_compare_distance(self):
        """Test distance comparison."""
        a = bytes([0x01])
        b = bytes([0x02])
        assert Crypto.compare_distance(a, b) == -1
        assert Crypto.compare_distance(b, a) == 1
        assert Crypto.compare_distance(a, a) == 0
    
    def test_random_bytes(self):
        """Test random bytes generation."""
        b1 = Crypto.random_bytes(16)
        b2 = Crypto.random_bytes(16)
        assert len(b1) == 16
        assert len(b2) == 16
        assert b1 != b2  # Should be different
