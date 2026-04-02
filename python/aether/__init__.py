"""
A.E.T.H.E.R. Python - Asynchronous Edge-Tolerant Holographic Execution Runtime
A decentralized P2P network implementation with DHT, messaging, and peer management.
"""

__version__ = "0.1.0"
__author__ = "A.E.T.H.E.R. Contributors"

from .crypto import Crypto, KeyPair
from .dht import DHT, BucketEntry, StoredValue
from .peer import Peer, PeerManager, PeerState
from .protocol import Node, Client, Config, Message, MessageType
from .node import run_node
from .client import connect_to_node

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
    # Helpers
    "run_node",
    "connect_to_node",
]
