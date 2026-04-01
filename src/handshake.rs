//! Cryptographic handshake protocol implementation

use std::time::{Duration, Instant, SystemTime};
use crate::crypto::NodeIdentity;
use crate::message::{Hello, HelloAck, Message, Endpoint, Protocol as MsgProtocol};
use crate::error::{Error, Result};
use crate::config::Config;

/// Maximum allowed clock skew for handshake timestamps
pub const MAX_CLOCK_SKEW: Duration = Duration::from_secs(5 * 60); // 5 minutes

/// Handshake timeout
pub const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(10);

/// Protocol version
pub const PROTOCOL_VERSION: u32 = 1;

/// Handshake state machine
#[derive(Debug, Clone)]
pub enum HandshakeState {
    /// Initial state, waiting to send/receive Hello
    Init,
    /// We sent Hello, waiting for HelloAck
    AwaitingAck,
    /// We received Hello, waiting to send HelloAck
    ReceivedHello,
    /// Handshake complete
    Complete,
    /// Handshake failed
    Failed,
}

/// Handshake builder and validator
pub struct Handshake {
    /// Our node identity
    identity: NodeIdentity,
    /// Our configuration
    config: Config,
    /// Current state
    state: HandshakeState,
    /// Remote node ID (once known)
    remote_node_id: Option<[u8; 32]>,
    /// Remote protocols (once known)
    remote_protocols: Vec<String>,
    /// Remote listen address (once known)
    remote_listen_addr: Option<SocketAddr>,
    /// When handshake started
    started_at: Instant,
    /// Challenge bytes for proof-of-work (optional)
    challenge: Option<Vec<u8>>,
}

use std::net::SocketAddr;

impl Handshake {
    /// Create a new handshake initiator
    pub fn new_initiator(identity: NodeIdentity, config: Config) -> Self {
        Self {
            identity,
            config,
            state: HandshakeState::Init,
            remote_node_id: None,
            remote_protocols: Vec::new(),
            remote_listen_addr: None,
            started_at: Instant::now(),
            challenge: None,
        }
    }
    
    /// Create a new handshake responder
    pub fn new_responder(identity: NodeIdentity, config: Config) -> Self {
        Self {
            identity,
            config,
            state: HandshakeState::Init,
            remote_node_id: None,
            remote_protocols: Vec::new(),
            remote_listen_addr: None,
            started_at: Instant::now(),
            challenge: None,
        }
    }
    
    /// Build outbound Hello message
    pub fn build_hello(&mut self, remote_addr: SocketAddr) -> Result<Message> {
        let timestamp = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .map_err(|_| Error::Crypto("System clock error".to_string()))?
            .as_micros() as u64;
        
        let listen_addr = self.config.public_addr
            .unwrap_or_else(|| {
                // Use local address with configured port
                SocketAddr::new(remote_addr.ip(), self.config.listen_addr.port())
            });
        
        let hello = Hello {
            version: PROTOCOL_VERSION,
            node_id: *self.identity.node_id(),
            timestamp,
            protocols: self.config.supported_protocols(),
            listen_addr: Endpoint::from_socket_addr(listen_addr),
            signature: Vec::new(), // Will be signed below
        };
        
        // Sign: node_id || timestamp
        let mut sign_data = Vec::with_capacity(32 + 8);
        sign_data.extend_from_slice(&hello.node_id);
        sign_data.extend_from_slice(&hello.timestamp.to_be_bytes());
        
        let signature = self.identity.sign(&sign_data);
        
        let hello = Hello {
            signature: signature.to_bytes().to_vec(),
            ..hello
        };
        
        self.state = HandshakeState::AwaitingAck;
        
        Ok(Message::Hello(hello))
    }
    
