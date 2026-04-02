#!/usr/bin/env python3
"""
DHT Storage Example

Store and retrieve data from the Distributed Hash Table.
"""

from aether import Node, Config
import argparse
import json


def main():
    parser = argparse.ArgumentParser(description="A.E.T.H.E.R. DHT Storage Example")
    parser.add_argument("--port", type=int, default=7821, help="Listen port")
    args = parser.parse_args()
    
    # Setup node
    config = Config(listen_port=args.port)
    node = Node(config)
    node.start()
    
    print("=" * 60)
    print("DHT Storage Example")
    print("=" * 60)
    
    # Store some values
    print("\n[1] Storing values in DHT...")
    
    test_data = {
        b"user:1001": b'{"name": "Alice", "age": 30, "city": "New York"}',
        b"user:1002": b'{"name": "Bob", "age": 25, "city": "San Francisco"}',
        b"user:1003": b'{"name": "Charlie", "age": 35, "city": "Seattle"}',
        b"config:app": b'{"theme": "dark", "language": "en", "version": "1.0"}',
        b"status:online": b'true',
    }
    
    for key, value in test_data.items():
        success = node.dht_store(key, value)
        print(f"  {'✓' if success else '✗'} Stored: {key.decode()}")
    
    # Retrieve values
    print("\n[2] Retrieving values from DHT...")
    
    for key in test_data.keys():
        retrieved = node.dht_get(key)
        if retrieved:
            try:
                # Try to parse as JSON for nice formatting
                data = json.loads(retrieved.decode())
                print(f"  ✓ {key.decode()}: {json.dumps(data, indent=2)}")
            except:
                print(f"  ✓ {key.decode()}: {retrieved.decode()}")
        else:
            print(f"  ✗ {key.decode()}: NOT FOUND")
    
    # Try to get non-existent key
    print("\n[3] Testing non-existent key...")
    missing = node.dht_get(b"missing:key")
    print(f"  Result for 'missing:key': {missing}")
    
    # Get node stats
    print("\n[4] Node Statistics:")
    stats = node.get_stats()
    print(f"  - Peer Count:    {stats.get('peer_count', 0)}")
    print(f"  - DHT Keys:      {stats.get('dht_values', 0)}")
    print(f"  - DHT Nodes:     {stats.get('dht_nodes', 0)}")
    print(f"  - Version:       {stats.get('version', 'unknown')}")
    
    # Cleanup
    print("\n[5] Stopping node...")
    node.stop()
    print("  Done!")
    
    print("\n" + "=" * 60)
    print("Example completed successfully!")
    print("=" * 60)


if __name__ == "__main__":
    main()
