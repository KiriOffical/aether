"""
NAT traversal and hole-punching support for A.E.T.H.E.R.
Implements STUN, UPnP, and TCP/UDP hole-punching techniques.
"""

import socket
import threading
import time
import random
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple, Callable
from enum import IntEnum
import logging
import struct


logger = logging.getLogger("aether.nat")


class NATType(IntEnum):
    """Types of NAT configurations."""
    OPEN = 0              # No NAT, public IP
    FULL_CONE = 1         # Full cone NAT
    RESTRICTED_CONE = 2   # Restricted cone NAT
    PORT_RESTRICTED = 3   # Port-restricted cone NAT
    SYMMETRIC = 4         # Symmetric NAT (hardest to traverse)
    UNKNOWN = 5


class HolePunchState(IntEnum):
    """States for hole-punching process."""
    IDLE = 0
    INITIATED = 1
    COORDINATING = 2
    PUNCHING = 3
    CONNECTED = 4
    FAILED = 5


@dataclass
class PublicEndpoint:
    """Public endpoint information."""
    public_ip: str
    public_port: int
    private_ip: str
    private_port: int
    nat_type: NATType = NATType.UNKNOWN
    
    def __str__(self) -> str:
        return f"{self.public_ip}:{self.public_port}"
        
    def to_tuple(self) -> Tuple[str, int]:
        return (self.public_ip, self.public_port)


@dataclass
class HolePunchContext:
    """Context for a hole-punching attempt."""
    peer_id: bytes
    peer_public_endpoint: Optional[PublicEndpoint]
    local_endpoint: Optional[PublicEndpoint]
    state: HolePunchState = HolePunchState.IDLE
    introduction_socket: Optional[socket.socket] = None
    start_time: float = field(default_factory=time.time)
    attempts: int = 0
    max_attempts: int = 10
    
    def should_retry(self) -> bool:
        return self.attempts < self.max_attempts


class STUNClient:
    """
    Simple STUN client for discovering public IP and port.
    Uses public STUN servers to determine NAT mapping.
    """
    
    # Public STUN servers
    DEFAULT_STUN_SERVERS = [
        ("stun.l.google.com", 19302),
        ("stun1.l.google.com", 19302),
        ("stun2.l.google.com", 19302),
        ("stun.stunprotocol.org", 3478),
        ("stun.voip.blackberry.com", 3478),
    ]
    
    # STUN message types
    BIND_REQUEST = 0x0001
    BIND_RESPONSE = 0x0101
    
    def __init__(self, timeout: float = 5.0):
        self.timeout = timeout
        self._socket: Optional[socket.socket] = None
        
    def discover(self, stun_server: Optional[Tuple[str, int]] = None) -> Optional[PublicEndpoint]:
        """
        Discover public endpoint using STUN.
        Returns public endpoint information.
        """
        server = stun_server or random.choice(self.DEFAULT_STUN_SERVERS)
        
        try:
            # Create UDP socket
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._socket.settimeout(self.timeout)
            
            # Get local endpoint
            local_ip = self._get_local_ip()
            local_port = 0  # Let OS assign
            
            self._socket.bind((local_ip, local_port))
            private_port = self._socket.getsockname()[1]
            
            # Send STUN binding request
            transaction_id = self._generate_transaction_id()
            request = self._create_stun_request(transaction_id)
            
            self._socket.sendto(request, server)
            
            # Wait for response
            data, addr = self._socket.recvfrom(512)
            
            # Parse response
            public_ip, public_port = self._parse_stun_response(data, transaction_id)
            
            if public_ip and public_port:
                # Detect NAT type
                nat_type = self._detect_nat_type(server)
                
                return PublicEndpoint(
                    public_ip=public_ip,
                    public_port=public_port,
                    private_ip=local_ip,
                    private_port=private_port,
                    nat_type=nat_type
                )
                
        except socket.timeout:
            logger.debug(f"STUN request timed out for {server}")
        except Exception as e:
            logger.debug(f"STUN discovery error: {e}")
        finally:
            if self._socket:
                self._socket.close()
                self._socket = None
                
        return None
        
    def _get_local_ip(self) -> str:
        """Get local IP address."""
        try:
            # Create temporary socket to determine local IP
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except:
            return "0.0.0.0"
            
    def _generate_transaction_id(self) -> bytes:
        """Generate random STUN transaction ID."""
        return random.randbytes(16)
        
    def _create_stun_request(self, transaction_id: bytes) -> bytes:
        """Create STUN binding request."""
        # STUN header: type (2) + length (2) + magic cookie (4) + transaction ID (12)
        header = struct.pack(
            "!HHI12s",
            self.BIND_REQUEST,
            0,  # Length
            0x2112A442,  # Magic cookie
            transaction_id
        )
        return header
        
    def _parse_stun_response(self, data: bytes, 
                            expected_transaction_id: bytes) -> Tuple[Optional[str], Optional[int]]:
        """Parse STUN binding response."""
        if len(data) < 20:
            return None, None
            
        msg_type, msg_length, magic_cookie, transaction_id = struct.unpack(
            "!HHI12s", data[:20]
        )
        
        if msg_type != self.BIND_RESPONSE:
            return None, None
            
        if transaction_id != expected_transaction_id:
            return None, None
            
        # Parse attributes
        offset = 20
        while offset < len(data):
            attr_type, attr_length = struct.unpack("!HH", data[offset:offset+4])
            offset += 4
            
            if attr_type == 0x0001:  # MAPPED-ADDRESS
                # Skip first byte (reserved)
                family = data[offset + 1]
                port = struct.unpack("!H", data[offset + 2:offset + 4])[0]
                
                if family == 0x01:  # IPv4
                    ip_bytes = data[offset + 4:offset + 8]
                    ip = ".".join(str(b) for b in ip_bytes)
                    return ip, port
                    
            offset += attr_length
            
        return None, None
        
    def _detect_nat_type(self, primary_server: Tuple[str, int]) -> NATType:
        """
        Detect NAT type using multiple STUN servers.
        Simplified detection - full implementation would be more complex.
        """
        try:
            # Test with primary server
            endpoint1 = self.discover(primary_server)
            
            # Test with different server
            other_servers = [s for s in self.DEFAULT_STUN_SERVERS if s != primary_server]
            if other_servers:
                endpoint2 = self.discover(random.choice(other_servers))
                
                if endpoint1 and endpoint2:
                    # If public IP:port is same, it's cone NAT
                    if (endpoint1.public_ip == endpoint2.public_ip and
                        endpoint1.public_port == endpoint2.public_port):
                        return NATType.FULL_CONE
                    else:
                        return NATType.SYMMETRIC
                        
            if endpoint1:
                # If we got a response, at least some NAT traversal is possible
                return NATType.RESTRICTED_CONE
                
        except Exception as e:
            logger.debug(f"NAT type detection error: {e}")
            
        return NATType.UNKNOWN


