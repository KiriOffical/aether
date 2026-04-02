"""
Cryptographic primitives for A.E.T.H.E.R.
Ed25519 signatures and SHA-256 hashing.
"""

import hashlib
import hmac
import os
import secrets
from typing import Optional

# Try to import nacl for Ed25519, fall back to pure Python if not available
try:
    from nacl.signing import SigningKey, VerifyKey
    NACL_AVAILABLE = True
except ImportError:
    NACL_AVAILABLE = False


class KeyPair:
    """Ed25519 key pair for node identity."""
    
    PUBLIC_KEY_SIZE = 32
    SECRET_KEY_SIZE = 64
    SIGNATURE_SIZE = 64
    
    def __init__(self, secret_key: Optional[bytes] = None, public_key: Optional[bytes] = None):
        if NACL_AVAILABLE:
            if secret_key is not None:
                self._signing_key = SigningKey(secret_key[:32])
                self._secret_key = self._signing_key.encode() + self._signing_key.verify_key.encode()
                self._public_key = self._signing_key.verify_key.encode()
            elif public_key is not None:
                self._public_key = public_key
                self._secret_key = None
                self._signing_key = None
            else:
                self._signing_key = SigningKey.generate()
                self._secret_key = self._signing_key.encode() + self._signing_key.verify_key.encode()
                self._public_key = self._signing_key.verify_key.encode()
        else:
            # Pure Python fallback - limited functionality
            if secret_key is not None:
                self._secret_key = secret_key
                self._public_key = public_key
            elif public_key is not None:
                self._public_key = public_key
                self._secret_key = None
            else:
                self._secret_key = secrets.token_bytes(32)
                self._public_key = None  # Would need proper Ed25519 impl
    
    @classmethod
    def generate(cls) -> 'KeyPair':
        """Generate a new random keypair."""
        return cls()
    
    @classmethod
    def load(cls, path: str) -> 'KeyPair':
        """Load keypair from file."""
        with open(path, 'rb') as f:
            secret_key = f.read()
        return cls(secret_key=secret_key)
    
    @classmethod
    def load_or_generate(cls, path: str) -> 'KeyPair':
        """Load keypair from file, or generate if not exists."""
        try:
            return cls.load(path)
        except FileNotFoundError:
            keypair = cls.generate()
            keypair.save(path)
            return keypair
    
    def save(self, path: str) -> None:
        """Save keypair to file."""
        import os
        os.makedirs(os.path.dirname(path) if os.path.dirname(path) else '.', exist_ok=True)
        with open(path, 'wb') as f:
            f.write(self._secret_key)
    
    @property
    def public_key(self) -> bytes:
        """Get the public key."""
        return self._public_key
    
    @property
    def secret_key(self) -> bytes:
        """Get the secret key (keep secure!)."""
        return self._secret_key
    
    def sign(self, data: bytes) -> bytes:
        """Sign data with the secret key."""
        if NACL_AVAILABLE and self._signing_key:
            return self._signing_key.sign(data).signature
        else:
            # Fallback - not secure, just for testing
            return hashlib.sha256(data + self._secret_key).digest()
    
    @staticmethod
    def verify(public_key: bytes, data: bytes, signature: bytes) -> bool:
        """Verify a signature using a public key."""
        if NACL_AVAILABLE:
            try:
                verify_key = VerifyKey(public_key)
                verify_key.verify(data, signature)
                return True
            except Exception:
                return False
        else:
            # Fallback - not secure
            return True


class Crypto:
    """Cryptographic utilities."""
    
    HASH_SIZE = 32
    NODE_ID_SIZE = 32
    
    @staticmethod
    def sha256(data: bytes) -> bytes:
        """Compute SHA-256 hash."""
        return hashlib.sha256(data).digest()
    
    @staticmethod
    def node_id(public_key: bytes) -> bytes:
        """Compute node ID from public key (SHA-256)."""
        return Crypto.sha256(public_key)
    
    @staticmethod
    def distance(a: bytes, b: bytes) -> bytes:
        """Compute XOR distance between two node IDs."""
        return bytes(x ^ y for x, y in zip(a, b))
    
    @staticmethod
    def compare_distance(a: bytes, b: bytes) -> int:
        """
        Compare two distances.
        Returns: -1 if a < b, 0 if equal, 1 if a > b
        """
        for x, y in zip(a, b):
            if x != y:
                return -1 if x < y else 1
        return 0
    
    @staticmethod
    def random_bytes(length: int) -> bytes:
        """Generate random bytes."""
        return secrets.token_bytes(length)
    
    @staticmethod
    def hmac_sha256(key: bytes, data: bytes) -> bytes:
        """Compute HMAC-SHA256."""
        return hmac.new(key, data, hashlib.sha256).digest()
