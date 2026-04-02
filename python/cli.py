#!/usr/bin/env python3
"""
A.E.T.H.E.R. Python - Main CLI entry point.
Supports both classic P2P mode and IPFS-like mode.
"""

import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print_help()
        return 1
    
    # Check for IPFS commands
    ipfs_commands = ['daemon', 'add', 'get', 'pin', 'unpin', 'ipfs']
    if sys.argv[1] in ipfs_commands or (len(sys.argv) > 2 and sys.argv[2] in ipfs_commands):
        from aether.ipfs import main as ipfs_main
        return ipfs_main()
    
    # Classic P2P mode
    from aether.node import main as node_main
    return node_main()


def print_help():
    """Print help message."""
    print("""
A.E.T.H.E.R. P2P Protocol - Python Implementation

Usage: aether <command> [options]

IPFS-like Commands:
  daemon              Start the IPFS node daemon
  add <file>          Add a file to the node
  get <cid> [output]  Get a file by CID
  pin <cid>           Pin a CID locally
  unpin <cid>         Unpin a CID

Classic P2P Commands:
  run                 Run a node (default)
  connect             Connect to a node

Options:
  -h, --help          Show this help
  -v, --version       Show version

Examples:
  aether daemon                    Start IPFS daemon
  aether add myfile.txt            Add file, prints CID
  aether get <CID> output.txt      Get file by CID
  aether run -p 7821               Run classic P2P node

For more info: aether <command> --help
""")


if __name__ == '__main__':
    sys.exit(main())