class UPnPManager:
    """
    Manages UPnP port forwarding.
    Uses SSDP/UPnP to configure router port mappings.
    """
    
    def __init__(self, timeout: float = 3.0):
        self.timeout = timeout
        self._igd_url: Optional[str] = None
        self._control_url: Optional[str] = None
        self._service_type: Optional[str] = None
        
    def discover_router(self) -> bool:
        """Discover UPnP-enabled router."""
        try:
            # SSDP discovery message
            ssdp_request = (
                'M-SEARCH * HTTP/1.1\r\n'
                'HOST: 239.255.255.250:1900\r\n'
                'MAN: "ssdp:discover"\r\n'
                'MX: 2\r\n'
                'ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n'
                '\r\n'
            ).encode()
            
            # Send SSDP request
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.settimeout(self.timeout)
            
            sock.sendto(ssdp_request, ('239.255.255.250', 1900))
            
            # Collect responses
            responses = []
            while True:
                try:
                    data, addr = sock.recvfrom(4096)
                    responses.append(data)
                except socket.timeout:
                    break
                    
            sock.close()
            
            # Parse responses to find IGD
            for response in responses:
                if b'InternetGatewayDevice' in response:
                    # Extract IGD URL (simplified)
                    # Full implementation would parse XML
                    logger.debug("UPnP router discovered")
                    return True
                    
        except Exception as e:
            logger.debug(f"UPnP discovery error: {e}")
            
        return False
        
    def add_port_mapping(self, internal_port: int, 
                        external_port: Optional[int] = None,
                        protocol: str = "TCP",
                        description: str = "A.E.T.H.E.R. Node") -> bool:
        """
        Add port mapping on router.
        Returns True if successful.
        """
        # Simplified implementation
        # Full implementation would send SOAP request to router
        logger.debug(f"UPnP port mapping requested: {internal_port}")
        return False
        
    def remove_port_mapping(self, external_port: int, 
                           protocol: str = "TCP") -> bool:
        """Remove port mapping from router."""
        logger.debug(f"UPnP port mapping removal requested: {external_port}")
        return False


