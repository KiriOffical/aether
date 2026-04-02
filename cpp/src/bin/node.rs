//! A.E.T.H.E.R. Node - P2P Daemon Entry Point

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use tracing::{info, error, Level};
use tracing_subscriber::FmtSubscriber;
use hex;

use aether_core::{Config, Protocol};

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    // Setup logging
    let subscriber = FmtSubscriber::builder()
        .with_max_level(Level::INFO)
        .with_target(true)
        .with_thread_ids(true)
        .finish();
    
    tracing::subscriber::set_global_default(subscriber)?;
    
    info!("╔═══════════════════════════════════════════════════════════╗");
    info!("║         A.E.T.H.E.R. Node - Starting                      ║");
    info!("║  Asynchronous Edge-Tolerant Holographic Execution Runtime ║");
    info!("╚═══════════════════════════════════════════════════════════╝");
    
    // Load configuration
    let config = load_config()?;
    
    info!("Configuration loaded:");
    info!("  Listen address: {}", config.listen_addr);
    info!("  Max connections: {}", config.max_connections);
    info!("  Data directory: {}", config.data_dir);
    info!("  Log level: {}", config.log_level);
    
    // Create data directory
    std::fs::create_dir_all(&config.data_dir)?;
    
    // Setup shutdown signal handler
    let shutdown = Arc::new(AtomicBool::new(false));
    let shutdown_clone = shutdown.clone();
    
    ctrlc::set_handler(move || {
        info!("\nShutdown signal received...");
        shutdown_clone.store(true, Ordering::SeqCst);
    })?;
    
    // Initialize and start the P2P protocol
    let protocol = Protocol::new(config).await?;
    
    info!("Node ID: {}", hex::encode(protocol.node_id()));
    info!("Listening on {}", protocol.listen_addr());
    
    // Main loop - wait for shutdown
    while !shutdown.load(Ordering::SeqCst) {
        tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
    }
    
    // Graceful shutdown
    info!("Shutting down gracefully...");
    protocol.shutdown();
    
    // Allow time for cleanup
    tokio::time::sleep(tokio::time::Duration::from_millis(500)).await;
    
    info!("A.E.T.H.E.R. Node stopped");
    
    Ok(())
}

fn load_config() -> anyhow::Result<Config> {
    // Check for config file in current directory or data dir
    let config_paths = [
        "aether.toml".to_string(),
        "aether_data/config.toml".to_string(),
        std::env::var("USERPROFILE")
            .map(|p| format!("{}/.aether/config.toml", p))
            .unwrap_or_default(),
    ];
    
    for path in &config_paths {
        if std::path::Path::new(path).exists() {
            info!("Loading configuration from: {}", path);
            return Ok(Config::load(path)?);
        }
    }
    
    // Use default config
    info!("No configuration file found, using defaults");
    let config = Config::default();
    
    // Save default config for future use
    let config_path = "aether.toml";
    if let Err(e) = config.save(config_path) {
        info!("Could not save default config: {}", e);
    } else {
        info!("Default configuration saved to: {}", config_path);
    }
    
    Ok(config)
}
