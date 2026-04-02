/**
 * Distributed Hash Table (DHT) Implementation - C++
 */

#include "dht.hpp"
#include "crypto.hpp"
#include <algorithm>
#include <numeric>

namespace aether {

// StoredValue implementation

StoredValue::StoredValue(std::vector<uint8_t> key,
                         std::vector<uint8_t> value,
                         const NodeId& publisher,
                         const Signature& signature,
                         std::chrono::hours ttl)
    : key(std::move(key))
    , value(std::move(value))
    , publisher(publisher)
    , signature(signature)
    , created_at(std::chrono::steady_clock::now())
    , expires_at(created_at + ttl)
{}

bool StoredValue::is_expired() const {
    return std::chrono::steady_clock::now() > expires_at;
}

// BucketEntry implementation

BucketEntry::BucketEntry(const NodeId& node_id, const Endpoint& endpoint)
    : node_id(node_id)
    , endpoint(endpoint)
    , last_seen(std::chrono::steady_clock::now())
    , is_self(false)
{}

void BucketEntry::mark_seen() {
    last_seen = std::chrono::steady_clock::now();
}

// KBucket implementation

KBucket::KBucket(size_t max_size)
    : max_size_(max_size)
{}

std::optional<BucketEntry> KBucket::add(const BucketEntry& entry) {
    // Check if entry already exists
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&entry](const BucketEntry& e) {
            return e.node_id == entry.node_id;
        });

    if (it != entries_.end()) {
        it->mark_seen();
        it->endpoint = entry.endpoint;
        return std::nullopt;
    }

    // If bucket is full, return the oldest entry
    if (entries_.size() >= max_size_) {
        const BucketEntry* oldest = oldest_ptr();
        if (oldest) {
            return *oldest;
        }
    }

    entries_.push_back(entry);
    return std::nullopt;
}

std::optional<BucketEntry> KBucket::remove(const NodeId& node_id) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&node_id](const BucketEntry& e) {
            return e.node_id == node_id;
        });

    if (it != entries_.end()) {
        BucketEntry entry = *it;
        entries_.erase(it);
        return entry;
    }

    return std::nullopt;
}

const BucketEntry* KBucket::oldest_ptr() const {
    if (entries_.empty()) {
        return nullptr;
    }

    auto it = std::min_element(entries_.begin(), entries_.end(),
        [](const BucketEntry& a, const BucketEntry& b) {
            return a.last_seen < b.last_seen;
        });

    return &(*it);
}

// RoutingTable implementation

RoutingTable::RoutingTable(const NodeId& node_id, const Endpoint& endpoint)
    : node_id_(node_id)
    , self_entry_(node_id, endpoint)
{
    self_entry_.is_self = true;
}

size_t RoutingTable::bucket_index(const std::array<uint8_t, NODE_ID_SIZE>& distance) {
    for (size_t i = 0; i < ID_BITS; i++) {
        size_t byte_idx = i / 8;
        size_t bit_idx = 7 - (i % 8);
        if (distance[byte_idx] & (1 << bit_idx)) {
            return i;
        }
    }
    return 0;
}

Error RoutingTable::add_node(const NodeId& node_id, const Endpoint& endpoint) {
    // Don't add self
    if (node_id == node_id_) {
        return Error::Ok;
    }

    auto distance = Crypto::distance(node_id_, node_id);
    size_t index = bucket_index(distance);

    BucketEntry entry(node_id, endpoint);

    if (auto evict = buckets_[index].add(entry)) {
        // Bucket is full
        return Error::NoMemory;
    }

    return Error::Ok;
}

void RoutingTable::remove_node(const NodeId& node_id) {
    if (node_id == node_id_) {
        return;
    }

    auto distance = Crypto::distance(node_id_, node_id);
    size_t index = bucket_index(distance);
    buckets_[index].remove(node_id);
}

