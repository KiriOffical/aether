/**
 * Peer Management - C++ API
 */

#ifndef AETHER_PEER_HPP
#define AETHER_PEER_HPP

#include "aether.hpp"
#include "crypto.hpp"
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <optional>
#include <string>

namespace aether {

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::steady_clock::duration;

/**
 * Peer connection state
 */
enum class PeerState {
    Connecting = 0,
    Handshaking = 1,
    Connected = 2,
    Disconnecting = 3,
    Disconnected = 4
};

/**
 * Disconnect reason
 */
enum class DisconnectReason {
    Graceful = 0,
    Timeout = 1,
    ProtocolError = 2,
    Blacklisted = 3,
    MaxConnections = 4,
    Other = 5
};

/**
 * Peer information
 */
struct Peer {
    NodeId node_id;
    std::string remote_addr;
    uint16_t remote_port = 0;
    std::optional<std::string> listen_addr;
    std::optional<uint16_t> listen_port;
    std::vector<std::string> protocols;
    PeerState state = PeerState::Connecting;
    std::optional<TimePoint> connected_at;
    TimePoint last_activity = std::chrono::steady_clock::now();
    std::optional<Duration> latency;
    uint8_t trust_score = 50;  // 0-100

    Peer() = default;
    Peer(const NodeId& node_id, const std::string& addr, uint16_t port);

    /**
     * Check if peer is active (connected)
     */
    bool is_active() const { return state == PeerState::Connected; }

    /**
     * Check if peer info is stale
     */
    bool is_stale() const;

    /**
     * Update last activity timestamp
     */
    void mark_active();

    /**
     * Set connection as established
     */
    void set_connected(const std::vector<std::string>& protocols,
                       const std::optional<std::string>& listen_addr,
                       const std::optional<uint16_t>& listen_port);

    /**
     * Convert to Endpoint for PEX
     */
    std::optional<Endpoint> to_endpoint() const;

    static constexpr Duration DEFAULT_TTL = std::chrono::hours(24);
};

/**
 * Peer manager for tracking all known peers
 */
class PeerManager {
public:
    explicit PeerManager(size_t max_connections = MAX_PEERS);

    /**
     * Add or update a peer
     */
    void add_peer(const Peer& peer);

    /**
     * Get peer by node ID
     */
    const Peer* get_peer(const NodeId& node_id) const;
    Peer* get_peer(const NodeId& node_id);

    /**
     * Get peer by address
     */
    const Peer* get_peer_by_addr(const std::string& addr, uint16_t port) const;
    Peer* get_peer_by_addr(const std::string& addr, uint16_t port);

    /**
     * Remove peer by node ID
     */
    std::optional<Peer> remove_peer(const NodeId& node_id);

    /**
     * Mark peer as disconnected
     */
    void disconnect_peer(const NodeId& node_id);

    /**
     * Add node to blacklist
     */
    void blacklist(const NodeId& node_id);

    /**
     * Add address to blacklist
     */
    void blacklist_address(const std::string& addr);

    /**
     * Check if node is blacklisted
     */
    bool is_blacklisted(const NodeId& node_id) const;

    /**
     * Check if address is blacklisted
     */
    bool is_address_blacklisted(const std::string& addr) const;

    /**
     * Get count of active connections
     */
    size_t active_count() const;

    /**
     * Check if we can accept more connections
     */
    bool can_accept() const { return active_count() < max_connections_; }

    /**
     * Get random active peers for PEX
     */
    std::vector<Endpoint> get_random_peers(size_t limit) const;

    /**
     * Get closest peers to a target node ID
     */
    std::vector<Peer> get_closest_peers(const NodeId& target, size_t k) const;

    /**
     * Get all active peers
     */
    std::vector<const Peer*> active_peers() const;

    /**
     * Evict stale peers
     * @return list of evicted node IDs
     */
    std::vector<NodeId> evict_stale();

    /**
     * Update peer latency measurement
     */
    void update_latency(const NodeId& node_id, Duration latency);

    /**
     * Adjust trust score
     */
    void adjust_trust(const NodeId& node_id, int delta);

    /**
     * Get max connections
     */
    size_t max_connections() const { return max_connections_; }

private:
    std::unordered_map<std::string, Peer> peers_;  // keyed by node_id hex
    std::unordered_map<std::string, std::string> by_address_;  // addr -> node_id
    std::unordered_set<std::string> blacklist_;
    std::unordered_set<std::string> blacklist_addr_;
    size_t max_connections_;
};

/**
 * Connection event types
 */
struct ConnectionEvent {
    enum Type {
        InboundConnected,
        OutboundConnected,
        Disconnected,
        HandshakeComplete,
        ConnectionError
    };

    Type type;
    std::string addr;
    std::optional<NodeId> node_id;
    std::optional<DisconnectReason> reason;
    std::optional<aether::Error> error_code;
    std::vector<std::string> protocols;
};

}  // namespace aether

#endif /* AETHER_PEER_HPP */
