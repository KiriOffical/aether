//! Configuration for A.E.T.H.E.R. node

use serde::{Deserialize, Serialize};
use std::net::SocketAddr;

/// Default listening port for A.E.T.H.E.R. nodes
pub const DEFAULT_PORT: u16 = 7821;

/// Default maximum number of connections
pub const DEFAULT_MAX_CONNECTIONS: usize = 10000;

/// Default message maximum size in bytes
pub const DEFAULT_MAX_MESSAGE_SIZE: usize = 64 * 1024 * 1024; // 64 MB

/// Bootstrap nodes for initial network entry
pub const DEFAULT_BOOTSTRAP_NODES: &[&str] = &[
    "bootstrap.aether.network:7821",
    "archive.org.aether.network:7821",
    "myrient.org.aether.network:7821",
];

/// Node configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    /// Node identity (generated if not provided)
    pub identity_path: Option<String>,
    
    /// Listening address
    pub listen_addr: SocketAddr,
    
    /// Advertised public address (for NAT traversal)
    pub public_addr: Option<SocketAddr>,
    
    /// Maximum concurrent connections
    pub max_connections: usize,
    
    /// Maximum message size in bytes
    pub max_message_size: usize,
    
    /// Bootstrap nodes for initial peer discovery
    pub bootstrap_nodes: Vec<String>,
    
    /// Enable kernel-bypass I/O optimizations (io_uring, IOCP)
    pub kernel_bypass: bool,
    
    /// Data directory for storage
    pub data_dir: String,
    
    /// Log level (trace, debug, info, warn, error)
    pub log_level: String,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            identity_path: None,
            listen_addr: format!("0.0.0.0:{}", DEFAULT_PORT)
                .parse()
                .expect("Invalid listen address"),
            public_addr: None,
            max_connections: DEFAULT_MAX_CONNECTIONS,
            max_message_size: DEFAULT_MAX_MESSAGE_SIZE,
            bootstrap_nodes: DEFAULT_BOOTSTRAP_NODES.iter().map(|s| s.to_string()).collect(),
            kernel_bypass: false, // Disabled by default, enabled on supported platforms
            data_dir: "aether_data".to_string(),
            log_level: "info".to_string(),
        }
    }
}

impl Config {
    /// Load configuration from a TOML file
    pub fn load(path: &str) -> Result<Self, crate::Error> {
        let content = std::fs::read_to_string(path)
            .map_err(|e| crate::Error::Config(format!("Failed to read config file: {}", e)))?;
        
        let config: Config = toml::from_str(&content)
            .map_err(|e| crate::Error::Config(format!("Failed to parse config: {}", e)))?;
        
        Ok(config)
    }
    
    /// Save configuration to a TOML file
    pub fn save(&self, path: &str) -> Result<(), crate::Error> {
        let content = toml::to_string_pretty(self)
            .map_err(|e| crate::Error::Config(format!("Failed to serialize config: {}", e)))?;
        
        std::fs::write(path, content)
            .map_err(|e| crate::Error::Config(format!("Failed to write config file: {}", e)))?;
        
        Ok(())
    }
    
    /// Get the protocol version
    pub fn protocol_version(&self) -> u32 {
        1
    }
    
    /// Get supported protocol identifiers
    pub fn supported_protocols(&self) -> Vec<String> {
        vec![
            "/aether/1.0.0".to_string(),
            "/aether/dht/1.0.0".to_string(),
        ]
    }
}
