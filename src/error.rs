//! Error types for A.E.T.H.E.R. protocol operations

use thiserror::Error;

/// Result type alias for A.E.T.H.E.R. operations
pub type Result<T> = std::result::Result<T, Error>;

/// Core error types for the A.E.T.H.E.R. protocol
#[derive(Error, Debug)]
pub enum Error {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Connection closed: {0}")]
    ConnectionClosed(String),

    #[error("Handshake failed: {0}")]
    HandshakeFailed(String),

    #[error("Invalid message format: {0}")]
    InvalidMessage(String),

    #[error("Invalid signature")]
    InvalidSignature,

    #[error("Message too large: {0} bytes (max: {1})")]
    MessageTooLarge(usize, usize),

    #[error("Frame size exceeds maximum: {0}")]
    FrameSizeExceeded(usize),

    #[error("Node not found: {0:?}")]
    NodeNotFound(Vec<u8>),

    #[error("Value not found for key: {0:?}")]
    ValueNotFound(Vec<u8>),

    #[error("DHT error: {0}")]
    Dht(String),

    #[error("Rate limited")]
    RateLimited,

    #[error("Blacklisted")]
    Blacklisted,

    #[error("Version mismatch: local={0}, remote={1}")]
    VersionMismatch(u32, u32),

    #[error("Timestamp out of range: {0}")]
    TimestampOutOfRange(i64),

    #[error("Serialization error: {0}")]
    Serialization(String),

    #[error("Configuration error: {0}")]
    Config(String),

    #[error("Cryptographic error: {0}")]
    Crypto(String),

    #[error("Task cancelled")]
    Cancelled,

    #[error("Timeout")]
    Timeout,

    #[error("Protocol error: {0}")]
    Protocol(String),
}

impl From<ed25519_dalek::SignatureError> for Error {
    fn from(_: ed25519_dalek::SignatureError) -> Self {
        Error::InvalidSignature
    }
}
