//! Distributed Hash Table (DHT) implementation using Kademlia-style routing

use std::collections::{BTreeMap, HashMap};
use std::time::{Duration, Instant};
use crate::crypto::NodeIdentity;
use crate::message::{NodeInfo, Endpoint};
use crate::error::{Error, Result};
use hex;

/// Size of K-bucket (number of entries per bucket)
pub const K_BUCKET_SIZE: usize = 20;

/// Number of bits in node ID (SHA-256 = 256 bits)
pub const ID_BITS: usize = 256;

/// Default replication factor (k parameter)
pub const REPLICATION_FACTOR: usize = 20;

/// Value expiration time
pub const VALUE_TTL: Duration = Duration::from_secs(24 * 60 * 60); // 24 hours

/// Refresh interval for buckets
pub const BUCKET_REFRESH: Duration = Duration::from_secs(60 * 60); // 1 hour

/// A single entry in a K-bucket
#[derive(Debug, Clone)]
pub struct BucketEntry {
    pub node_id: [u8; 32],
    pub endpoint: Endpoint,
    pub last_seen: Instant,
    pub is_self: bool,
}

impl BucketEntry {
    pub fn new(node_id: [u8; 32], endpoint: Endpoint) -> Self {
        Self {
            node_id,
            endpoint,
            last_seen: Instant::now(),
            is_self: false,
        }
    }
    
    pub fn mark_seen(&mut self) {
        self.last_seen = Instant::now();
    }
}

/// A K-bucket containing up to K node entries
#[derive(Debug, Clone)]
pub struct KBucket {
    entries: Vec<BucketEntry>,
    max_size: usize,
}

impl KBucket {
    pub fn new(max_size: usize) -> Self {
        Self {
            entries: Vec::with_capacity(max_size),
            max_size,
        }
    }
    
    /// Add or update an entry
    pub fn add(&mut self, entry: BucketEntry) -> Option<BucketEntry> {
        // Check if entry already exists
        if let Some(existing) = self.entries.iter_mut().find(|e| e.node_id == entry.node_id) {
            existing.mark_seen();
            existing.endpoint = entry.endpoint;
            return None;
        }
        
        // If bucket is full, return the oldest entry for potential eviction
        if self.entries.len() >= self.max_size {
            return Some(entry);
        }
        
        self.entries.push(entry);
        None
    }
    
    /// Remove an entry
    pub fn remove(&mut self, node_id: &[u8; 32]) -> Option<BucketEntry> {
        if let Some(pos) = self.entries.iter().position(|e| &e.node_id == node_id) {
            Some(self.entries.remove(pos))
        } else {
            None
        }
    }
    
    /// Get all entries
    pub fn entries(&self) -> &[BucketEntry] {
        &self.entries
    }
    
    /// Get the oldest entry (for eviction)
    pub fn oldest(&self) -> Option<&BucketEntry> {
        self.entries.iter().min_by_key(|e| e.last_seen)
    }
    
    /// Check if bucket is full
    pub fn is_full(&self) -> bool {
        self.entries.len() >= self.max_size
    }
}

/// Stored value in the DHT
#[derive(Debug, Clone)]
pub struct StoredValue {
    pub key: Vec<u8>,
    pub value: Vec<u8>,
    pub publisher: [u8; 32],
    pub created_at: Instant,
    pub expires_at: Instant,
    pub signature: Vec<u8>,
}

impl StoredValue {
    pub fn new(key: Vec<u8>, value: Vec<u8>, publisher: [u8; 32], signature: Vec<u8>) -> Self {
        let now = Instant::now();
        Self {
            key,
            value,
            publisher,
            created_at: now,
            expires_at: now + VALUE_TTL,
            signature,
        }
    }
    
    pub fn is_expired(&self) -> bool {
        Instant::now() > self.expires_at
    }
}

/// Kademlia-style DHT routing table
pub struct RoutingTable {
    /// Our node ID
    node_id: [u8; 32],
    /// K-buckets organized by distance (index = position of first differing bit)
    buckets: Vec<KBucket>,
    /// Our own entry
    self_entry: BucketEntry,
}

impl RoutingTable {
    pub fn new(node_id: [u8; 32], endpoint: Endpoint) -> Self {
        let mut buckets = Vec::with_capacity(ID_BITS);
        for _ in 0..ID_BITS {
            buckets.push(KBucket::new(K_BUCKET_SIZE));
        }
        
        let mut self_entry = BucketEntry::new(node_id, endpoint);
        self_entry.is_self = true;
        
        Self {
            node_id,
            buckets,
            self_entry,
        }
    }
    
    /// Get the bucket index for a given distance
    fn bucket_index(distance: &[u8; 32]) -> usize {
        for i in 0..ID_BITS {
            let byte_index = i / 8;
            let bit_index = 7 - (i % 8);
            if distance[byte_index] & (1 << bit_index) != 0 {
                return i;
            }
        }
        0
    }
    
