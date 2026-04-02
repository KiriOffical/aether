"""
A.E.T.H.E.R. Client - Connect to a running node.
"""

import socket
import struct
import time
import argparse
import secrets
import json
from typing import Optional, List, Dict, Any

from .protocol import Message, MessageType


class Client:
    """Client for connecting to an A.E.T.H.E.R. node."""
    
    def __init__(self, host: str = 'localhost', port: int = 7821):
        self.host = host
        self.port = port
        self.sock: Optional[socket.socket] = None
        self.connected = False
        self.handshake_complete = False
        self.node_id = secrets.token_hex(32)
    
    def connect(self, timeout: int = 5) -> bool:
        """Connect to the node and complete handshake."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(timeout)
            self.sock.connect((self.host, self.port))
            self.connected = True
            print(f"[+] Connected to {self.host}:{self.port}")
            
            # Complete handshake
            if not self._do_handshake():
                self.disconnect()
                return False
            
            return True
        except Exception as e:
            print(f"[-] Connection failed: {e}")
            return False
    
    def _do_handshake(self) -> bool:
        """Perform HELLO handshake."""
        try:
            # Receive HELLO from server
            self.sock.settimeout(5)
            hello = self._recv_message()
            if not hello or hello.type != MessageType.HELLO:
                print("[-] No HELLO received from server")
                return False
            
            # Send HELLO_ACK
            ack = Message(MessageType.HELLO_ACK, {
                'version': '0.1.0',
                'node_id': self.node_id,
                'timestamp': time.time(),
                'port': 0
            })
            if not self._send_message(ack):
                print("[-] Failed to send HELLO_ACK")
                return False
            
            self.handshake_complete = True
            print("[+] Handshake complete")
            return True
        except Exception as e:
            print(f"[-] Handshake failed: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from node."""
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
        self.connected = False
        print("[*] Disconnected")
    
    def _recv_all(self, n: int) -> Optional[bytes]:
        """Receive exactly n bytes."""
        data = b''
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                return None
            data += chunk
        return data
    
    def _recv_message(self, timeout: int = 5) -> Optional[Message]:
        """Receive a complete message."""
        try:
            self.sock.settimeout(timeout)
            header = self._recv_all(5)
            if not header:
                return None
            msg_type, payload_len = struct.unpack('>BI', header)
            payload = self._recv_all(payload_len)
            if not payload:
                return None
            return Message.decode(header + payload)
        except:
            return None
    
    def _send_message(self, msg: Message) -> bool:
        """Send a message to node."""
        try:
            self.sock.sendall(msg.encode())
            return True
        except Exception as e:
            print(f"[-] Send error: {e}")
            return False
    
    def ping(self) -> bool:
        """Send PING and wait for PONG."""
        # Try to receive HELLO first
        try:
            self.sock.settimeout(0.5)
            hello = self._recv_message()
            if hello and hello.type == MessageType.HELLO:
                ack = Message(MessageType.HELLO_ACK, {
                    'version': '0.1.0',
                    'node_id': self.node_id,
                    'timestamp': time.time(),
                    'port': 0
                })
                self._send_message(ack)
        except:
            pass
        
        # Send PING
        msg = Message(MessageType.PING, {'sequence': int(time.time() * 1000)})
        start = time.time()
        if not self._send_message(msg):
            return False
        
        response = self._recv_message(timeout=5)
        if response and response.type == MessageType.PONG:
            latency = (time.time() - start) * 1000
            print(f"[+] PONG received (latency: {latency:.1f}ms)")
            return True
        
        print("[-] No PONG received")
        return False
    
    def store(self, key: str, value: str) -> bool:
        """Store a value in DHT."""
        # Try to receive HELLO first
        try:
            self.sock.settimeout(0.5)
            hello = self._recv_message()
            if hello and hello.type == MessageType.HELLO:
                ack = Message(MessageType.HELLO_ACK, {
                    'version': '0.1.0',
                    'node_id': self.node_id,
                    'timestamp': time.time(),
                    'port': 0
                })
                self._send_message(ack)
        except:
            pass
        
        # Encode key and value as hex strings
        key_hex = key if self._is_hex(key) else key.encode().hex()
        value_hex = value if self._is_hex(value) else value.encode().hex()
        
        msg = Message(MessageType.STORE_VALUE, {
            'key': key_hex,
            'value': value_hex
        })
        if self._send_message(msg):
            print(f"[+] Stored value for key: {key}")
            return True
        return False
    
    def get(self, key: str) -> Optional[str]:
        """Get a value from DHT."""
        # Try to receive HELLO first
        try:
            self.sock.settimeout(0.5)
            hello = self._recv_message()
            if hello and hello.type == MessageType.HELLO:
                ack = Message(MessageType.HELLO_ACK, {
                    'version': '0.1.0',
                    'node_id': self.node_id,
                    'timestamp': time.time(),
                    'port': 0
                })
                self._send_message(ack)
        except:
            pass
        
        # Encode key as hex string
        key_hex = key if self._is_hex(key) else key.encode().hex()
        
        msg = Message(MessageType.GET_VALUE, {
            'key': key_hex
        })
        if not self._send_message(msg):
            return None
        
        response = self._recv_message(timeout=5)
        if response and response.type == MessageType.GET_VALUE_RESPONSE:
            if response.payload.get('found'):
                value_hex = response.payload.get('value', '')
                try:
                    value = bytes.fromhex(value_hex).decode('utf-8')
                except:
                    value = value_hex
                print(f"[+] Found value: {value}")
                return value
            else:
                print(f"[-] Key not found: {key}")
        return None
    
    def find_node(self, target_id: str) -> List[Dict[str, Any]]:
        """Find nodes closest to target."""
        # Try to receive HELLO first
        try:
            self.sock.settimeout(0.5)
            hello = self._recv_message()
            if hello and hello.type == MessageType.HELLO:
                ack = Message(MessageType.HELLO_ACK, {
                    'version': '0.1.0',
                    'node_id': self.node_id,
                    'timestamp': time.time(),
                    'port': 0
                })
                self._send_message(ack)
        except:
            pass
        
        msg = Message(MessageType.FIND_NODE, {'target': target_id})
        if not self._send_message(msg):
            return []
        
        response = self._recv_message(timeout=5)
        if response and response.type == MessageType.FIND_NODE_RESPONSE:
            nodes = response.payload.get('nodes', [])
            print(f"[+] Found {len(nodes)} closest nodes:")
            for node in nodes:
                print(f"    - {node.get('node_id', 'unknown')[:8]}... @ {node.get('addr')}:{node.get('port')}")
            return nodes
        return []
    
    def peer_exchange(self) -> List[Dict[str, Any]]:
        """Request peer list."""
        # Try to receive HELLO first
        try:
            self.sock.settimeout(0.5)
            hello = self._recv_message()
            if hello and hello.type == MessageType.HELLO:
                ack = Message(MessageType.HELLO_ACK, {
                    'version': '0.1.0',
                    'node_id': self.node_id,
                    'timestamp': time.time(),
                    'port': 0
                })
                self._send_message(ack)
        except:
            pass
        
        msg = Message(MessageType.PEER_EXCHANGE, {})
        if not self._send_message(msg):
            return []
        
        response = self._recv_message(timeout=5)
        if response and response.type == MessageType.PEER_EXCHANGE:
            peers = response.payload.get('peers', [])
            print(f"[+] Got {len(peers)} peers:")
            for peer in peers:
                print(f"    - {peer.get('addr')}:{peer.get('port')}")
            return peers
        return []
    
    @staticmethod
    def _is_hex(s: str) -> bool:
        """Check if string is hex encoded."""
        try:
            int(s, 16)
            return len(s) % 2 == 0
        except ValueError:
            return False