    /// Process received Hello message, return HelloAck response
    pub fn receive_hello(&mut self, hello: &Hello, local_addr: SocketAddr) -> Result<Message> {
        if !matches!(self.state, HandshakeState::Init) {
            return Err(Error::HandshakeFailed("Invalid state for Hello".to_string()));
        }
        
        // Validate version
        if hello.version != PROTOCOL_VERSION {
            return Err(Error::VersionMismatch(hello.version, PROTOCOL_VERSION));
        }
        
        // Validate timestamp
        let now = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .map_err(|_| Error::Crypto("System clock error".to_string()))?
            .as_micros() as u64;
        
        let timestamp_micros = hello.timestamp as i64;
        let now_micros = now as i64;
        let skew = (now_micros - timestamp_micros).unsigned_abs();
        
        if skew > MAX_CLOCK_SKEW.as_micros() as i64 {
            return Err(Error::TimestampOutOfRange(timestamp_micros));
        }
        
        // Verify signature
        let mut sign_data = Vec::with_capacity(32 + 8);
        sign_data.extend_from_slice(&hello.node_id);
        sign_data.extend_from_slice(&hello.timestamp.to_be_bytes());
        
        NodeIdentity::verify(&hello.node_id, &sign_data, &hello.signature)?;
        
        // Store remote info
        self.remote_node_id = Some(hello.node_id);
        self.remote_protocols = hello.protocols.clone();
        self.remote_listen_addr = hello.listen_addr.to_socket_addr();
        
        // Generate challenge for proof-of-work (optional hardening)
        use rand::Rng;
        let mut rng = rand::thread_rng();
        let challenge: Vec<u8> = (0..32).map(|_| rng.gen()).collect();
        self.challenge = Some(challenge.clone());
        
        // Build HelloAck
        let timestamp = now;
        
        let ack = HelloAck {
            node_id: *self.identity.node_id(),
            timestamp,
            protocols: self.config.supported_protocols(),
            listen_addr: Endpoint::from_socket_addr(local_addr),
            signature: Vec::new(),
            challenge,
        };
        
        // Sign: node_id || timestamp
        let mut sign_data = Vec::with_capacity(32 + 8);
        sign_data.extend_from_slice(&ack.node_id);
        sign_data.extend_from_slice(&ack.timestamp.to_be_bytes());
        
        let signature = self.identity.sign(&sign_data);
        
        let ack = HelloAck {
            signature: signature.to_bytes().to_vec(),
            ..ack
        };
        
        self.state = HandshakeState::ReceivedHello;
        
        Ok(Message::HelloAck(ack))
    }
    
    /// Process received HelloAck, complete handshake
    pub fn receive_hello_ack(&mut self, ack: &HelloAck) -> Result<()> {
        if !matches!(self.state, HandshakeState::AwaitingAck) {
            return Err(Error::HandshakeFailed("Invalid state for HelloAck".to_string()));
        }
        
        // Validate version compatibility
        // (HelloAck doesn't have version, assume compatible if we got here)
        
        // Validate timestamp
        let now = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .map_err(|_| Error::Crypto("System clock error".to_string()))?
            .as_micros() as u64;
        
        let timestamp_micros = ack.timestamp as i64;
        let now_micros = now as i64;
        let skew = (now_micros - timestamp_micros).unsigned_abs();
        
        if skew > MAX_CLOCK_SKEW.as_micros() as i64 {
            return Err(Error::TimestampOutOfRange(timestamp_micros));
        }
        
        // Verify signature
        let mut sign_data = Vec::with_capacity(32 + 8);
        sign_data.extend_from_slice(&ack.node_id);
        sign_data.extend_from_slice(&ack.timestamp.to_be_bytes());
        
        NodeIdentity::verify(&ack.node_id, &sign_data, &ack.signature)?;
        
        // Check timeout
        if self.started_at.elapsed() > HANDSHAKE_TIMEOUT {
            return Err(Error::Timeout);
        }
        
        // Store remote info
        self.remote_node_id = Some(ack.node_id);
        self.remote_protocols = ack.protocols.clone();
        self.remote_listen_addr = ack.listen_addr.to_socket_addr();
        self.challenge = Some(ack.challenge.clone());
        
        self.state = HandshakeState::Complete;
        
        Ok(())
    }
    
    /// Check if handshake is complete
    pub fn is_complete(&self) -> bool {
        matches!(self.state, HandshakeState::Complete)
    }
    
    /// Check if handshake has timed out
    pub fn is_timed_out(&self) -> bool {
        self.started_at.elapsed() > HANDSHAKE_TIMEOUT
    }
    
    /// Get remote node ID
    pub fn remote_node_id(&self) -> Option<&[u8; 32]> {
        self.remote_node_id.as_ref()
    }
    
    /// Get remote protocols
    pub fn remote_protocols(&self) -> &[String] {
        &self.remote_protocols
    }
    
    /// Get remote listen address
    pub fn remote_listen_addr(&self) -> Option<SocketAddr> {
        self.remote_listen_addr
    }
    
    /// Get challenge bytes
    pub fn challenge(&self) -> Option<&[u8]> {
        self.challenge.as_deref()
    }
    
    /// Check protocol compatibility
    pub fn has_protocol(&self, protocol: &str) -> bool {
        self.remote_protocols.iter().any(|p| p == protocol)
    }
}

