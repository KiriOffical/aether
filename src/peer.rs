//! Peer management and connection state tracking

use std::collections::{HashMap, HashSet, VecDeque};
use std::net::SocketAddr;
use std::time::{Duration, Instant};
use crate::crypto::NodeIdentity;
use crate::message::{Endpoint, DisconnectReason};
use crate::error::{Error, Result};

/// Maximum age of peer information before eviction
pub const PEER_TTL: Duration = Duration::from_secs(24 * 60 * 60); // 24 hours

/// Maximum peers to share in PEX message
pub const MAX_PEX_PEERS: usize = 50;

/// Connection state for a peer
#[derive(Debug, Clone)]
pub struct Peer {
    /// Node ID of the peer
    pub node_id: [u8; 32],
    /// Remote address
    pub remote_addr: SocketAddr,
    /// Advertised listening address
    pub listen_addr: Option<SocketAddr>,
    /// Supported protocols
    pub protocols: Vec<String>,
    /// Connection state
    pub state: PeerState,
    /// When the connection was established
    pub connected_at: Option<Instant>,
    /// Last activity timestamp
    pub last_activity: Instant,
    /// Round-trip latency measurement
    pub latency: Option<Duration>,
    /// Trust score (0-100)
    pub trust_score: u8,
}

impl Peer {
    pub fn new(node_id: [u8; 32], remote_addr: SocketAddr) -> Self {
        Self {
            node_id,
            remote_addr,
            listen_addr: None,
            protocols: Vec::new(),
            state: PeerState::Connecting,
            connected_at: None,
            last_activity: Instant::now(),
            latency: None,
            trust_score: 50, // Default neutral trust
        }
    }
    
    /// Check if peer is active (connected and responsive)
    pub fn is_active(&self) -> bool {
        matches!(self.state, PeerState::Connected)
    }
    
    /// Check if peer info is stale
    pub fn is_stale(&self) -> bool {
        self.last_activity.elapsed() > PEER_TTL
    }
    
    /// Update last activity timestamp
    pub fn mark_active(&mut self) {
        self.last_activity = Instant::now();
    }
    
    /// Set connection as established
    pub fn set_connected(&mut self, protocols: Vec<String>, listen_addr: Option<SocketAddr>) {
        self.state = PeerState::Connected;
        self.connected_at = Some(Instant::now());
        self.protocols = protocols;
        self.listen_addr = listen_addr;
    }
    
    /// Convert to Endpoint for PEX
    pub fn to_endpoint(&self) -> Option<Endpoint> {
        self.listen_addr.or(Some(self.remote_addr)).map(Endpoint::from_socket_addr)
    }
}

/// Peer connection state
#[derive(Debug, Clone, PartialEq)]
pub enum PeerState {
    /// Initial connection in progress
    Connecting,
    /// Handshake in progress
    Handshaking,
    /// Connection established, ready for messages
    Connected,
    /// Graceful disconnect in progress
    Disconnecting,
    /// Connection closed
    Disconnected,
}

/// Peer manager for tracking all known peers
pub struct PeerManager {
    /// All known peers indexed by node ID
    peers: HashMap<[u8; 32], Peer>,
    /// Peers indexed by remote address for quick lookup
    by_address: HashMap<SocketAddr, [u8; 32]>,
    /// Set of blacklisted node IDs
    blacklist: HashSet<[u8; 32]>,
    /// Set of blacklisted addresses
    blacklist_addr: HashSet<SocketAddr>,
    /// Maximum number of connections
    max_connections: usize,
    /// Our own node identity
    identity: NodeIdentity,
}

impl PeerManager {
    pub fn new(identity: NodeIdentity, max_connections: usize) -> Self {
        Self {
            peers: HashMap::new(),
            by_address: HashMap::new(),
            blacklist: HashSet::new(),
            blacklist_addr: HashSet::new(),
            max_connections,
            identity,
        }
    }
    
    /// Add or update a peer
    pub fn add_peer(&mut self, peer: Peer) {
        let node_id = peer.node_id;
        
        if let Some(existing) = self.peers.get(&node_id) {
            // Update address mapping if changed
            if existing.remote_addr != peer.remote_addr {
                self.by_address.remove(&existing.remote_addr);
            }
        }
        
        self.by_address.insert(peer.remote_addr, node_id);
        self.peers.insert(node_id, peer);
    }
    
    /// Get peer by node ID
    pub fn get_peer(&self, node_id: &[u8; 32]) -> Option<&Peer> {
        self.peers.get(node_id)
    }
    
    /// Get mutable peer by node ID
    pub fn get_peer_mut(&mut self, node_id: &[u8; 32]) -> Option<&mut Peer> {
        self.peers.get_mut(node_id)
    }
    
    /// Get peer by address
    pub fn get_peer_by_addr(&self, addr: &SocketAddr) -> Option<&Peer> {
        self.by_address.get(addr).and_then(|id| self.peers.get(id))
    }
    
    /// Remove peer by node ID
    pub fn remove_peer(&mut self, node_id: &[u8; 32]) -> Option<Peer> {
        if let Some(peer) = self.peers.remove(node_id) {
            self.by_address.remove(&peer.remote_addr);
            Some(peer)
        } else {
            None
        }
    }
    
    /// Mark peer as disconnected
    pub fn disconnect_peer(&mut self, node_id: &[u8; 32]) {
        if let Some(peer) = self.peers.get_mut(node_id) {
            peer.state = PeerState::Disconnected;
            peer.connected_at = None;
        }
    }
    
