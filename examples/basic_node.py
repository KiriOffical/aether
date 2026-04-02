#!/usr/bin/env python3
"""
Basic Node Example

The simplest example - running an A.E.T.H.E.R. node.
"""

from aether import Node, Config
import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description="A.E.T.H.E.R. Basic Node Example")
    parser.add_argument("--port", type=int, default=7821, help="Listen port (default: 7821)")
    parser.add_argument("--datadir", type=str, default="./aether_data", help="Data directory")
    parser.add_argument("--log-level", type=str, default="INFO", 
                       choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                       help="Log level")
    args = parser.parse_args()
    
    # Create configuration
    config = Config(
        listen_port=args.port,
        data_dir=args.datadir,
        log_level=args.log_level
    )
    
    # Create and start node
    node = Node(config)
    node.start()
    
    print("=" * 60)
    print("A.E.T.H.E.R. Node Started")
    print("=" * 60)
    print(f"Node ID:    {node.node_id.hex()[:32]}...")
    print(f"Port:       {node.get_port()}")
    print(f"Data Dir:   {args.datadir}")
    print(f"Log Level:  {args.log_level}")
    print("=" * 60)
    print("\nPress Ctrl+C to stop\n")
    
    try:
        # Run main loop
        node.run()
    except KeyboardInterrupt:
        print("\nShutting down...")
        node.stop()
        print("Node stopped.")
        sys.exit(0)


if __name__ == "__main__":
    main()
