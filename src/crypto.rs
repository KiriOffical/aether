//! Cryptographic primitives and node identity management

use ed25519_dalek::{Signer, SigningKey, VerifyingKey, Signature, Verifier};
use rand::rngs::OsRng;
use sha2::{Sha256, Digest};
use std::path::Path;

use crate::error::{Error, Result};

/// Size of node ID in bytes (SHA-256 output)
pub const NODE_ID_SIZE: usize = 32;

/// Size of Ed25519 public key in bytes
pub const PUBLIC_KEY_SIZE: usize = 32;

/// Size of Ed25519 signature in bytes
pub const SIGNATURE_SIZE: usize = 64;

/// Node identity containing Ed25519 keypair
#[derive(Clone)]
pub struct NodeIdentity {
    signing_key: SigningKey,
    verifying_key: VerifyingKey,
    node_id: [u8; NODE_ID_SIZE],
}

impl NodeIdentity {
    /// Generate a new random node identity
    pub fn generate() -> Self {
        let signing_key = SigningKey::generate(&mut OsRng);
        let verifying_key = signing_key.verifying_key();
        
        // Node ID = SHA-256(public_key)
        let node_id = Sha256::digest(verifying_key.as_bytes());
        let mut node_id_array = [0u8; NODE_ID_SIZE];
        node_id_array.copy_from_slice(&node_id);
        
        Self {
            signing_key,
            verifying_key,
            node_id: node_id_array,
        }
    }
    
    /// Load identity from a file, or generate if not exists
    pub fn load_or_generate(path: &str) -> Result<Self> {
        if Path::new(path).exists() {
            Self::load(path)
        } else {
            let identity = Self::generate();
            identity.save(path)?;
            Ok(identity)
        }
    }
    
    /// Load identity from a file
    pub fn load(path: &str) -> Result<Self> {
        let bytes = std::fs::read(path)
            .map_err(|e| Error::Crypto(format!("Failed to read identity file: {}", e)))?;
        
        if bytes.len() != PUBLIC_KEY_SIZE + 64 {
            return Err(Error::Crypto("Invalid identity file size".to_string()));
        }
        
        let signing_key_bytes: &[u8; 64] = bytes[..64].try_into()
            .map_err(|_| Error::Crypto("Invalid signing key size".to_string()))?;
        
        let signing_key = SigningKey::from_bytes(signing_key_bytes);
        let verifying_key = signing_key.verifying_key();
        
        let node_id = Sha256::digest(verifying_key.as_bytes());
        let mut node_id_array = [0u8; NODE_ID_SIZE];
        node_id_array.copy_from_slice(&node_id);
        
        Ok(Self {
            signing_key,
            verifying_key,
            node_id: node_id_array,
        })
    }
    
    /// Save identity to a file
    pub fn save(&self, path: &str) -> Result<()> {
        let mut bytes = Vec::with_capacity(PUBLIC_KEY_SIZE + 64);
        bytes.extend_from_slice(&self.signing_key.to_bytes());
        
        std::fs::write(path, bytes)
            .map_err(|e| Error::Crypto(format!("Failed to write identity file: {}", e)))?;
        
        Ok(())
    }
    
    /// Get the node ID (SHA-256 of public key)
    pub fn node_id(&self) -> &[u8; NODE_ID_SIZE] {
        &self.node_id
    }
    
    /// Get the public key
    pub fn public_key(&self) -> &[u8; PUBLIC_KEY_SIZE] {
        self.verifying_key.as_bytes()
    }
    
    /// Sign data with the node's private key
    pub fn sign(&self, data: &[u8]) -> Signature {
        self.signing_key.sign(data)
    }
    
    /// Verify a signature using a public key
    pub fn verify(public_key: &[u8], data: &[u8], signature: &[u8]) -> Result<()> {
        let pk_bytes: &[u8; PUBLIC_KEY_SIZE] = public_key.try_into()
            .map_err(|_| Error::InvalidSignature)?;
        
        let verifying_key = VerifyingKey::from_bytes(pk_bytes)
            .map_err(|_| Error::InvalidSignature)?;
        
        let sig_bytes: &[u8; SIGNATURE_SIZE] = signature.try_into()
            .map_err(|_| Error::InvalidSignature)?;
        
        let signature = Signature::from_bytes(sig_bytes);
        
        verifying_key.verify(data, &signature)?;
        Ok(())
    }
    
    /// Compute XOR distance between two node IDs (for DHT routing)
    pub fn distance(a: &[u8; NODE_ID_SIZE], b: &[u8; NODE_ID_SIZE]) -> [u8; NODE_ID_SIZE] {
        let mut distance = [0u8; NODE_ID_SIZE];
        for i in 0..NODE_ID_SIZE {
            distance[i] = a[i] ^ b[i];
        }
        distance
    }
    
    /// Compare two distances (returns true if a < b)
    pub fn compare_distance(a: &[u8; NODE_ID_SIZE], b: &[u8; NODE_ID_SIZE]) -> std::cmp::Ordering {
        for i in 0..NODE_ID_SIZE {
            if a[i] != b[i] {
                return a[i].cmp(&b[i]);
            }
        }
        std::cmp::Ordering::Equal
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_identity_generation() {
        let identity = NodeIdentity::generate();
        assert_eq!(identity.node_id().len(), NODE_ID_SIZE);
        assert_eq!(identity.public_key().len(), PUBLIC_KEY_SIZE);
    }
    
    #[test]
    fn test_sign_verify() {
        let identity = NodeIdentity::generate();
        let data = b"test message";
        let signature = identity.sign(data);
        
        assert!(NodeIdentity::verify(
            identity.public_key(),
            data,
            &signature.to_bytes()
        ).is_ok());
    }
    
    #[test]
    fn test_distance() {
        let id1 = NodeIdentity::generate();
        let id2 = NodeIdentity::generate();
        let id3 = NodeIdentity::generate();
        
        let dist1 = NodeIdentity::distance(id1.node_id(), id2.node_id());
        let dist2 = NodeIdentity::distance(id1.node_id(), id3.node_id());
        
        // Distance should be consistent
        assert_eq!(
            NodeIdentity::distance(id2.node_id(), id1.node_id()),
            dist1
        );
        
        // Comparison should work
        let _ = NodeIdentity::compare_distance(&dist1, &dist2);
    }
}