    /// Add node to blacklist
    pub fn blacklist(&mut self, node_id: &[u8; 32]) {
        self.blacklist.insert(*node_id);
    }
    
    /// Add address to blacklist
    pub fn blacklist_address(&mut self, addr: SocketAddr) {
        self.blacklist_addr.insert(addr);
    }
    
    /// Check if node is blacklisted
    pub fn is_blacklisted(&self, node_id: &[u8; 32]) -> bool {
        self.blacklist.contains(node_id)
    }
    
    /// Check if address is blacklisted
    pub fn is_address_blacklisted(&self, addr: &SocketAddr) -> bool {
        self.blacklist_addr.contains(addr)
    }
    
    /// Get count of active connections
    pub fn active_count(&self) -> usize {
        self.peers.values().filter(|p| p.is_active()).count()
    }
    
    /// Check if we can accept more connections
    pub fn can_accept(&self) -> bool {
        self.active_count() < self.max_connections
    }
    
    /// Get random active peers for PEX (peer exchange)
    pub fn get_random_peers(&self, limit: usize) -> Vec<Endpoint> {
        let active: Vec<&Peer> = self.peers.values()
            .filter(|p| p.is_active())
            .collect();
        
        if active.is_empty() {
            return Vec::new();
        }
        
        use rand::seq::SliceRandom;
        let mut rng = rand::thread_rng();
        let selected: Vec<&Peer> = active.choose_multiple(&mut rng, limit.min(MAX_PEX_PEERS)).collect();
        
        selected.iter()
            .filter_map(|p| p.to_endpoint())
            .collect()
    }
    
    /// Get closest peers to a target node ID (for DHT)
    pub fn get_closest_peers(&self, target: &[u8; 32], k: usize) -> Vec<Peer> {
        let mut peers: Vec<(&Peer, [u8; 32])> = self.peers.values()
            .filter(|p| p.is_active())
            .map(|p| {
                let distance = NodeIdentity::distance(&p.node_id, target);
                (p, distance)
            })
            .collect();
        
        // Sort by distance
        peers.sort_by(|a, b| {
            NodeIdentity::compare_distance(&a.1, &b.1)
        });
        
        peers.into_iter()
            .take(k)
            .map(|(p, _)| p.clone())
            .collect()
    }
    
    /// Get all active peers
    pub fn active_peers(&self) -> Vec<&Peer> {
        self.peers.values().filter(|p| p.is_active()).collect()
    }
    
    /// Evict stale peers
    pub fn evict_stale(&mut self) -> Vec<[u8; 32]> {
        let stale: Vec<[u8; 32]> = self.peers.values()
            .filter(|p| p.is_stale() && !p.is_active())
            .map(|p| p.node_id)
            .collect();
        
        for node_id in &stale {
            self.remove_peer(node_id);
        }
        
        stale
    }
    
    /// Update peer latency measurement
    pub fn update_latency(&mut self, node_id: &[u8; 32], latency: Duration) {
        if let Some(peer) = self.peers.get_mut(node_id) {
            peer.latency = Some(latency);
        }
    }
    
    /// Adjust trust score
    pub fn adjust_trust(&mut self, node_id: &[u8; 32], delta: i8) {
        if let Some(peer) = self.peers.get_mut(node_id) {
            let new_score = (peer.trust_score as i16 + delta as i16).clamp(0, 100) as u8;
            peer.trust_score = new_score;
        }
    }
}

/// Connection event types
#[derive(Debug, Clone)]
pub enum ConnectionEvent {
    /// New inbound connection received
    InboundConnected {
        addr: SocketAddr,
    },
    /// Outbound connection established
    OutboundConnected {
        addr: SocketAddr,
        node_id: [u8; 32],
    },
    /// Connection closed
    Disconnected {
        addr: SocketAddr,
        node_id: Option<[u8; 32]>,
        reason: DisconnectReason,
    },
    /// Handshake completed
    HandshakeComplete {
        node_id: [u8; 32],
        protocols: Vec<String>,
    },
    /// Error during connection
    Error {
        addr: SocketAddr,
        error: Error,
    },
}

/// Pending outbound connections
pub struct PendingConnections {
    /// Connections being established
    pending: HashMap<SocketAddr, Instant>,
    /// Timeout for pending connections
    timeout: Duration,
}

impl PendingConnections {
    pub fn new(timeout: Duration) -> Self {
        Self {
            pending: HashMap::new(),
            timeout,
        }
    }
    
    /// Add a pending connection
    pub fn add(&mut self, addr: SocketAddr) -> bool {
        // Don't add if already pending
        if self.pending.contains_key(&addr) {
            return false;
        }
        
        self.pending.insert(addr, Instant::now());
        true
    }
    
    /// Remove a pending connection
    pub fn remove(&mut self, addr: &SocketAddr) {
        self.pending.remove(addr);
    }
    
    /// Check if connection is pending
    pub fn is_pending(&self, addr: &SocketAddr) -> bool {
        self.pending.contains_key(addr)
    }
    
    /// Clean up timed out pending connections
    pub fn cleanup(&mut self) -> Vec<SocketAddr> {
        let now = Instant::now();
        let timed_out: Vec<SocketAddr> = self.pending.iter()
            .filter(|(_, started)| now.duration_since(**started) > self.timeout)
            .map(|(addr, _)| *addr)
            .collect();
        
        for addr in &timed_out {
            self.pending.remove(addr);
        }
        
        timed_out
    }
}
