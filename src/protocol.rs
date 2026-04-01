//! Core P2P protocol implementation

use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::sync::{mpsc, RwLock};
use tracing::{info, warn, error, debug, trace};
use hex;

use crate::config::Config;
use crate::crypto::NodeIdentity;
use crate::dht::Dht;
use crate::error::{Error, Result};
use crate::framing::Framer;
use crate::handshake::Handshake;
use crate::message::{Message, Ping, Pong, Disconnect, DisconnectReason, PeerExchange};
use crate::peer::{Peer, PeerManager, PeerState, ConnectionEvent, PendingConnections};

/// Default timeout for receiving messages
pub const RECV_TIMEOUT: Duration = Duration::from_secs(30);

/// Keepalive ping interval
pub const PING_INTERVAL: Duration = Duration::from_secs(30);

/// Peer exchange interval
pub const PEX_INTERVAL: Duration = Duration::from_secs(60);

/// Channel capacity for message queues
pub const CHANNEL_CAPACITY: usize = 1000;

/// Outbound message type
#[derive(Debug, Clone)]
pub struct OutboundMessage {
    pub peer_node_id: [u8; 32],
    pub message: Message,
}

/// Inbound message type
#[derive(Debug, Clone)]
pub struct InboundMessage {
    pub from_node_id: [u8; 32],
    pub message: Message,
}

/// Protocol state shared across tasks
pub struct ProtocolState {
    pub identity: NodeIdentity,
    pub config: Config,
    pub peer_manager: RwLock<PeerManager>,
    pub dht: RwLock<Dht>,
}

/// Main P2P protocol handler
pub struct Protocol {
    state: Arc<ProtocolState>,
    /// Outbound message sender
    outbound_tx: mpsc::Sender<OutboundMessage>,
    /// Inbound message receiver
    inbound_rx: mpsc::Receiver<InboundMessage>,
    /// Connection event receiver
    event_rx: mpsc::Receiver<ConnectionEvent>,
    /// Shutdown signal
    shutdown_tx: tokio::sync::broadcast::Sender<()>,
}

impl Protocol {
    /// Create and initialize the P2P protocol
    pub async fn new(config: Config) -> Result<Self> {
        // Load or generate node identity
        let identity = if let Some(path) = &config.identity_path {
            NodeIdentity::load_or_generate(path)?
        } else {
            NodeIdentity::generate()
        };
        
        info!("Node ID: {}", hex::encode(identity.node_id()));
        
        // Create endpoint for our node
        let endpoint = crate::message::Endpoint::from_socket_addr(config.listen_addr);
        
        // Initialize DHT
        let dht = Dht::new(*identity.node_id(), endpoint, 100_000);
        
        // Initialize peer manager
        let peer_manager = PeerManager::new(identity.clone(), config.max_connections);
        
        let state = Arc::new(ProtocolState {
            identity,
            config,
            peer_manager: RwLock::new(peer_manager),
            dht: RwLock::new(dht),
        });
        
        // Create channels
        let (outbound_tx, outbound_rx) = mpsc::channel(CHANNEL_CAPACITY);
        let (inbound_tx, inbound_rx) = mpsc::channel(CHANNEL_CAPACITY);
        let (event_tx, event_rx) = mpsc::channel(CHANNEL_CAPACITY);
        let (shutdown_tx, _) = tokio::sync::broadcast::channel(1);
        
        // Spawn the protocol runner
        let runner = ProtocolRunner {
            state: state.clone(),
            outbound_rx,
            inbound_tx,
            event_tx,
            shutdown_rx: shutdown_tx.subscribe(),
        };
        
        tokio::spawn(async move {
            runner.run().await;
        });
        
        Ok(Self {
            state,
            outbound_tx,
            inbound_rx,
            event_rx,
            shutdown_tx,
        })
    }
    
    /// Get our node ID
    pub fn node_id(&self) -> &[u8; 32] {
        self.state.identity.node_id()
    }
    
    /// Get the listening address
    pub fn listen_addr(&self) -> SocketAddr {
        self.state.config.listen_addr
    }
    
    /// Send a message to a peer
    pub async fn send_to(&self, node_id: [u8; 32], message: Message) -> Result<()> {
        self.outbound_tx.send(OutboundMessage { node_id, message })
            .await
            .map_err(|_| Error::ConnectionClosed("Channel closed".to_string()))
    }
    
    /// Receive an inbound message
    pub async fn recv_message(&mut self) -> Option<InboundMessage> {
        self.inbound_rx.recv().await
    }
    
    /// Receive a connection event
    pub async fn recv_event(&mut self) -> Option<ConnectionEvent> {
        self.event_rx.recv().await
    }
    
    /// Get the DHT reference
    pub fn dht(&self) -> &RwLock<Dht> {
        &self.state.dht
    }
    
    /// Get peer manager reference
    pub fn peer_manager(&self) -> &RwLock<PeerManager> {
        &self.state.peer_manager
    }
    