class HolePuncher:
    """
    Implements TCP/UDP hole-punching for NAT traversal.
    Coordinates with introduction server to establish P2P connections.
    """
    
    def __init__(self, introduction_servers: Optional[List[Tuple[str, int]]] = None):
        self.introduction_servers = introduction_servers or [
            ("intro1.aether.network", 7820),
            ("intro2.aether.network", 7820),
        ]
        self.stun_client = STUNClient()
        self.upnp_manager = UPnPManager()
        self._active_punches: Dict[bytes, HolePunchContext] = {}
        self._lock = threading.Lock()
        
    def get_public_endpoint(self) -> Optional[PublicEndpoint]:
        """Get public endpoint for this node."""
        # Try STUN first
        endpoint = self.stun_client.discover()
        if endpoint:
            logger.info(f"Discovered public endpoint via STUN: {endpoint}")
            return endpoint
            
        # Try UPnP
        if self.upnp_manager.discover_router():
            logger.info("UPnP router discovered")
            # UPnP mapping would be configured here
            
        logger.warning("Could not determine public endpoint")
        return None
        
    def initiate_hole_punch(self, peer_id: bytes,
                           peer_endpoint: PublicEndpoint) -> HolePunchContext:
        """Initiate hole-punching with a peer."""
        with self._lock:
            context = HolePunchContext(
                peer_id=peer_id,
                peer_public_endpoint=peer_endpoint
            )
            self._active_punches[peer_id] = context
            
        # Start hole-punching in background
        thread = threading.Thread(
            target=self._punch_thread,
            args=(context,),
            daemon=True
        )
        thread.start()
        
        return context
        
    def _punch_thread(self, context: HolePunchContext):
        """Background thread for hole-punching."""
        try:
            context.state = HolePunchState.INITIATED
            
            # Get our public endpoint
            context.local_endpoint = self.get_public_endpoint()
            
            if not context.local_endpoint:
                context.state = HolePunchState.FAILED
                logger.error("Hole-punch failed: could not determine local endpoint")
                return
                
            context.state = HolePunchState.COORDINATING
            
            # Contact introduction server
            intro_socket = self._contact_introduction_server(context)
            if not intro_socket:
                context.state = HolePunchState.FAILED
                return
                
            context.introduction_socket = intro_socket
            context.state = HolePunchState.PUNCHING
            
            # Start punching
            success = self._perform_hole_punch(context)
            
            if success:
                context.state = HolePunchState.CONNECTED
                logger.info(
                    f"Hole-punch successful with {context.peer_id.hex()[:8]}..."
                )
            else:
                context.state = HolePunchState.FAILED
                logger.warning(
                    f"Hole-punch failed with {context.peer_id.hex()[:8]}..."
                )
                
        except Exception as e:
            logger.error(f"Hole-punch error: {e}")
            context.state = HolePunchState.FAILED
            
    def _contact_introduction_server(self, context: HolePunchContext
                                    ) -> Optional[socket.socket]:
        """Contact introduction server to coordinate hole-punch."""
        for server_addr in self.introduction_servers:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.settimeout(5.0)
                
                # Send introduction request
                # Format: PEER_ID + OUR_ENDPOINT
                message = (
                    context.peer_id +
                    context.local_endpoint.public_ip.encode() +
                    struct.pack("!H", context.local_endpoint.public_port)
                )
                
                sock.sendto(message, server_addr)
                
                # Wait for response with peer's info
                data, addr = sock.recvfrom(512)
                
                # Parse response
                # (In real implementation, would extract peer's endpoint)
                
                return sock
                
            except Exception as e:
                logger.debug(f"Introduction server {server_addr} failed: {e}")
                
        return None
        
    def _perform_hole_punch(self, context: HolePunchContext) -> bool:
        """
        Perform the actual hole-punching.
        Sends packets to peer's predicted ports.
        """
        if not context.peer_public_endpoint or not context.introduction_socket:
            return False
            
        peer_ip = context.peer_public_endpoint.public_ip
        base_port = context.peer_public_endpoint.public_port
        
        # Try multiple ports (for symmetric NAT)
        ports_to_try = [
            base_port,
            base_port + 1,
            base_port - 1,
            base_port + 2,
            base_port - 2,
        ]
        
        # Send packets to open holes
        for attempt in range(context.max_attempts):
            context.attempts += 1
            
            for port in ports_to_try:
                try:
                    # Send UDP packet
                    message = b"PUNCH"  # Hole-punch packet
                    context.introduction_socket.sendto(
                        message, (peer_ip, port)
                    )
                    
                    # Check for response
                    context.introduction_socket.settimeout(0.5)
                    try:
                        data, addr = context.introduction_socket.recvfrom(512)
                        if data == b"PUNCH":
                            # Connection established!
                            logger.debug(f"Connected via port {port}")
                            return True
                    except socket.timeout:
                        pass
                        
                except Exception as e:
                    logger.debug(f"Punch attempt failed: {e}")
                    
            # Wait before next attempt
            time.sleep(0.5)
            
        return False
        
    def cancel_hole_punch(self, peer_id: bytes):
        """Cancel an active hole-punch attempt."""
        with self._lock:
            context = self._active_punches.pop(peer_id, None)
            if context and context.introduction_socket:
                context.introduction_socket.close()
                
    def cleanup(self):
        """Cleanup all active hole-punch attempts."""
        with self._lock:
            for context in self._active_punches.values():
                if context.introduction_socket:
                    context.introduction_socket.close()
            self._active_punches.clear()


