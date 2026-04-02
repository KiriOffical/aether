/**
 * Peer Management Implementation - C++
 */

#include "peer.hpp"
#include "crypto.hpp"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

namespace aether {

// Helper to convert NodeId to hex string
static std::string node_id_to_hex(const NodeId& id) {
    static const char hex_chars[] = "0123456789abcdef";
    std::ostringstream oss;
    for (uint8_t byte : id) {
        oss << hex_chars[(byte >> 4) & 0xF] << hex_chars[byte & 0xF];
    }
    return oss.str();
}

// Peer implementation

Peer::Peer(const NodeId& node_id, const std::string& addr, uint16_t port)
    : node_id(node_id)
    , remote_addr(addr)
    , remote_port(port)
    , last_activity(std::chrono::steady_clock::now())
{}

bool Peer::is_stale() const {
    return std::chrono::steady_clock::now() - last_activity > DEFAULT_TTL;
}

void Peer::mark_active() {
    last_activity = std::chrono::steady_clock::now();
}

void Peer::set_connected(const std::vector<std::string>& protocols,
                         const std::optional<std::string>& listen_addr,
                         const std::optional<uint16_t>& listen_port) {
    state = PeerState::Connected;
    connected_at = std::chrono::steady_clock::now();
    this->protocols = protocols;
    this->listen_addr = listen_addr;
    this->listen_port = listen_port;
}

std::optional<Endpoint> Peer::to_endpoint() const {
    if (listen_addr && listen_port) {
        return Endpoint::from_ipv4(*listen_addr, *listen_port);
    }
    if (!remote_addr.empty() && remote_port > 0) {
        return Endpoint::from_ipv4(remote_addr, remote_port);
    }
    return std::nullopt;
}

// PeerManager implementation

PeerManager::PeerManager(size_t max_connections)
    : max_connections_(max_connections)
{}

void PeerManager::add_peer(const Peer& peer) {
    std::string id_hex = node_id_to_hex(peer.node_id);

    // Remove old address mapping if exists
    auto it = peers_.find(id_hex);
    if (it != peers_.end() && it->second.remote_addr != peer.remote_addr) {
        by_address_.erase(it->second.remote_addr + ":" + std::to_string(it->second.remote_port));
    }

    // Add/update peer
    peers_[id_hex] = peer;

    // Update address mapping
    std::string addr_key = peer.remote_addr + ":" + std::to_string(peer.remote_port);
    by_address_[addr_key] = id_hex;
}

const Peer* PeerManager::get_peer(const NodeId& node_id) const {
    std::string id_hex = node_id_to_hex(node_id);
    auto it = peers_.find(id_hex);
    return (it != peers_.end()) ? &it->second : nullptr;
}

Peer* PeerManager::get_peer(const NodeId& node_id) {
    std::string id_hex = node_id_to_hex(node_id);
    auto it = peers_.find(id_hex);
    return (it != peers_.end()) ? &it->second : nullptr;
}

const Peer* PeerManager::get_peer_by_addr(const std::string& addr, uint16_t port) const {
    std::string addr_key = addr + ":" + std::to_string(port);
    auto it = by_address_.find(addr_key);
    if (it != by_address_.end()) {
        NodeId node_id;
        std::istringstream iss(it->second);
        std::string hex;
        iss >> hex;
        if (hex.length() == NODE_ID_SIZE * 2) {
            for (size_t i = 0; i < NODE_ID_SIZE; i++) {
                node_id[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
            }
            return get_peer(node_id);
        }
    }
    return nullptr;
}

Peer* PeerManager::get_peer_by_addr(const std::string& addr, uint16_t port) {
    std::string addr_key = addr + ":" + std::to_string(port);
    auto it = by_address_.find(addr_key);
    if (it != by_address_.end()) {
        NodeId node_id;
        std::istringstream iss(it->second);
        std::string hex;
        iss >> hex;
        if (hex.length() == NODE_ID_SIZE * 2) {
            for (size_t i = 0; i < NODE_ID_SIZE; i++) {
                node_id[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
            }
            return get_peer(node_id);
        }
    }
    return nullptr;
}

std::optional<Peer> PeerManager::remove_peer(const NodeId& node_id) {
    std::string id_hex = node_id_to_hex(node_id);
    auto it = peers_.find(id_hex);

    if (it != peers_.end()) {
        Peer peer = it->second;
        by_address_.erase(peer.remote_addr + ":" + std::to_string(peer.remote_port));
        peers_.erase(it);
        return peer;
    }

    return std::nullopt;
}

void PeerManager::disconnect_peer(const NodeId& node_id) {
    Peer* peer = get_peer(node_id);
    if (peer) {
        peer->state = PeerState::Disconnected;
        peer->connected_at = std::nullopt;
    }
}

void PeerManager::blacklist(const NodeId& node_id) {
    blacklist_.insert(node_id_to_hex(node_id));
}

void PeerManager::blacklist_address(const std::string& addr) {
    blacklist_addr_.insert(addr);
}

bool PeerManager::is_blacklisted(const NodeId& node_id) const {
    return blacklist_.count(node_id_to_hex(node_id)) > 0;
}

bool PeerManager::is_address_blacklisted(const std::string& addr) const {
    return blacklist_addr_.count(addr) > 0;
}

size_t PeerManager::active_count() const {
    size_t count = 0;
    for (const auto& [id, peer] : peers_) {
        if (peer.is_active()) {
            count++;
        }
    }
    return count;
}

std::vector<Endpoint> PeerManager::get_random_peers(size_t limit) const {
    std::vector<const Peer*> active;
    active.reserve(peers_.size());

    for (const auto& [id, peer] : peers_) {
        if (peer.is_active()) {
            active.push_back(&peer);
        }
    }

    if (active.empty()) {
        return {};
    }

    // Shuffle and take limit
    static std::random_device rd;
    static std::mt19937 gen(rd());

    std::shuffle(active.begin(), active.end(), gen);

    std::vector<Endpoint> result;
    size_t count = std::min(limit, active.size());
    result.reserve(count);

    for (size_t i = 0; i < count; i++) {
        if (auto ep = active[i]->to_endpoint()) {
            result.push_back(*ep);
        }
    }

    return result;
}

std::vector<Peer> PeerManager::get_closest_peers(const NodeId& target, size_t k) const {
    std::vector<std::pair<const Peer*, std::array<uint8_t, NODE_ID_SIZE>>> peers_with_dist;

    for (const auto& [id, peer] : peers_) {
        if (peer.is_active()) {
            auto dist = Crypto::distance(peer.node_id, target);
            peers_with_dist.emplace_back(&peer, dist);
        }
    }

    // Sort by distance
    std::sort(peers_with_dist.begin(), peers_with_dist.end(),
        [](const auto& a, const auto& b) {
            return Crypto::compare_distance(a.second, b.second) < 0;
        });

    // Return top k
    std::vector<Peer> result;
    result.reserve(std::min(k, peers_with_dist.size()));

    for (size_t i = 0; i < k && i < peers_with_dist.size(); i++) {
        result.push_back(*peers_with_dist[i].first);
    }

    return result;
}

std::vector<const Peer*> PeerManager::active_peers() const {
    std::vector<const Peer*> result;
    result.reserve(active_count());

    for (const auto& [id, peer] : peers_) {
        if (peer.is_active()) {
            result.push_back(&peer);
        }
    }

    return result;
}

std::vector<NodeId> PeerManager::evict_stale() {
    std::vector<NodeId> evicted;

    for (auto it = peers_.begin(); it != peers_.end(); ) {
        if (it->second.is_stale() && !it->second.is_active()) {
            evicted.push_back(it->second.node_id);
            by_address_.erase(it->second.remote_addr + ":" + std::to_string(it->second.remote_port));
            it = peers_.erase(it);
        } else {
            ++it;
        }
    }

    return evicted;
}

void PeerManager::update_latency(const NodeId& node_id, Duration latency) {
    Peer* peer = get_peer(node_id);
    if (peer) {
        peer->latency = latency;
    }
}

void PeerManager::adjust_trust(const NodeId& node_id, int delta) {
    Peer* peer = get_peer(node_id);
    if (peer) {
        int new_score = static_cast<int>(peer->trust_score) + delta;
        peer->trust_score = static_cast<uint8_t>(std::clamp(new_score, 0, 100));
    }
}

}  // namespace aether