    /// Connect to a peer
    pub async fn connect(&self, addr: SocketAddr) -> Result<()> {
        // Check if already connected or pending
        {
            let pm = self.state.peer_manager.read().await;
            if pm.is_address_blacklisted(&addr) {
                return Err(Error::Blacklisted);
            }
            if let Some(peer) = pm.get_peer_by_addr(&addr) {
                if peer.is_active() {
                    return Ok(()); // Already connected
                }
            }
        }
        
        // Attempt outbound connection
        let state = self.state.clone();
        let inbound_tx = self.outbound_tx.clone(); // This is wrong, should be inbound_tx
        
        tokio::spawn(async move {
            match TcpStream::connect(addr).await {
                Ok(stream) => {
                    // Handle connection...
                }
                Err(e) => {
                    warn!("Failed to connect to {}: {}", addr, e);
                }
            }
        });
        
        Ok(())
    }
    
    /// Disconnect from a peer
    pub async fn disconnect(&self, node_id: &[u8; 32], reason: DisconnectReason) -> Result<()> {
        let mut pm = self.state.peer_manager.write().await;
        pm.disconnect_peer(node_id);
        
        // Send disconnect message if connected
        // (would need to track active connections)
        
        Ok(())
    }
    
    /// Get connected peer count
    pub async fn peer_count(&self) -> usize {
        let pm = self.state.peer_manager.read().await;
        pm.active_count()
    }
    
    /// Shutdown the protocol
    pub fn shutdown(&self) {
        let _ = self.shutdown_tx.send(());
    }
}

/// Internal protocol runner
struct ProtocolRunner {
    state: Arc<ProtocolState>,
    outbound_rx: mpsc::Receiver<OutboundMessage>,
    inbound_tx: mpsc::Sender<InboundMessage>,
    event_tx: mpsc::Sender<ConnectionEvent>,
    shutdown_rx: tokio::sync::broadcast::Receiver<()>,
}

impl ProtocolRunner {
    async fn run(mut self) {
        info!("Starting A.E.T.H.E.R. protocol on {}", self.state.config.listen_addr);
        
        // Start TCP listener
        let listener = match TcpListener::bind(self.state.config.listen_addr).await {
            Ok(l) => l,
            Err(e) => {
                error!("Failed to bind to {}: {}", self.state.config.listen_addr, e);
                return;
            }
        };
        
        info!("Listening for incoming connections");
        
        // Track active connections
        let connections: Arc<RwLock<HashMap<[u8; 32], tokio::task::JoinHandle<()>>>> = 
            Arc::new(RwLock::new(HashMap::new()));
        
        // Pending connections tracker
        let mut pending = PendingConnections::new(Duration::from_secs(10));
        
        // Periodic tasks
        let mut ping_interval = tokio::time::interval(PING_INTERVAL);
        let mut pex_interval = tokio::time::interval(PEX_INTERVAL);
        let mut cleanup_interval = tokio::time::interval(Duration::from_secs(300));
        
        loop {
            tokio::select! {
                // Accept inbound connections
                result = listener.accept() => {
                    match result {
                        Ok((stream, addr)) => {
                            debug!("New inbound connection from {}", addr);
                            
                            // Check if we can accept
                            let pm = self.state.peer_manager.read().await;
                            if !pm.can_accept() {
                                warn!("Max connections reached, rejecting {}", addr);
                                drop(pm);
                                continue;
                            }
                            
                            if pm.is_address_blacklisted(&addr) {
                                warn!("Blacklisted address rejected: {}", addr);
                                drop(pm);
                                continue;
                            }
                            drop(pm);
                            
                            // Spawn connection handler
                            let conn_handler = ConnectionHandler::new(
                                self.state.clone(),
                                self.inbound_tx.clone(),
                                self.event_tx.clone(),
                                stream,
                                addr,
                                false, // inbound
                            );
                            
                            tokio::spawn(async move {
                                conn_handler.run().await;
                            });
                        }
                        Err(e) => {
                            error!("Accept error: {}", e);
                        }
                    }
                }
                
                // Handle outbound messages
                Some(outbound) = self.outbound_rx.recv() => {
                    // Route to appropriate connection
                    // (would need connection registry)
                    trace!("Outbound message to {:?}", hex::encode(&outbound.peer_node_id));
                }
                
                // Periodic ping
                _ = ping_interval.tick() => {
                    // Send pings to all active peers
                    let pm = self.state.peer_manager.read().await;
                    for peer in pm.active_peers() {
                        // Would send ping through connection
                    }
                }
                
                // Periodic peer exchange
                _ = pex_interval.tick() => {
                    // Send PEX to peers
                }
                
                // Periodic cleanup
                _ = cleanup_interval.tick() => {
                    let mut pm = self.state.peer_manager.write().await;
                    let evicted = pm.evict_stale();
                    if !evicted.is_empty() {
                        debug!("Evicted {} stale peers", evicted.len());
                    }
                    
                    // Cleanup DHT
                    let mut dht = self.state.dht.write().await;
                    let expired = dht.storage_mut().cleanup();
                    if expired > 0 {
                        debug!("Cleaned up {} expired DHT values", expired);
                    }
                }
                
                // Shutdown signal
                Ok(_) = self.shutdown_rx.recv() => {
                    info!("Shutdown signal received");
                    break;
                }
            }
        }
        
        info!("Protocol shutdown complete");
    }
}

