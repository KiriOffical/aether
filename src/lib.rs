//! A.E.T.H.E.R. Core P2P Protocol Implementation
//! 
//! This crate provides the foundational peer-to-peer networking layer
//! for the A.E.T.H.E.R. decentralized web substrate.

pub mod config;
pub mod crypto;
pub mod dht;
pub mod error;
pub mod framing;
pub mod handshake;
pub mod message;
pub mod peer;
pub mod protocol;

pub use config::Config;
pub use crypto::NodeIdentity;
pub use dht::Dht;
pub use error::{Error, Result};
pub use protocol::Protocol;