/// Verify Hello message without state (for stateless validation)
pub fn verify_hello(hello: &Hello) -> Result<()> {
    // Validate version
    if hello.version != PROTOCOL_VERSION {
        return Err(Error::VersionMismatch(hello.version, PROTOCOL_VERSION));
    }
    
    // Validate timestamp
    let now = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .map_err(|_| Error::Crypto("System clock error".to_string()))?
        .as_micros() as u64;
    
    let timestamp_micros = hello.timestamp as i64;
    let now_micros = now as i64;
    let skew = (now_micros - timestamp_micros).unsigned_abs();
    
    if skew > MAX_CLOCK_SKEW.as_micros() as i64 {
        return Err(Error::TimestampOutOfRange(timestamp_micros));
    }
    
    // Verify signature
    let mut sign_data = Vec::with_capacity(32 + 8);
    sign_data.extend_from_slice(&hello.node_id);
    sign_data.extend_from_slice(&hello.timestamp.to_be_bytes());
    
    NodeIdentity::verify(&hello.node_id, &sign_data, &hello.signature)?;
    
    Ok(())
}

/// Verify HelloAck message without state
pub fn verify_hello_ack(ack: &HelloAck) -> Result<()> {
    // Validate timestamp
    let now = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .map_err(|_| Error::Crypto("System clock error".to_string()))?
        .as_micros() as u64;
    
    let timestamp_micros = ack.timestamp as i64;
    let now_micros = now as i64;
    let skew = (now_micros - timestamp_micros).unsigned_abs();
    
    if skew > MAX_CLOCK_SKEW.as_micros() as i64 {
        return Err(Error::TimestampOutOfRange(timestamp_micros));
    }
    
    // Verify signature
    let mut sign_data = Vec::with_capacity(32 + 8);
    sign_data.extend_from_slice(&ack.node_id);
    sign_data.extend_from_slice(&ack.timestamp.to_be_bytes());
    
    NodeIdentity::verify(&ack.node_id, &sign_data, &ack.signature)?;
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_handshake_round_trip() {
        let identity_alice = NodeIdentity::generate();
        let identity_bob = NodeIdentity::generate();
        
        let config = Config::default();
        
        // Alice initiates
        let mut alice = Handshake::new_initiator(identity_alice.clone(), config.clone());
        let mut bob = Handshake::new_responder(identity_bob.clone(), config.clone());
        
        // Alice sends Hello
        let remote_addr = "127.0.0.1:7821".parse().unwrap();
        let hello_msg = alice.build_hello(remote_addr).unwrap();
        
        if let Message::Hello(hello) = hello_msg {
            // Bob receives Hello, sends HelloAck
            let local_addr = "127.0.0.1:7822".parse().unwrap();
            let ack_msg = bob.receive_hello(&hello, local_addr).unwrap();
            
            if let Message::HelloAck(ack) = ack_msg {
                // Alice receives HelloAck, handshake complete
                alice.receive_hello_ack(&ack).unwrap();
                
                assert!(alice.is_complete());
                assert_eq!(alice.remote_node_id(), Some(identity_bob.node_id()));
            } else {
                panic!("Expected HelloAck");
            }
        } else {
            panic!("Expected Hello");
        }
    }
    
    #[test]
    fn test_handshake_invalid_signature() {
        let identity = NodeIdentity::generate();
        let config = Config::default();
        let mut handshake = Handshake::new_responder(identity, config);
        
        let mut hello = Hello {
            version: PROTOCOL_VERSION,
            node_id: [1u8; 32],
            timestamp: SystemTime::now()
                .duration_since(SystemTime::UNIX_EPOCH)
                .unwrap()
                .as_micros() as u64,
            protocols: vec!["/aether/1.0.0".to_string()],
            listen_addr: Endpoint::from_socket_addr("127.0.0.1:7821".parse().unwrap()),
            signature: vec![0u8; 64], // Invalid signature
        };
        
        let result = handshake.receive_hello(&hello, "127.0.0.1:7822".parse().unwrap());
        assert!(matches!(result, Err(Error::InvalidSignature)));
    }
    
    #[test]
    fn test_handshake_version_mismatch() {
        let identity = NodeIdentity::generate();
        let config = Config::default();
        let mut handshake = Handshake::new_responder(identity, config);
        
        let hello = Hello {
            version: 999, // Wrong version
            node_id: [1u8; 32],
            timestamp: SystemTime::now()
                .duration_since(SystemTime::UNIX_EPOCH)
                .unwrap()
                .as_micros() as u64,
            protocols: vec![],
            listen_addr: Endpoint::default(),
            signature: vec![],
        };
        
        let result = handshake.receive_hello(&hello, "127.0.0.1:7822".parse().unwrap());
        assert!(matches!(result, Err(Error::VersionMismatch(_, _))));
    }
}