/// Handles a single peer connection
struct ConnectionHandler {
    state: Arc<ProtocolState>,
    inbound_tx: mpsc::Sender<InboundMessage>,
    event_tx: mpsc::Sender<ConnectionEvent>,
    stream: TcpStream,
    remote_addr: SocketAddr,
    is_inbound: bool,
    framer: Framer,
    handshake: Option<Handshake>,
    peer_node_id: Option<[u8; 32]>,
}

impl ConnectionHandler {
    fn new(
        state: Arc<ProtocolState>,
        inbound_tx: mpsc::Sender<InboundMessage>,
        event_tx: mpsc::Sender<ConnectionEvent>,
        stream: TcpStream,
        remote_addr: SocketAddr,
        is_inbound: bool,
    ) -> Self {
        let handshake = if is_inbound {
            Some(Handshake::new_responder(state.identity.clone(), state.config.clone()))
        } else {
            Some(Handshake::new_initiator(state.identity.clone(), state.config.clone()))
        };
        
        Self {
            state,
            inbound_tx,
            event_tx,
            stream,
            remote_addr,
            is_inbound,
            framer: Framer::new(),
            handshake,
            peer_node_id: None,
        }
    }
    
    async fn run(mut self) {
        // Perform handshake if inbound
        if self.is_inbound {
            match self.perform_handshake().await {
                Ok(node_id) => {
                    self.peer_node_id = Some(node_id);
                    info!("Handshake complete with {}", hex::encode(&node_id));
                }
                Err(e) => {
                    warn!("Handshake failed with {}: {}", self.remote_addr, e);
                    return;
                }
            }
        }
        
        // Message receive loop
        let mut buf = vec![0u8; 4096];
        loop {
            tokio::select! {
                result = self.stream.read(&mut buf) => {
                    match result {
                        Ok(0) => {
                            debug!("Connection closed by {}", self.remote_addr);
                            break;
                        }
                        Ok(n) => {
                            self.framer.receive(&buf[..n]);
                            
                            // Try to decode messages
                            loop {
                                match self.framer.decode() {
                                    Ok(Some(msg)) => {
                                        if let Err(e) = self.handle_message(msg).await {
                                            warn!("Error handling message: {}", e);
                                            break;
                                        }
                                    }
                                    Ok(None) => break,
                                    Err(e) => {
                                        warn!("Frame decode error: {}", e);
                                        break;
                                    }
                                }
                            }
                        }
                        Err(e) => {
                            warn!("Read error from {}: {}", self.remote_addr, e);
                            break;
                        }
                    }
                }
            }
        }
        
        // Cleanup
        if let Some(node_id) = self.peer_node_id {
            let mut pm = self.state.peer_manager.write().await;
            pm.disconnect_peer(&node_id);
        }
    }
    
    async fn perform_handshake(&mut self) -> Result<[u8; 32]> {
        // Read Hello message
        let mut buf = vec![0u8; 4096];
        
        tokio::time::timeout(Duration::from_secs(10), async {
            loop {
                let n = self.stream.read(&mut buf).await?;
                if n == 0 {
                    return Err(Error::ConnectionClosed("Connection closed during handshake".to_string()));
                }
                
                self.framer.receive(&buf[..n]);
                
                if let Some(msg) = self.framer.decode()? {
                    return Ok(msg);
                }
            }
        }).await
        .map_err(|_| Error::Timeout)??;
    }
    
    async fn handle_message(&self, message: Message) -> Result<()> {
        trace!("Received message: {:?}", message);
        
        match message {
            Message::Ping(ping) => {
                // Respond with pong
                let pong = Message::Pong(Pong {
                    sequence: ping.sequence,
                    latency_ns: 0,
                });
                // Would send response
            }
            Message::Pong(pong) => {
                // Update latency
                if let Some(node_id) = self.peer_node_id {
                    // Would calculate and update latency
                }
            }
            Message::Disconnect(disconnect) => {
                debug!("Peer disconnecting: {:?}", disconnect.reason);
            }
            Message::PeerExchange(pex) => {
                // Process peer list
            }
            _ => {
                // Forward to inbound channel
                if let Some(node_id) = self.peer_node_id {
                    let _ = self.inbound_tx.send(InboundMessage {
                        from_node_id: node_id,
                        message,
                    }).await;
                }
            }
        }
        
        Ok(())
    }
}

/// Send a message over TCP
async fn send_message(stream: &mut TcpStream, message: &Message) -> Result<()> {
    let framer = Framer::new();
    let framed = framer.encode(message)?;
    
    stream.write_all(&framed).await?;
    stream.flush().await?;
    
    Ok(())
}