class NATTraversalManager:
    """
    High-level manager for NAT traversal.
    Combines STUN, UPnP, and hole-punching strategies.
    """
    
    def __init__(self, local_port: int):
        self.local_port = local_port
        self.hole_puncher = HolePuncher()
        self._public_endpoint: Optional[PublicEndpoint] = None
        self._callbacks: Dict[str, List[Callable]] = {}
        self._running = False
        self._refresh_thread: Optional[threading.Thread] = None
        
    def start(self):
        """Start NAT traversal manager."""
        self._running = True
        
        # Discover public endpoint
        self._public_endpoint = self.hole_puncher.get_public_endpoint()
        
        # Start periodic refresh
        self._refresh_thread = threading.Thread(
            target=self._refresh_loop,
            daemon=True
        )
        self._refresh_thread.start()
        
        logger.info(f"NAT traversal started, endpoint: {self._public_endpoint}")
        
    def stop(self):
        """Stop NAT traversal manager."""
        self._running = False
        self.hole_puncher.cleanup()
        if self._refresh_thread:
            self._refresh_thread.join(timeout=5.0)
            
    def _refresh_loop(self):
        """Periodically refresh NAT mappings."""
        while self._running:
            time.sleep(300)  # Every 5 minutes
            
            if not self._running:
                break
                
            # Refresh public endpoint
            new_endpoint = self.hole_puncher.get_public_endpoint()
            if new_endpoint:
                old_endpoint = self._public_endpoint
                self._public_endpoint = new_endpoint
                
                if old_endpoint != new_endpoint:
                    logger.info(f"Public endpoint changed: {new_endpoint}")
                    self._trigger_callback('endpoint_changed', new_endpoint)
                    
    def get_public_endpoint(self) -> Optional[PublicEndpoint]:
        """Get current public endpoint."""
        return self._public_endpoint
        
    def is_behind_nat(self) -> bool:
        """Check if node is behind NAT."""
        if not self._public_endpoint:
            return True
            
        return (
            self._public_endpoint.nat_type != NATType.OPEN and
            self._public_endpoint.public_ip != self._public_endpoint.private_ip
        )
        
    def get_nat_type(self) -> NATType:
        """Get detected NAT type."""
        if not self._public_endpoint:
            return NATType.UNKNOWN
        return self._public_endpoint.nat_type
        
    def connect_to_peer(self, peer_id: bytes,
                       peer_endpoint: PublicEndpoint) -> bool:
        """
        Attempt to connect to a peer, using NAT traversal if needed.
        Returns True if connection established.
        """
        # If both peers are public, direct connection
        if not self.is_behind_nat() and peer_endpoint.nat_type == NATType.OPEN:
            return self._direct_connect(peer_endpoint)
            
        # Need hole-punching
        context = self.hole_puncher.initiate_hole_punch(peer_id, peer_endpoint)
        
        # Wait for completion (with timeout)
        start_time = time.time()
        while time.time() - start_time < 30:
            if context.state == HolePunchState.CONNECTED:
                return True
            elif context.state == HolePunchState.FAILED:
                return False
            time.sleep(0.5)
            
        return False
        
    def _direct_connect(self, endpoint: PublicEndpoint) -> bool:
        """Attempt direct TCP connection."""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(10.0)
            sock.connect((endpoint.public_ip, endpoint.public_port))
            sock.close()
            return True
        except Exception as e:
            logger.debug(f"Direct connect failed: {e}")
            return False
            
    def on_endpoint_changed(self, callback: Callable[[PublicEndpoint], None]):
        """Register callback for endpoint changes."""
        if 'endpoint_changed' not in self._callbacks:
            self._callbacks['endpoint_changed'] = []
        self._callbacks['endpoint_changed'].append(callback)
        
    def _trigger_callback(self, event: str, *args):
        """Trigger event callbacks."""
        for callback in self._callbacks.get(event, []):
            try:
                callback(*args)
            except Exception as e:
                logger.error(f"Callback error for {event}: {e}")