    /// Add a node to the routing table
    pub fn add_node(&mut self, node_id: [u8; 32], endpoint: Endpoint) -> Result<()> {
        if node_id == self.node_id {
            return Ok(()); // Don't add self
        }
        
        let distance = NodeIdentity::distance(&self.node_id, &node_id);
        let index = Self::bucket_index(&distance);
        
        let entry = BucketEntry::new(node_id, endpoint);
        
        if let Some(evict_candidate) = self.buckets[index].add(entry) {
            // Bucket is full, could implement ping-based eviction here
            // For now, just return the candidate for potential removal
            return Err(Error::Dht(format!(
                "Bucket {} full, candidate for eviction: {:?}",
                index,
                hex::encode(&evict_candidate.node_id)
            )));
        }
        
        Ok(())
    }
    
    /// Remove a node from the routing table
    pub fn remove_node(&mut self, node_id: &[u8; 32]) {
        if node_id == &self.node_id {
            return;
        }
        
        let distance = NodeIdentity::distance(&self.node_id, node_id);
        let index = Self::bucket_index(&distance);
        self.buckets[index].remove(node_id);
    }
    
    /// Find the k closest nodes to a target
    pub fn find_closest(&self, target: &[u8; 32], k: usize) -> Vec<NodeInfo> {
        let mut all_nodes: Vec<(&BucketEntry, [u8; 32])> = Vec::new();
        
        // Collect all nodes with their distances
        for bucket in &self.buckets {
            for entry in bucket.entries() {
                let distance = NodeIdentity::distance(&entry.node_id, target);
                all_nodes.push((entry, distance));
            }
        }
        
        // Sort by distance
        all_nodes.sort_by(|a, b| {
            NodeIdentity::compare_distance(&a.1, &b.1)
        });
        
        // Return top k
        all_nodes.into_iter()
            .take(k)
            .map(|(entry, distance)| NodeInfo {
                node_id: entry.node_id.to_vec(),
                endpoint: entry.endpoint.clone(),
                distance: u32::from_be_bytes([
                    distance[0], distance[1], distance[2], distance[3]
                ]),
            })
            .collect()
    }
    
    /// Get all nodes in routing table
    pub fn all_nodes(&self) -> Vec<NodeInfo> {
        let mut nodes = Vec::new();
        
        for bucket in &self.buckets {
            for entry in bucket.entries() {
                nodes.push(NodeInfo {
                    node_id: entry.node_id.to_vec(),
                    endpoint: entry.endpoint.clone(),
                    distance: 0, // Not calculated for all_nodes
                });
            }
        }
        
        nodes
    }
    
    /// Get count of nodes in routing table
    pub fn node_count(&self) -> usize {
        self.buckets.iter().map(|b| b.entries().len()).sum()
    }
    
    /// Get our node ID
    pub fn node_id(&self) -> &[u8; 32] {
        &self.node_id
    }
}

/// DHT storage for key-value pairs
pub struct DhtStorage {
    /// Stored values indexed by key hash
    values: HashMap<Vec<u8>, StoredValue>,
    /// Maximum number of values to store
    max_values: usize,
}

impl DhtStorage {
    pub fn new(max_values: usize) -> Self {
        Self {
            values: HashMap::new(),
            max_values,
        }
    }
    
    /// Store a value
    pub fn store(&mut self, value: StoredValue) -> Result<()> {
        // Check if we need to evict
        if self.values.len() >= self.max_values && !self.values.contains_key(&value.key) {
            // Evict expired values first
            let expired: Vec<_> = self.values.iter()
                .filter(|(_, v)| v.is_expired())
                .map(|(k, _)| k.clone())
                .collect();
            
            for key in expired {
                self.values.remove(&key);
            }
            
            // If still full, evict oldest
            if self.values.len() >= self.max_values {
                let oldest_key = self.values.iter()
                    .min_by_key(|(_, v)| v.created_at)
                    .map(|(k, _)| k.clone());
                
                if let Some(key) = oldest_key {
                    self.values.remove(&key);
                }
            }
        }
        
        self.values.insert(value.key.clone(), value);
        Ok(())
    }
    
    /// Get a value by key
    pub fn get(&self, key: &[u8]) -> Option<&StoredValue> {
        self.values.get(key).filter(|v| !v.is_expired())
    }
    
    /// Remove expired values
    pub fn cleanup(&mut self) -> usize {
        let expired: Vec<_> = self.values.iter()
            .filter(|(_, v)| v.is_expired())
            .map(|(k, _)| k.clone())
            .collect();
        
        let count = expired.len();
        for key in expired {
            self.values.remove(&key);
        }
        count
    }
    
    /// Get count of stored values
    pub fn value_count(&self) -> usize {
        self.values.len()
    }
}

/// Main DHT struct combining routing and storage
pub struct Dht {
    routing_table: RoutingTable,
    storage: DhtStorage,
    /// Pending queries (for iterative lookups)
    pending_queries: HashMap<Vec<u8>, Instant>,
    /// Query timeout
    query_timeout: Duration,
}

impl Dht {
    pub fn new(node_id: [u8; 32], endpoint: Endpoint, max_values: usize) -> Self {
        Self {
            routing_table: RoutingTable::new(node_id, endpoint),
            storage: DhtStorage::new(max_values),
            pending_queries: HashMap::new(),
            query_timeout: Duration::from_secs(30),
        }
    }
    
