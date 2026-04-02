"""
A.E.T.H.E.R. Python - Setup script.
Asynchronous Edge-Tolerant Holographic Execution Runtime

Includes:
- Classic P2P mode (Ed25519, DHT, peer messaging)
- IPFS-like mode (BLAKE2b chunking, Merkle trees, HTTP gateway)
"""

from setuptools import setup, find_packages
import os

# Try to read README, use fallback if not available
try:
    with open("README.md", "r", encoding="utf-8") as fh:
        long_description = fh.read()
except FileNotFoundError:
    long_description = "A.E.T.H.E.R. P2P Protocol - Python Implementation"

setup(
    name="aether-p2p",
    version="0.1.0",
    author="A.E.T.H.E.R. Contributors",
    description="Decentralized P2P network with DHT, messaging, and IPFS-like content addressing",
    long_description=long_description,
    long_description_content_type="text/markdown",
    packages=find_packages(),
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Topic :: System :: Networking",
        "Topic :: System :: Distributed Computing",
    ],
    python_requires=">=3.8",
    install_requires=[
        "PyNaCl>=1.5.0",
    ],
    extras_require={
        "ipfs": [
            "lmdb>=1.4.0",
            "aiohttp>=3.8.0",
        ],
        "dev": [
            "pytest>=7.0.0",
            "pytest-asyncio>=0.21.0",
            "black>=22.0.0",
            "flake8>=5.0.0",
            "mypy>=0.990",
        ],
    },
    entry_points={
        "console_scripts": [
            "aether=aether.cli:main",
            "aether-node=aether.cli:main",
            "aether-ipfs=aether.ipfs:main",
        ],
    },
)
