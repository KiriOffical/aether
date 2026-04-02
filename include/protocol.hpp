/**
 * Core Protocol - C++ API
 */

#ifndef AETHER_PROTOCOL_HPP
#define AETHER_PROTOCOL_HPP

#include "aether.hpp"
#include "crypto.hpp"
#include "dht.hpp"
#include "peer.hpp"
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <poll.h>
#endif

namespace aether {

/**
 * Message types for the P2P protocol
 */
enum class MessageType : uint8_t {
    Hello = 1,
    HelloAck = 2,
    FindNode = 3,
    FindNodeResponse = 4,
    StoreValue = 5,
    GetValue = 6,
    GetValueResponse = 7,
    Ping = 8,
    Pong = 9,
    Disconnect = 10,
    PeerExchange = 11,
    Error = 12
};

/**
 * Protocol message
 */
struct Message {
    MessageType type;
    std::vector<uint8_t> payload;

    Message() = default;
    Message(MessageType type, std::vector<uint8_t> payload)
        : type(type), payload(std::move(payload)) {}

    /**
     * Encode message to bytes
     */
    std::vector<uint8_t> encode() const;

    /**
     * Decode message from bytes
     * @return nullopt if invalid
     */
    static std::optional<Message> decode(const uint8_t* data, size_t len);
    static std::optional<Message> decode(const std::vector<uint8_t>& data);
};

/**
 * Internal protocol state
 */
struct ProtocolState {
    KeyPair identity;
    Config config;
    NodeId node_id;
    std::unique_ptr<Dht> dht;
    std::unique_ptr<PeerManager> peer_manager;

    ProtocolState();
    explicit ProtocolState(const Config& config);
};

/**
 * Main A.E.T.H.E.R. Node class
 */
class Node {
public:
    explicit Node(const Config& config = Config());
    ~Node();

    // Non-copyable
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    /**
     * Start the node (binds to port)
     */
    Error start();

    /**
     * Stop the node
     */
    Error stop();

    /**
     * Run main loop (blocking)
     */
    Error run();

    /**
     * Get node ID
     */
    const NodeId& node_id() const { return state_->node_id; }

    /**
     * Get listening port
     */
    uint16_t port() const { return state_->config.listen_port; }

    /**
     * Get peer count
     */
    size_t peer_count() const;

    /**
     * Get node statistics
     */
    NodeStats stats() const;

    /**
     * Send message to specific peer
     */
    Error send(const NodeId& target_id, const std::vector<uint8_t>& data);

    /**
     * Broadcast to all peers
     */
    Error broadcast(const std::vector<uint8_t>& data);

    /**
     * DHT operations
     */
    Error dht_store(const std::vector<uint8_t>& key,
                    const std::vector<uint8_t>& value);
    std::optional<std::vector<uint8_t>> dht_get(const std::vector<uint8_t>& key) const;
    std::vector<BucketEntry> dht_find_node(const NodeId& target, size_t k = 20) const;

    /**
     * Set callbacks
     */
    void set_message_callback(MessageCallback cb);
    void set_peer_callback(PeerCallback cb);
    void set_log_callback(LogCallback cb);

    /**
     * Check if running
     */
    bool is_running() const { return running_.load(); }

    /**
     * Get DHT reference
     */
    Dht* dht() { return state_->dht.get(); }
    const Dht* dht() const { return state_->dht.get(); }

    /**
     * Get peer manager reference
     */
    PeerManager* peer_manager() { return state_->peer_manager.get(); }
    const PeerManager* peer_manager() const { return state_->peer_manager.get(); }

private:
    void accept_connections();
    void handle_client(int client_sock);

    std::shared_ptr<ProtocolState> state_;
    std::atomic<bool> running_{false};
    int listen_socket_ = -1;
    std::thread accept_thread_;

    MessageCallback message_cb_;
    PeerCallback peer_cb_;
    LogCallback log_cb_;
    mutable std::mutex callback_mutex_;

    void log(LogLevel level, const std::string& msg) const;
};

/**
 * Client for connecting to a node
 */
class Client {
public:
    Client(const std::string& host = "localhost", uint16_t port = DEFAULT_PORT);
    ~Client();

    // Non-copyable
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    /**
     * Connect to node and complete handshake
     * @param timeout connection timeout in seconds
     */
    Error connect(int timeout = 5);

    /**
     * Disconnect from node
     */
    void disconnect();

    /**
     * Check if connected
     */
    bool is_connected() const { return connected_; }

    /**
     * Send PING and wait for PONG
     * @return latency in milliseconds, or -1 on failure
     */
    double ping();

    /**
     * Store key-value in DHT
     */
    Error store(const std::vector<uint8_t>& key,
                const std::vector<uint8_t>& value);
    Error store(const std::string& key, const std::string& value);

    /**
     * Get value from DHT
     * @return nullopt if not found
     */
    std::optional<std::vector<uint8_t>> get(const std::vector<uint8_t>& key);
    std::optional<std::string> get(const std::string& key);

    /**
     * Find nodes closest to target
     */
    std::vector<BucketEntry> find_node(const NodeId& target);

    /**
     * Request peer list (PEX)
     */
    std::vector<Endpoint> peer_exchange();

private:
    std::string host_;
    uint16_t port_;
    int socket_ = -1;
    bool connected_ = false;
    NodeId node_id_;

    bool send_message(const Message& msg);
    std::optional<Message> recv_message(int timeout = 5);
    bool do_handshake();
    std::optional<Message> try_recv_hello();
};

}  // namespace aether

#endif /* AETHER_PROTOCOL_HPP */