    /// Get the routing table
    pub fn routing_table(&self) -> &RoutingTable {
        &self.routing_table
    }
    
    /// Get mutable routing table
    pub fn routing_table_mut(&mut self) -> &mut RoutingTable {
        &mut self.routing_table
    }
    
    /// Get storage
    pub fn storage(&self) -> &DhtStorage {
        &self.storage
    }
    
    /// Get mutable storage
    pub fn storage_mut(&mut self) -> &mut DhtStorage {
        &mut self.storage
    }
    
    /// Store a value locally
    pub fn store(&mut self, key: Vec<u8>, value: Vec<u8>, publisher: [u8; 32], signature: Vec<u8>) -> Result<()> {
        let stored_value = StoredValue::new(key.clone(), value, publisher, signature);
        self.storage.store(stored_value)
    }
    
    /// Get a value locally
    pub fn get(&self, key: &[u8]) -> Option<&StoredValue> {
        self.storage.get(key)
    }
    
    /// Find closest nodes to a target
    pub fn find_closest_nodes(&self, target: &[u8; 32], k: usize) -> Vec<NodeInfo> {
        self.routing_table.find_closest(target, k)
    }
    
    /// Add a node to routing table
    pub fn add_node(&mut self, node_id: [u8; 32], endpoint: Endpoint) -> Result<()> {
        self.routing_table.add_node(node_id, endpoint)
    }
    
    /// Remove a node from routing table
    pub fn remove_node(&mut self, node_id: &[u8; 32]) {
        self.routing_table.remove_node(node_id);
    }
    
    /// Start a query (track as pending)
    pub fn start_query(&mut self, key: Vec<u8>) {
        self.pending_queries.insert(key, Instant::now());
    }
    
    /// Complete a query
    pub fn complete_query(&mut self, key: &[u8]) {
        self.pending_queries.remove(key);
    }
    
    /// Cleanup timed out queries
    pub fn cleanup_queries(&mut self) -> Vec<Vec<u8>> {
        let now = Instant::now();
        let timed_out: Vec<_> = self.pending_queries.iter()
            .filter(|(_, started)| now.duration_since(**started) > self.query_timeout)
            .map(|(k, _)| k.clone())
            .collect();
        
        for key in &timed_out {
            self.pending_queries.remove(key);
        }
        
        timed_out
    }
    
    /// Check if we are responsible for a key (closest to key among known nodes)
    pub fn is_responsible_for(&self, key: &[u8]) -> bool {
        let closest = self.routing_table.find_closest(key, 1);
        
        if closest.is_empty() {
            return true; // We're the only node
        }
        
        // Compare our distance to the closest known node's distance
        let our_distance = NodeIdentity::distance(self.routing_table.node_id(), &{
            let mut arr = [0u8; 32];
            arr.copy_from_slice(&key[..32.min(key.len())]);
            arr
        });
        
        let their_distance = {
            let mut arr = [0u8; 32];
            arr.copy_from_slice(&closest[0].node_id);
            NodeIdentity::distance(&arr, &{
                let mut arr2 = [0u8; 32];
                arr2.copy_from_slice(&key[..32.min(key.len())]);
                arr2
            })
        };
        
        NodeIdentity::compare_distance(&our_distance, &their_distance) == std::cmp::Ordering::Less
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_routing_table_add_nodes() {
        let node_id = [1u8; 32];
        let endpoint = Endpoint::default();
        let mut table = RoutingTable::new(node_id, endpoint);
        
        // Add some nodes
        for i in 0..50 {
            let mut new_id = [0u8; 32];
            new_id[0] = i as u8;
            let ep = Endpoint::default();
            let _ = table.add_node(new_id, ep);
        }
        
        assert!(table.node_count() > 0);
        assert!(table.node_count() <= K_BUCKET_SIZE * ID_BITS);
    }
    
    #[test]
    fn test_routing_table_find_closest() {
        let node_id = [1u8; 32];
        let endpoint = Endpoint::default();
        let mut table = RoutingTable::new(node_id, endpoint);
        
        // Add nodes
        for i in 0..100 {
            let mut new_id = [0u8; 32];
            new_id[0] = i as u8;
            let ep = Endpoint::default();
            let _ = table.add_node(new_id, ep);
        }
        
        // Find closest to a target
        let target = [50u8; 32];
        let closest = table.find_closest(&target, 20);
        
        assert_eq!(closest.len(), 20.min(table.node_count()));
    }
    
    #[test]
    fn test_dht_store_get() {
        let node_id = [1u8; 32];
        let endpoint = Endpoint::default();
        let mut dht = Dht::new(node_id, endpoint, 1000);
        
        let key = b"test_key".to_vec();
        let value = b"test_value".to_vec();
        let publisher = [2u8; 32];
        let signature = vec![0u8; 64];
        
        dht.store(key.clone(), value.clone(), publisher, signature).unwrap();
        
        let stored = dht.get(&key);
        assert!(stored.is_some());
        assert_eq!(stored.unwrap().value, value);
    }
}