std::vector<BucketEntry> RoutingTable::find_closest(const NodeId& target, size_t k) const {
    // Collect all nodes with distances
    std::vector<std::pair<const BucketEntry*, std::array<uint8_t, NODE_ID_SIZE>>> all_nodes;

    for (const auto& bucket : buckets_) {
        for (const auto& entry : bucket.entries()) {
            auto distance = Crypto::distance(entry.node_id, target);
            all_nodes.emplace_back(&entry, distance);
        }
    }

    // Sort by distance
    std::sort(all_nodes.begin(), all_nodes.end(),
        [](const auto& a, const auto& b) {
            return Crypto::compare_distance(a.second, b.second) < 0;
        });

    // Return top k
    std::vector<BucketEntry> result;
    result.reserve(std::min(k, all_nodes.size()));

    for (size_t i = 0; i < k && i < all_nodes.size(); i++) {
        result.push_back(*all_nodes[i].first);
    }

    return result;
}

std::vector<BucketEntry> RoutingTable::all_nodes() const {
    std::vector<BucketEntry> nodes;

    for (const auto& bucket : buckets_) {
        const auto& entries = bucket.entries();
        nodes.insert(nodes.end(), entries.begin(), entries.end());
    }

    return nodes;
}

size_t RoutingTable::node_count() const {
    size_t count = 0;
    for (const auto& bucket : buckets_) {
        count += bucket.size();
    }
    return count;
}

// DhtStorage implementation

DhtStorage::DhtStorage(size_t max_values)
    : max_values_(max_values)
{}

Error DhtStorage::store(StoredValue value) {
    std::string key_str(value.key.begin(), value.key.end());

    // Check if we need to evict
    if (values_.size() >= max_values_ && values_.find(key_str) == values_.end()) {
        // Evict expired values first
        std::vector<std::string> expired_keys;
        for (const auto& [key, val] : values_) {
            if (val.is_expired()) {
                expired_keys.push_back(key);
            }
        }

        for (const auto& key : expired_keys) {
            values_.erase(key);
        }

        // If still full, evict oldest
        if (values_.size() >= max_values_) {
            auto oldest_it = std::min_element(values_.begin(), values_.end(),
                [](const auto& a, const auto& b) {
                    return a.second.created_at < b.second.created_at;
                });

            if (oldest_it != values_.end()) {
                values_.erase(oldest_it);
            }
        }
    }

    values_[key_str] = std::move(value);
    return Error::Ok;
}

std::optional<StoredValue> DhtStorage::get(const std::vector<uint8_t>& key) const {
    std::string key_str(key.begin(), key.end());

    auto it = values_.find(key_str);
    if (it != values_.end() && !it->second.is_expired()) {
        return it->second;
    }

    return std::nullopt;
}

size_t DhtStorage::cleanup() {
    std::vector<std::string> expired_keys;

    for (const auto& [key, val] : values_) {
        if (val.is_expired()) {
            expired_keys.push_back(key);
        }
    }

    for (const auto& key : expired_keys) {
        values_.erase(key);
    }

    return expired_keys.size();
}

// Dht implementation

Dht::Dht(const NodeId& node_id, const Endpoint& endpoint, size_t max_values)
    : routing_table_(node_id, endpoint)
    , storage_(max_values)
{}

Error Dht::store(const std::vector<uint8_t>& key,
                 const std::vector<uint8_t>& value,
                 const NodeId& publisher,
                 const Signature& signature) {
    StoredValue stored(key, value, publisher, signature);
    return storage_.store(std::move(stored));
}

std::optional<std::vector<uint8_t>> Dht::get(const std::vector<uint8_t>& key) const {
    auto stored = storage_.get(key);
    if (stored) {
        return stored->value;
    }
    return std::nullopt;
}

std::vector<BucketEntry> Dht::find_closest_nodes(const NodeId& target, size_t k) const {
    return routing_table_.find_closest(target, k);
}

Error Dht::add_node(const NodeId& node_id, const Endpoint& endpoint) {
    return routing_table_.add_node(node_id, endpoint);
}

void Dht::remove_node(const NodeId& node_id) {
    routing_table_.remove_node(node_id);
}

size_t Dht::cleanup() {
    return storage_.cleanup();
}

}  // namespace aether
