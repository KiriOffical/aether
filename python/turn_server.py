#!/usr/bin/env python3
"""
A.E.T.H.E.R. TURN Server
Traversal Using Relays for NAT (RFC 5766)

Minimal implementation for symmetric NAT fallback.
"""

import socket
import struct
import threading
import time
import hashlib
import hmac
import secrets
import argparse
import logging
from typing import Dict, Tuple, Optional
from dataclasses import dataclass

# TURN Constants
TURN_PORT = 3478
TURN_MAGIC = 0x2112A442
TURN_MESSAGE_HEADER_SIZE = 20

# Message Types
BINDING_REQUEST = 0x0001
BINDING_RESPONSE = 0x0101
ALLOCATE_REQUEST = 0x0003
ALLOCATE_RESPONSE = 0x0103
REFRESH_REQUEST = 0x0004
REFRESH_RESPONSE = 0x0104
SEND_INDICATION = 0x0016
DATA_INDICATION = 0x0117
CREATE_PERMISSION_REQUEST = 0x0008
CREATE_PERMISSION_RESPONSE = 0x0108
CHANNEL_BIND_REQUEST = 0x0009
CHANNEL_BIND_RESPONSE = 0x0109

# Attributes
USERNAME = 0x0006
REALM = 0x0014
NONCE = 0x0015
XOR_RELAYED_ADDRESS = 0x0016
REQUESTED_TRANSPORT = 0x0019
XOR_PEER_ADDRESS = 0x0020
XOR_MAPPED_ADDRESS = 0x0022
LIFETIME = 0x000D
CHANNEL_NUMBER = 0x000C

# Default configuration
DEFAULT_REALM = "aether.network"
DEFAULT_USERNAME = "aether_user"
DEFAULT_PASSWORD = "aether_secret_2025"
DEFAULT_ALLOCATION_LIFETIME = 600  # 10 minutes


@dataclass
class Allocation:
    """TURN allocation (relay session)"""
    username: str
    client_addr: Tuple[str, int]
    relay_addr: Tuple[str, int]
    relay_socket: socket.socket
    lifetime: int
    created_at: float
    permissions: Dict[str, float]  # peer_ip -> expiry
    channels: Dict[int, str]  # channel_number -> peer_ip