def connect_to_node(args=None):
    """Connect to a node and run commands."""
    parser = argparse.ArgumentParser(description='A.E.T.H.E.R. Node Client')
    parser.add_argument('-H', '--host', default='localhost', help='Node hostname/IP')
    parser.add_argument('-p', '--port', type=int, default=7821, help='Node port')
    parser.add_argument('-i', '--interactive', action='store_true', help='Interactive mode')
    parser.add_argument('--ping', action='store_true', help='Send PING')
    parser.add_argument('--store', nargs=2, metavar=('KEY', 'VALUE'), help='Store key-value')
    parser.add_argument('--get', metavar='KEY', help='Get value by key')
    parser.add_argument('--find-node', metavar='NODE_ID', help='Find closest nodes')
    parser.add_argument('--peers', action='store_true', help='Get peer list')
    
    args = parser.parse_args(args)
    
    print("=" * 50)
    print("     A.E.T.H.E.R. Client v0.1.0")
    print("=" * 50)
    print()
    
    client = Client(args.host, args.port)
    
    if not client.connect():
        print("\nFailed to connect. Is the node running?")
        print(f"Start node: python -m aether.node run --port {args.port}")
        return 1
    
    try:
        if args.interactive:
            print("Interactive mode. Commands:")
            print("  ping              - Send PING")
            print("  store <k> <v>     - Store key-value")
            print("  get <key>         - Get value")
            print("  find <node_id>    - Find nodes")
            print("  peers             - Get peer list")
            print("  quit/exit         - Disconnect")
            print()
            
            while True:
                try:
                    cmd = input("aether> ").strip().split()
                    if not cmd:
                        continue
                    elif cmd[0] in ('quit', 'exit'):
                        break
                    elif cmd[0] == 'ping':
                        client.ping()
                    elif cmd[0] == 'store' and len(cmd) >= 3:
                        client.store(cmd[1], cmd[2])
                    elif cmd[0] == 'get' and len(cmd) >= 2:
                        client.get(cmd[1])
                    elif cmd[0] == 'find' and len(cmd) >= 2:
                        client.find_node(cmd[1])
                    elif cmd[0] == 'peers':
                        client.peer_exchange()
                    else:
                        print("Unknown command. Type 'help' for commands.")
                except KeyboardInterrupt:
                    break
                except EOFError:
                    break
        
        elif args.ping:
            client.ping()
        elif args.store:
            client.store(args.store[0], args.store[1])
        elif args.get:
            client.get(args.get)
        elif args.find_node:
            client.find_node(args.find_node)
        elif args.peers:
            client.peer_exchange()
        else:
            # Default: just do handshake and ping
            client.ping()
            print("\n[*] Use --help for available commands")
    
    finally:
        client.disconnect()
    
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(connect_to_node())
