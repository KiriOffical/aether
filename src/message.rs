//! Protocol message types and serialization using bincode

use serde::{Serialize, Deserialize};
use bytes::{Buf, BufMut, BytesMut};
use byteorder::{BigEndian, WriteBytesExt, ReadBytesExt};
use std::io::Cursor;

use crate::error::{Error, Result};

/// Maximum frame size (64 MB)
pub const MAX_FRAME_SIZE: usize = 64 * 1024 * 1024;

/// Protocol message envelope
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Message {
    // Handshake
    Hello(Hello),
    HelloAck(HelloAck),
    
    // DHT
    FindNode(FindNode),
    FindNodeResponse(FindNodeResponse),
    StoreValue(StoreValue),
    GetValue(GetValue),
    GetValueResponse(GetValueResponse),
    
    // Data Transfer
    FragmentRequest(FragmentRequest),
    FragmentResponse(FragmentResponse),
    
    // Maintenance
    Ping(Ping),
    Pong(Pong),
    Disconnect(Disconnect),
    PeerExchange(PeerExchange),
    
    // Errors
    Error(ProtocolError),
}

impl Message {
    /// Serialize message to bytes using bincode
    pub fn encode(&self) -> Result<Vec<u8>> {
        bincode::serialize(self)
            .map_err(|e| Error::Serialization(prost::DecodeError::new(format!("bincode error: {}", e))))
    }
    
    /// Deserialize message from bytes
    pub fn decode(data: &[u8]) -> Result<Self> {
        bincode::deserialize(data)
            .map_err(|e| Error::Serialization(prost::DecodeError::new(format!("bincode error: {}", e))))
    }
}

// ============================================
// Handshake Messages
// ============================================

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Hello {
    pub version: u32,
    pub node_id: [u8; 32],
    pub timestamp: u64,
    pub protocols: Vec<String>,
    pub listen_addr: Endpoint,
    pub signature: Vec<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HelloAck {
    pub node_id: [u8; 32],
    pub timestamp: u64,
    pub protocols: Vec<String>,
    pub listen_addr: Endpoint,
    pub signature: Vec<u8>,
    pub challenge: Vec<u8>,
}

// ============================================
// DHT Messages
// ============================================

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FindNode {
    pub target_id: Vec<u8>,
    pub k: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FindNodeResponse {
    pub nodes: Vec<NodeInfo>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NodeInfo {
    pub node_id: Vec<u8>,
    pub endpoint: Endpoint,
    pub distance: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StoreValue {
    pub key: Vec<u8>,
    pub value: Vec<u8>,
    pub expiration: u64,
    pub signature: Vec<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GetValue {
    pub key: Vec<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GetValueResponse {
    pub result: GetValueResult,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum GetValueResult {
    Value(Vec<u8>),
    NotFound(Vec<NodeInfo>),
}

// ============================================
// Data Transfer Messages
// ============================================

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FragmentRequest {
    pub fragment_id: Vec<u8>,
    pub fragment_index: u32,
    pub file_id: Vec<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FragmentResponse {
    pub result: FragmentResult,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum FragmentResult {
    Data {
        fragment_id: Vec<u8>,
        payload: Vec<u8>,
        proof: Vec<u8>,
    },
    NotFound {
        reason: String,
    },
}

// ============================================
// Maintenance Messages
// ============================================

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Ping {
    pub sequence: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Pong {
    pub sequence: u64,
    pub latency_ns: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Disconnect {
    pub reason: DisconnectReason,
    pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum DisconnectReason {
    Shutdown,
    ProtocolError,
    Timeout,
    Maintenance,
    Blacklisted,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PeerExchange {
    pub peers: Vec<Endpoint>,
    pub timestamp: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProtocolError {
    pub code: ErrorCode,
    pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum ErrorCode {
    Ok = 0,
    InvalidFormat = 1,
    InvalidSignature = 2,
    VersionMismatch = 3,
    NotFound = 4,
    RateLimited = 5,
    InternalError = 6,
    Blacklisted = 7,
    ProtocolError = 8,
}

// ============================================
// Endpoint / Address
// ============================================

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct Endpoint {
    pub proto: Protocol,
    pub address: Vec<u8>,
    pub port: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub enum Protocol {
    #[default]
    Ip4,
    Ip6,
}

impl Endpoint {
    /// Create endpoint from SocketAddr
    pub fn from_socket_addr(addr: std::net::SocketAddr) -> Self {
        match addr {
            std::net::SocketAddr::V4(v4) => Self {
                proto: Protocol::Ip4,
                address: v4.ip().octets().to_vec(),
                port: addr.port() as u32,
            },
            std::net::SocketAddr::V6(v6) => Self {
                proto: Protocol::Ip6,
                address: v6.ip().octets().to_vec(),
                port: addr.port() as u32,
            },
        }
    }
    
    /// Convert to SocketAddr
    pub fn to_socket_addr(&self) -> Option<std::net::SocketAddr> {
        match self.proto {
            Protocol::Ip4 => {
                if self.address.len() == 4 {
                    let ip: [u8; 4] = self.address[..4].try_into().ok()?;
                    Some(std::net::SocketAddr::new(
                        std::net::Ipv4Addr::from(ip),
                        self.port as u16,
                    ))
                } else {
                    None
                }
            }
            Protocol::Ip6 => {
                if self.address.len() == 16 {
                    let ip: [u8; 16] = self.address[..16].try_into().ok()?;
                    Some(std::net::SocketAddr::new(
                        std::net::Ipv6Addr::from(ip),
                        self.port as u16,
                    ))
                } else {
                    None
                }
            }
        }
    }
}
