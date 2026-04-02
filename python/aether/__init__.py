"""
A.E.T.H.E.R. Python - Asynchronous Edge-Tolerant Holographic Execution Runtime
A decentralized P2P network implementation with DHT, messaging, and peer management.
"""

__version__ = "0.1.0"
__author__ = "A.E.T.H.E.R. Contributors"

from .crypto import Crypto, KeyPair
from .dht import DHT, BucketEntry, StoredValue, Endpoint
from .peer import Peer, PeerManager, PeerState
from .protocol import Node, Config, Message, MessageType
from .client import Client

__all__ = [
    # Version
    "__version__",
    # Crypto
    "Crypto",
    "KeyPair",
    # DHT
    "DHT",
    "BucketEntry",
    "StoredValue",
    "Endpoint",
    # Peer
    "Peer",
    "PeerManager",
    "PeerState",
    # Protocol
    "Node",
    "Client",
    "Config",
    "Message",
    "MessageType",
]
