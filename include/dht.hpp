/**
 * Distributed Hash Table (DHT) - C++ API
 * Kademlia-style routing
 */

#ifndef AETHER_DHT_HPP
#define AETHER_DHT_HPP

#include "aether.hpp"
#include <array>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>

namespace aether {

/**
 * Time point for expiration tracking
 */
using TimePoint = std::chrono::steady_clock::time_point;

/**
 * Stored value in the DHT
 */
struct StoredValue {
    std::vector<uint8_t> key;
    std::vector<uint8_t> value;
    NodeId publisher;
    TimePoint created_at;
    TimePoint expires_at;
    Signature signature;

    static constexpr std::chrono::hours DEFAULT_TTL{24};

    StoredValue() = default;

    StoredValue(std::vector<uint8_t> key,
                std::vector<uint8_t> value,
                const NodeId& publisher,
                const Signature& signature,
                std::chrono::hours ttl = DEFAULT_TTL);

    bool is_expired() const;
};

/**
 * Entry in a K-bucket
 */
struct BucketEntry {
    NodeId node_id;
    Endpoint endpoint;
    TimePoint last_seen;
    bool is_self = false;

    BucketEntry() = default;
    BucketEntry(const NodeId& node_id, const Endpoint& endpoint);

    void mark_seen();
};

/**
 * A K-bucket containing up to K node entries
 */
class KBucket {
public:
    explicit KBucket(size_t max_size = K_BUCKET_SIZE);

    /**
     * Add or update an entry
     * @return nullopt if added/updated, or the oldest entry if full
     */
    std::optional<BucketEntry> add(const BucketEntry& entry);

    /**
     * Remove an entry by node ID
     */
    std::optional<BucketEntry> remove(const NodeId& node_id);

    /**
     * Get all entries
     */
    const std::vector<BucketEntry>& entries() const { return entries_; }

    /**
     * Get the oldest entry
     */
    const BucketEntry* oldest_ptr() const;

    /**
     * Check if bucket is full
     */
    bool is_full() const { return entries_.size() >= max_size_; }

    /**
     * Get entry count
     */
    size_t size() const { return entries_.size(); }

private:
    std::vector<BucketEntry> entries_;
    size_t max_size_;
};

/**
 * Kademlia-style DHT routing table
 */
class RoutingTable {
public:
    static constexpr size_t ID_BITS = 256;

    explicit RoutingTable(const NodeId& node_id, const Endpoint& endpoint);

    /**
     * Add a node to the routing table
     * @return Error::Ok on success, Error::NoMemory if bucket full
     */
    Error add_node(const NodeId& node_id, const Endpoint& endpoint);

    /**
     * Remove a node from the routing table
     */
    void remove_node(const NodeId& node_id);

    /**
     * Find the k closest nodes to a target
     */
    std::vector<BucketEntry> find_closest(const NodeId& target, size_t k) const;

    /**
     * Get all nodes in routing table
     */
    std::vector<BucketEntry> all_nodes() const;

    /**
     * Get count of nodes in routing table
     */
    size_t node_count() const;

    /**
     * Get our node ID
     */
    const NodeId& node_id() const { return node_id_; }

private:
    static size_t bucket_index(const std::array<uint8_t, NODE_ID_SIZE>& distance);

    NodeId node_id_;
    std::array<KBucket, ID_BITS> buckets_;
    BucketEntry self_entry_;
};

/**
 * DHT storage for key-value pairs
 */
class DhtStorage {
public:
    explicit DhtStorage(size_t max_values = 100000);

    /**
     * Store a value
     */
    Error store(StoredValue value);

    /**
     * Get a value by key
     */
    std::optional<StoredValue> get(const std::vector<uint8_t>& key) const;

    /**
     * Remove expired values
     * @return count of removed values
     */
    size_t cleanup();

    /**
     * Get count of stored values
     */
    size_t size() const { return values_.size(); }

private:
    std::unordered_map<std::string, StoredValue> values_;
    size_t max_values_;
};

/**
 * Main DHT class combining routing and storage
 */
class Dht {
public:
    Dht(const NodeId& node_id, const Endpoint& endpoint, size_t max_values = 100000);

    /**
     * Store a value locally
     */
    Error store(const std::vector<uint8_t>& key,
                const std::vector<uint8_t>& value,
                const NodeId& publisher,
                const Signature& signature);

    /**
     * Get a value locally
     */
    std::optional<std::vector<uint8_t>> get(const std::vector<uint8_t>& key) const;

    /**
     * Find closest nodes to a target
     */
    std::vector<BucketEntry> find_closest_nodes(const NodeId& target, size_t k) const;

    /**
     * Add a node to routing table
     */
    Error add_node(const NodeId& node_id, const Endpoint& endpoint);

    /**
     * Remove a node from routing table
     */
    void remove_node(const NodeId& node_id);

    /**
     * Cleanup expired values
     */
    size_t cleanup();

    /**
     * Get routing table
     */
    const RoutingTable& routing_table() const { return routing_table_; }
    RoutingTable& routing_table() { return routing_table_; }

    /**
     * Get storage
     */
    const DhtStorage& storage() const { return storage_; }
    DhtStorage& storage() { return storage_; }

    /**
     * Get node count
     */
    size_t node_count() const { return routing_table_.node_count(); }

    /**
     * Get value count
     */
    size_t value_count() const { return storage_.size(); }

private:
    RoutingTable routing_table_;
    DhtStorage storage_;
};

}  // namespace aether

#endif /* AETHER_DHT_HPP */