class TurnServer:
    """Minimal TURN server for symmetric NAT traversal"""
    
    def __init__(self, host: str = '0.0.0.0', port: int = TURN_PORT,
                 realm: str = DEFAULT_REALM,
                 username: str = DEFAULT_USERNAME,
                 password: str = DEFAULT_PASSWORD,
                 relay_port_start: int = 40000,
                 relay_port_end: int = 40100):
        self.host = host
        self.port = port
        self.realm = realm
        self.username = username
        self.password = password
        
        # Relay port pool
        self.relay_port_start = relay_port_start
        self.relay_port_end = relay_port_end
        self.used_relay_ports = set()
        
        # Allocations
        self.allocations: Dict[Tuple[str, int], Allocation] = {}
        self.relay_sockets: Dict[int, socket.socket] = {}
        
        # Server socket
        self.server_socket: Optional[socket.socket] = None
        self.running = False
        
        # Setup logging
        logging.basicConfig(
            level=logging.INFO,
            format='%(asctime)s [TURN] %(message)s'
        )
        self.logger = logging.getLogger('TURN')
    
    def generate_nonce(self) -> bytes:
        """Generate random nonce"""
        return secrets.token_bytes(16)
    
    def generate_password_hash(self, username: str, realm: str, password: str) -> bytes:
        """Generate long-term credential hash (RFC 5389)"""
        key = f"{username}:{realm}:{password}".encode('utf-8')
        return hashlib.md5(key).digest()
    
    def check_auth(self, data: bytes, username: str, nonce: bytes,
                   message_integrity: bytes) -> bool:
        """Verify MESSAGE-INTEGRITY attribute"""
        # Simplified auth - in production, verify HMAC-SHA1
        return username == self.username
    
    def create_udp_socket(self, port: int) -> Tuple[socket.socket, int]:
        """Create relay UDP socket"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        # Try to bind to requested port
        try:
            sock.bind((self.host, port))
            return sock, port
        except OSError:
            # Port in use, find available
            for p in range(self.relay_port_start, self.relay_port_end):
                if p not in self.used_relay_ports:
                    try:
                        sock.bind((self.host, p))
                        self.used_relay_ports.add(p)
                        return sock, p
                    except OSError:
                        continue
            
            sock.close()
            raise RuntimeError("No relay ports available")
    
    def handle_allocate(self, data: bytes, client_addr: Tuple[str, int]):
        """Handle ALLOCATE request"""
        self.logger.info(f"ALLOCATE from {client_addr}")
        
        # Parse request
        msg_type = struct.unpack('>H', data[:2])[0]
        msg_len = struct.unpack('>H', data[2:4])[0]
        
        if msg_type != ALLOCATE_REQUEST:
            return
        
        # Generate nonce
        nonce = self.generate_nonce()
        
        # Check for USERNAME attribute (simplified parsing)
        offset = TURN_MESSAGE_HEADER_SIZE
        has_username = False
        username = None
        
        while offset < TURN_MESSAGE_HEADER_SIZE + msg_len:
            attr_type = struct.unpack('>H', data[offset:offset+2])[0]
            attr_len = struct.unpack('>H', data[offset+2:offset+4])[0]
            
            if attr_type == USERNAME:
                has_username = True
                username = data[offset+4:offset+4+attr_len].decode('utf-8')
            
            offset += 4 + attr_len
            if attr_len % 4 != 0:
                offset += 4 - (attr_len % 4)
        
        # Require username
        if not has_username:
            response = self.create_error_response(401, b'Unauthorized', nonce)
            self.server_socket.sendto(response, client_addr)
            return
        
        # Create relay allocation
        try:
            relay_sock, relay_port = self.create_udp_socket(self.relay_port_start)
        except RuntimeError as e:
            self.logger.error(f"Failed to allocate relay port: {e}")
            return
        
        relay_addr = (self.host, relay_port)
        
        # Store allocation
        allocation = Allocation(
            username=username or self.username,
            client_addr=client_addr,
            relay_addr=relay_addr,
            relay_socket=relay_sock,
            lifetime=DEFAULT_ALLOCATION_LIFETIME,
            created_at=time.time(),
            permissions={},
            channels={}
        )
        self.allocations[client_addr] = allocation
        
        # Start relay thread
        thread = threading.Thread(
            target=self._relay_loop,
            args=(allocation,),
            daemon=True
        )
        thread.start()
        
        self.logger.info(f"Allocated relay {relay_addr} for {client_addr}")
        
        # Send success response
        response = self.create_allocate_response(relay_addr, nonce)
        self.server_socket.sendto(response, client_addr)
    
    def create_allocate_response(self, relay_addr: Tuple[str, int], nonce: bytes) -> bytes:
        """Create ALLOCATE success response"""
        response = struct.pack('>HHI', ALLOCATE_RESPONSE, 0, TURN_MAGIC)
        response += secrets.token_bytes(12)  # Transaction ID
        
        # LIFETIME attribute
        lifetime = struct.pack('>HHI', LIFETIME, 4, DEFAULT_ALLOCATION_LIFETIME)
        response += lifetime
        
        # XOR-RELAYED-ADDRESS attribute
        ip_bytes = socket.inet_aton(relay_addr[0])
        port_bytes = struct.pack('>H', relay_addr[1] ^ (TURN_MAGIC >> 16))
        xor_ip = bytes(b ^ ((TURN_MAGIC >> (8 * ((i // 4)))) & 0xFF) 
                       for i, b in enumerate(ip_bytes))
        
        xor_addr = struct.pack('>HH', 0x01, 0)  # IPv4, reserved
        xor_addr += port_bytes + xor_ip
        
        attr = struct.pack('>HH', XOR_RELAYED_ADDRESS, len(xor_addr)) + xor_addr
        response += attr
        
        # XOR-MAPPED-ADDRESS (echo client address)
        response += attr  # Simplified - use same as relay
        
        # Update length
        response = response[:2] + struct.pack('>H', len(response) - 20) + response[4:]
        
        return response
    
    def create_error_response(self, code: int, reason: bytes, nonce: bytes) -> bytes:
        """Create error response"""
        response = struct.pack('>HHI', BINDING_RESPONSE, 0, TURN_MAGIC)
        response += secrets.token_bytes(12)
        
        # ERROR-CODE attribute
        error_code = struct.pack('>HHIH', 0x0009, 8, code, 0) + reason
        response += error_code
        
        # NONCE attribute
        nonce_attr = struct.pack('>HH', NONCE, len(nonce)) + nonce
        response += nonce_attr
        
        # REALM attribute
        realm_bytes = self.realm.encode('utf-8')
        realm_attr = struct.pack('>HH', REALM, len(realm_bytes)) + realm_bytes
        response += realm_attr
        
        # Update length
        response = response[:2] + struct.pack('>H', len(response) - 20) + response[4:]
        
        return response
    
    def _relay_loop(self, allocation: Allocation):
        """Relay data between client and peers"""
        self.logger.debug(f"Starting relay loop for {allocation.client_addr}")
        
        while allocation.lifetime > 0:
            try:
                # Check lifetime
                age = time.time() - allocation.created_at
                allocation.lifetime = max(0, DEFAULT_ALLOCATION_LIFETIME - int(age))
                
                if allocation.lifetime <= 0:
                    break
                
                # Receive from relay socket
                allocation.relay_socket.settimeout(1.0)
                try:
                    data, peer_addr = allocation.relay_socket.recvfrom(65535)
                except socket.timeout:
                    continue
                
                # Check permission
                peer_ip = peer_addr[0]
                if peer_ip not in allocation.permissions:
                    self.logger.debug(f"Dropped - no permission for {peer_ip}")
                    continue
                
                # Send DATA indication to client
                indication = self.create_data_indication(data, peer_addr)
                self.server_socket.sendto(indication, allocation.client_addr)
                
            except Exception as e:
                self.logger.error(f"Relay error: {e}")
                break
        
        # Cleanup
        self.logger.info(f"Relay expired for {allocation.client_addr}")
        self._cleanup_allocation(allocation)
    
    def create_data_indication(self, data: bytes, peer_addr: Tuple[str, int]) -> bytes:
        """Create DATA indication to client"""
        indication = struct.pack('>HHI', DATA_INDICATION, 0, TURN_MAGIC)
        indication += secrets.token_bytes(12)
        
        # XOR-PEER-ADDRESS
        ip_bytes = socket.inet_aton(peer_addr[0])
        port_bytes = struct.pack('>H', peer_addr[1] ^ (TURN_MAGIC >> 16))
        xor_ip = bytes(b ^ ((TURN_MAGIC >> (8 * ((i // 4)))) & 0xFF) 
                       for i, b in enumerate(ip_bytes))
        
        xor_addr = struct.pack('>HH', 0x01, 0) + port_bytes + xor_ip
        attr = struct.pack('>HH', XOR_PEER_ADDRESS, len(xor_addr)) + xor_addr
        indication += attr
        
        # DATA attribute
        data_attr = struct.pack('>HH', 0x000A, len(data)) + data
        indication += data_attr
        
        # Update length
        indication = indication[:2] + struct.pack('>H', len(indication) - 20) + indication[4:]
        
        return indication
    
    def handle_send_indication(self, data: bytes, client_addr: Tuple[str, int]):
        """Handle SEND indication (client -> peer)"""
        if client_addr not in self.allocations:
            return
        
        allocation = self.allocations[client_addr]
        
        # Parse indication
        msg_len = struct.unpack('>H', data[2:4])[0]
        
        # Extract XOR-PEER-ADDRESS and DATA
        offset = TURN_MESSAGE_HEADER_SIZE
        peer_addr = None
        send_data = None
        
        while offset < TURN_MESSAGE_HEADER_SIZE + msg_len:
            attr_type = struct.unpack('>H', data[offset:offset+2])[0]
            attr_len = struct.unpack('>H', data[offset+2:offset+4])[0]
            
            if attr_type == XOR_PEER_ADDRESS:
                # Parse XOR address
                port = struct.unpack('>H', data[offset+6:offset+8])[0]
                port ^= (TURN_MAGIC >> 16)
                ip_bytes = bytes(b ^ ((TURN_MAGIC >> (8 * ((i // 4)))) & 0xFF) 
                               for i, b in enumerate(data[offset+8:offset+12]))
                ip = socket.inet_ntoa(ip_bytes)
                peer_addr = (ip, port)
            
            elif attr_type == 0x000A:  # DATA
                send_data = data[offset+4:offset+4+attr_len]
            
            offset += 4 + attr_len
            if attr_len % 4 != 0:
                offset += 4 - (attr_len % 4)
        
        if peer_addr and send_data:
            # Check permission
            if peer_addr[0] in allocation.permissions:
                allocation.relay_socket.sendto(send_data, peer_addr)
                self.logger.debug(f"Relayed {len(send_data)} bytes to {peer_addr}")
    
    def handle_create_permission(self, data: bytes, client_addr: Tuple[str, int]):
        """Handle CREATE_PERMISSION request"""
        if client_addr not in self.allocations:
            return
        
        allocation = self.allocations[client_addr]
        
        # Parse request
        msg_len = struct.unpack('>H', data[2:4])[0]
        offset = TURN_MESSAGE_HEADER_SIZE
        
        while offset < TURN_MESSAGE_HEADER_SIZE + msg_len:
            attr_type = struct.unpack('>H', data[offset:offset+2])[0]
            attr_len = struct.unpack('>H', data[offset+2:offset+4])[0]
            
            if attr_type == XOR_PEER_ADDRESS:
                # Parse XOR address
                port = struct.unpack('>H', data[offset+6:offset+8])[0]
                port ^= (TURN_MAGIC >> 16)
                ip_bytes = bytes(b ^ ((TURN_MAGIC >> (8 * ((i // 4)))) & 0xFF) 
                               for i, b in enumerate(data[offset+8:offset+12]))
                ip = socket.inet_ntoa(ip_bytes)
                
                # Add permission
                allocation.permissions[ip] = time.time() + 300  # 5 min
                self.logger.info(f"Permission granted for {ip}")
            
            offset += 4 + attr_len
            if attr_len % 4 != 0:
                offset += 4 - (attr_len % 4)
        
        # Send success response
        response = struct.pack('>HHI', CREATE_PERMISSION_RESPONSE, 0, TURN_MAGIC)
        response += secrets.token_bytes(12)
        response = response[:2] + struct.pack('>H', 0) + response[4:]
        self.server_socket.sendto(response, client_addr)
    
    def _cleanup_allocation(self, allocation: Allocation):
        """Clean up expired allocation"""
        if allocation.client_addr in self.allocations:
            del self.allocations[allocation.client_addr]
        
        if allocation.relay_socket:
            allocation.relay_socket.close()
        
        if allocation.relay_addr[1] in self.used_relay_ports:
            self.used_relay_ports.remove(allocation.relay_addr[1])
    
    def start(self):
        """Start TURN server"""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.setblocking(False)
        
        self.running = True
        
        self.logger.info(f"TURN server started on {self.host}:{self.port}")
        self.logger.info(f"Realm: {self.realm}")
        self.logger.info(f"Username: {self.username}")
        self.logger.info(f"Relay ports: {self.relay_port_start}-{self.relay_port_end}")
        
        # Main loop
        while self.running:
            try:
                data, client_addr = self.server_socket.recvfrom(65535)
                self._handle_packet(data, client_addr)
            except BlockingIOError:
                time.sleep(0.1)
            except Exception as e:
                if self.running:
                    self.logger.error(f"Server error: {e}")
    
    def _handle_packet(self, data: bytes, client_addr: Tuple[str, int]):
        """Handle incoming TURN packet"""
        if len(data) < TURN_MESSAGE_HEADER_SIZE:
            return
        
        msg_type = struct.unpack('>H', data[:2])[0]
        
        if msg_type == ALLOCATE_REQUEST:
            self.handle_allocate(data, client_addr)
        elif msg_type == SEND_INDICATION:
            self.handle_send_indication(data, client_addr)
        elif msg_type == CREATE_PERMISSION_REQUEST:
            self.handle_create_permission(data, client_addr)
        elif msg_type == REFRESH_REQUEST:
            # Simplified - just extend lifetime
            if client_addr in self.allocations:
                self.allocations[client_addr].created_at = time.time()
                self.logger.debug(f"Refreshed allocation for {client_addr}")
    
    def stop(self):
        """Stop TURN server"""
        self.running = False
        
        # Cleanup all allocations
        for allocation in list(self.allocations.values()):
            self._cleanup_allocation(allocation)
        
        if self.server_socket:
            self.server_socket.close()
        
        self.logger.info("TURN server stopped")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description='A.E.T.H.E.R. TURN Server')
    parser.add_argument('--host', default='0.0.0.0', help='Listen address')
    parser.add_argument('--port', type=int, default=TURN_PORT, help='Listen port')
    parser.add_argument('--realm', default=DEFAULT_REALM, help='TURN realm')
    parser.add_argument('--username', default=DEFAULT_USERNAME, help='Username')
    parser.add_argument('--password', default=DEFAULT_PASSWORD, help='Password')
    parser.add_argument('--relay-start', type=int, default=40000, help='Relay port start')
    parser.add_argument('--relay-end', type=int, default=40100, help='Relay port end')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("  A.E.T.H.E.R. TURN Server")
    print("=" * 60)
    print(f"  Listen: {args.host}:{args.port}")
    print(f"  Realm: {args.realm}")
    print(f"  Username: {args.username}")
    print(f"  Password: {args.password}")
    print(f"  Relay Ports: {args.relay_start}-{args.relay_end}")
    print("=" * 60)
    print()
    print("Starting TURN server... Press Ctrl+C to stop.")
    print()
    
    server = TurnServer(
        host=args.host,
        port=args.port,
        realm=args.realm,
        username=args.username,
        password=args.password,
        relay_port_start=args.relay_start,
        relay_port_end=args.relay_end
    )
    
    try:
        server.start()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.stop()


if __name__ == '__main__':
    main()
