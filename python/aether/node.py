"""
A.E.T.H.E.R. Node - Main entry point.
"""

import argparse
import os
import sys
import signal
import time

from .protocol import Node, Config
from .crypto import Crypto


VERSION_STRING = "0.1.0"


def print_banner():
    """Print startup banner."""
    print("=" * 60)
    print("     A.E.T.H.E.R. Node")
    print("  Asynchronous Edge-Tolerant Holographic")
    print(f"       Execution Runtime v{VERSION_STRING}")
    print("=" * 60)
    print()


def print_usage(prog: str):
    """Print usage information."""
    print(f"Usage: {prog} [options]")
    print("Options:")
    print("  -p, --port <port>       Listening port (default: 7821)")
    print("  -d, --datadir <dir>     Data directory")
    print("  -v, --verbose           Verbose logging")
    print("  -h, --help              Show this help")
    print()
    print("Commands:")
    print("  run                     Run a node (default)")
    print("  connect                 Connect to a node")
    print()


def run_node(args=None):
    """Run a node."""
    parser = argparse.ArgumentParser(description='A.E.T.H.E.R. Node')
    parser.add_argument('-p', '--port', type=int, default=7821, help='Listening port')
    parser.add_argument('-d', '--datadir', type=str, help='Data directory')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose logging')
    parser.add_argument('--log-file', type=str, help='Log file path')
    parser.add_argument('--max-connections', type=int, default=10000, help='Max connections')
    parser.add_argument('--auth-token', type=str, help='Authentication token')
    
    args = parser.parse_args(args)
    
    print_banner()
    
    # Create config
    data_dir = args.datadir or os.path.join(os.path.dirname(__file__), '..', 'aether_data')
    identity_path = os.path.join(data_dir, 'identity.bin') if data_dir else ''
    
    config = Config(
        identity_path=identity_path,
        data_dir=data_dir,
        listen_port=args.port,
        max_connections=args.max_connections,
        log_level=10 if args.verbose else 20,  # DEBUG or INFO
        log_file=args.log_file,
        auth_token=args.auth_token
    )
    
    # Setup signal handlers
    def signal_handler(sig, frame):
        print("\nShutting down...")
        node.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Create and start node
    node = Node(config)
    node.start()
    
    print("Configuration:")
    print(f"  Listen port:     {config.listen_port}")
    print(f"  Max connections: {config.max_connections}")
    print(f"  Data directory:  {config.data_dir}")
    print(f"  Log level:       {config.log_level}")
    if config.auth_token:
        print(f"  Auth enabled:    Yes")
    print()
    
    print(f"Node ID: {node.node_id.hex()[:16]}...")
    print(f"Listening on port {node.get_port()}")
    print()
    print("Press Ctrl+C to stop...")
    print()
    
    # Run main loop
    node.run()
    
    print("A.E.T.H.E.R. Node stopped.")
    return 0


def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print_usage(sys.argv[0])
        return 1
    
    cmd = sys.argv[1]
    
    if cmd in ('run', '-p', '--port', '-d', '--datadir', '-v', '--verbose', '-h', '--help'):
        return run_node()
    elif cmd == 'connect':
        # Import client for connect command
        from .client import connect_to_node
        return connect_to_node(sys.argv[2:])
    elif cmd == '-h' or cmd == '--help':
        print_usage(sys.argv[0])
        return 0
    else:
        print(f"Unknown command: {cmd}")
        print_usage(sys.argv[0])
        return 1


if __name__ == '__main__':
    sys.exit(main())
