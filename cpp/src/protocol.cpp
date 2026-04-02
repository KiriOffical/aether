/**
 * Core Protocol Implementation - C++
 */

#include "protocol.hpp"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <netdb.h>
    #include <sys/poll.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

namespace aether {

// Helper function to convert NodeId to hex string
static std::string node_id_to_hex(const NodeId& id) {
    static const char hex_chars[] = "0123456789abcdef";
    std::ostringstream oss;
    for (uint8_t byte : id) {
        oss << hex_chars[(byte >> 4) & 0xF] << hex_chars[byte & 0xF];
    }
    return oss.str();
}

// Overload for std::vector<uint8_t>
static std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    static const char hex_chars[] = "0123456789abcdef";
    std::ostringstream oss;
    for (uint8_t byte : bytes) {
        oss << hex_chars[(byte >> 4) & 0xF] << hex_chars[byte & 0xF];
    }
    return oss.str();
}

// Message encoding/decoding

std::vector<uint8_t> Message::encode() const {
    // Header: 1 byte type + 4 bytes length (big-endian)
    std::vector<uint8_t> result(5 + payload.size());

    result[0] = static_cast<uint8_t>(type);
    result[1] = (payload.size() >> 24) & 0xFF;
    result[2] = (payload.size() >> 16) & 0xFF;
    result[3] = (payload.size() >> 8) & 0xFF;
    result[4] = payload.size() & 0xFF;

    if (!payload.empty()) {
        std::copy(payload.begin(), payload.end(), result.begin() + 5);
    }

    return result;
}

std::optional<Message> Message::decode(const uint8_t* data, size_t len) {
    if (len < 5) {
        return std::nullopt;
    }

    MessageType type = static_cast<MessageType>(data[0]);
    uint32_t payload_len = (static_cast<uint32_t>(data[1]) << 24) |
                           (static_cast<uint32_t>(data[2]) << 16) |
                           (static_cast<uint32_t>(data[3]) << 8) |
                           static_cast<uint32_t>(data[4]);

    if (len < 5 + payload_len) {
        return std::nullopt;
    }

    Message msg;
    msg.type = type;
    msg.payload.assign(data + 5, data + 5 + payload_len);

    return msg;
}

std::optional<Message> Message::decode(const std::vector<uint8_t>& data) {
    return decode(data.data(), data.size());
}

// ProtocolState implementation

ProtocolState::ProtocolState()
    : node_id{}
{}

ProtocolState::ProtocolState(const Config& config)
    : config(config)
    , node_id{}
{
    // Load or generate identity
    if (!config.identity_path.empty()) {
        identity = KeyPair::load_or_generate(config.identity_path);
    } else {
        identity = KeyPair::generate();
    }

    // Compute node ID from public key
    node_id = Crypto::node_id(identity.public_key());

    // Initialize DHT
    Endpoint endpoint;
    endpoint.port = config.listen_port;
    dht = std::make_unique<Dht>(node_id, endpoint, 100000);

    // Initialize peer manager
    peer_manager = std::make_unique<PeerManager>(config.max_connections);
}

// Node implementation

Node::Node(const Config& config)
    : state_(std::make_shared<ProtocolState>(config))
{}

Node::~Node() {
    stop();
}

Error Node::start() {
    if (running_.load()) {
        return Error::Ok;  // Already running
    }

    // Create socket
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return Error::Io;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return Error::Io;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(state_->config.listen_port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return Error::Io;
    }

    if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return Error::Io;
    }

    listen_socket_ = static_cast<int>(sock);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return Error::Io;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(state_->config.listen_port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return Error::Io;
    }

    if (listen(sock, SOMAXCONN) < 0) {
        close(sock);
        return Error::Io;
    }

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    listen_socket_ = sock;
#endif

    running_ = true;

    log(LogLevel::Info, "Node ID: " + node_id_to_hex(state_->node_id).substr(0, 16) + "...");
    log(LogLevel::Info, "Listening on port " + std::to_string(state_->config.listen_port));

    // Start accept thread
    accept_thread_ = std::thread(&Node::accept_connections, this);

    return Error::Ok;
}

Error Node::stop() {
    if (!running_.load()) {
        return Error::Ok;
    }

    running_ = false;

    // Close listen socket
#ifdef _WIN32
    if (listen_socket_ >= 0) {
        closesocket(static_cast<SOCKET>(listen_socket_));
        WSACleanup();
    }
#else
    if (listen_socket_ >= 0) {
        close(listen_socket_);
    }
#endif
    listen_socket_ = -1;

    // Wait for accept thread
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    log(LogLevel::Info, "Node stopped");
    return Error::Ok;
}

Error Node::run() {
    auto last_ping = std::chrono::steady_clock::now();
    auto last_cleanup = std::chrono::steady_clock::now();

    constexpr auto PING_INTERVAL = std::chrono::seconds(30);
    constexpr auto CLEANUP_INTERVAL = std::chrono::minutes(5);

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();

        // Periodic ping
        if (now - last_ping >= PING_INTERVAL) {
            // Would send pings to all active peers
            last_ping = now;
        }

        // Periodic cleanup
        if (now - last_cleanup >= CLEANUP_INTERVAL) {
            state_->peer_manager->evict_stale();
            state_->dht->cleanup();
            last_cleanup = now;
        }
    }

    return Error::Ok;
}

size_t Node::peer_count() const {
    return state_->peer_manager->active_count();
}

NodeStats Node::stats() const {
    NodeStats stats;
    stats.node_id = state_->node_id;
    stats.port = state_->config.listen_port;
    stats.peer_count = peer_count();
    stats.dht_node_count = state_->dht->node_count();
    stats.dht_value_count = state_->dht->value_count();
    stats.version = VERSION_STRING;
    return stats;
}

Error Node::send(const NodeId& target_id, const std::vector<uint8_t>& data) {
    // Find peer and send (simplified - would need connection tracking)
    (void)target_id;
    (void)data;
    return Error::Ok;
}

Error Node::broadcast(const std::vector<uint8_t>& data) {
    (void)data;
    return Error::Ok;
}

Error Node::dht_store(const std::vector<uint8_t>& key,
                      const std::vector<uint8_t>& value) {
    Signature sig = state_->identity.sign(key);
    return state_->dht->store(key, value, state_->node_id, sig);
}

std::optional<std::vector<uint8_t>> Node::dht_get(const std::vector<uint8_t>& key) const {
    return state_->dht->get(key);
}

std::vector<BucketEntry> Node::dht_find_node(const NodeId& target, size_t k) const {
    return state_->dht->find_closest_nodes(target, k);
}

void Node::set_message_callback(MessageCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_cb_ = std::move(cb);
}

void Node::set_peer_callback(PeerCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    peer_cb_ = std::move(cb);
}

void Node::set_log_callback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    log_cb_ = std::move(cb);
}

void Node::log(LogLevel level, const std::string& msg) const {
    if (level > state_->config.log_level) {
        return;
    }

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (log_cb_) {
        log_cb_(level, msg);
    } else {
        const char* levels[] = {"ERROR", "WARN", "INFO", "DEBUG"};
        std::cout << "[" << levels[static_cast<int>(level)] << "] " << msg << std::endl;
    }
}

void Node::accept_connections() {
    while (running_.load()) {
#ifdef _WIN32
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(static_cast<SOCKET>(listen_socket_), &fds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(0, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(static_cast<SOCKET>(listen_socket_), &fds)) {
            sockaddr_in client_addr{};
            int client_len = sizeof(client_addr);
            SOCKET client_sock = accept(static_cast<SOCKET>(listen_socket_),
                                        reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_sock != INVALID_SOCKET) {
                log(LogLevel::Debug, "New connection from " +
                    std::string(inet_ntoa(client_addr.sin_addr)) + ":" +
                    std::to_string(ntohs(client_addr.sin_port)));

                // Handle in separate thread
                std::thread(&Node::handle_client, this, static_cast<int>(client_sock)).detach();
            }
        }
#else
        struct pollfd pfd;
        pfd.fd = listen_socket_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 1000);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_sock >= 0) {
                char addr_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));

                log(LogLevel::Debug, "New connection from " +
                    std::string(addr_str) + ":" + std::to_string(ntohs(client_addr.sin_port)));

                // Handle in separate thread
                std::thread(&Node::handle_client, this, client_sock).detach();
            }
        }
#endif
    }
}

void Node::handle_client(int client_sock) {
    // Simplified client handler
    // In a full implementation, this would:
    // 1. Perform handshake
    // 2. Read messages
    // 3. Route to appropriate handlers
    // 4. Handle disconnection

#ifdef _WIN32
    closesocket(static_cast<SOCKET>(client_sock));
#else
    close(client_sock);
#endif
}

// Client implementation

Client::Client(const std::string& host, uint16_t port)
    : host_(host)
    , port_(port)
    , socket_(-1)
    , connected_(false)
{
    Crypto::random_bytes(node_id_.data(), node_id_.size());
}

Client::~Client() {
    disconnect();
}

Error Client::connect(int timeout) {
    if (connected_) {
        return Error::Ok;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return Error::Io;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return Error::Io;
    }

    // Set timeout
    struct timeval tv;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));

    // Resolve hostname
    struct hostent* server = gethostbyname(host_.c_str());
    if (!server) {
        closesocket(sock);
        WSACleanup();
        return Error::Io;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(port_);

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closesocket(sock);
        WSACleanup();
        return Error::Io;
    }

    socket_ = static_cast<int>(sock);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return Error::Io;
    }

    // Set timeout
    struct timeval tv;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Resolve hostname
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (::getaddrinfo(host_.c_str(), nullptr, &hints, &res) != 0) {
        close(sock);
        return Error::Io;
    }

    auto* addr_in = reinterpret_cast<sockaddr_in*>(res->ai_addr);
    addr_in->sin_port = htons(port_);

    if (::connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        ::freeaddrinfo(res);
        close(sock);
        return Error::Io;
    }

    ::freeaddrinfo(res);
    socket_ = sock;
#endif

    connected_ = true;

    // Perform handshake
    if (!do_handshake()) {
        disconnect();
        return Error::HandshakeFailed;
    }

    return Error::Ok;
}

void Client::disconnect() {
    if (socket_ >= 0) {
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(socket_));
        WSACleanup();
#else
        close(socket_);
#endif
    }
    socket_ = -1;
    connected_ = false;
}

double Client::ping() {
    if (!connected_) {
        return -1.0;
    }

    // Try to receive HELLO first
    if (auto hello = try_recv_hello()) {
        if (hello->type == MessageType::Hello) {
            // Send HELLO_ACK
            std::vector<uint8_t> payload;
            std::string json = "{\"version\":\"0.2.0\",\"node_id\":\"" +
                              node_id_to_hex(node_id_) + "\",\"timestamp\":0}";
            payload.assign(json.begin(), json.end());

            Message ack(MessageType::HelloAck, std::move(payload));
            send_message(ack);
        }
    }

    // Send PING
    auto start = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload(4);
    payload[0] = 0; payload[1] = 0; payload[2] = 0; payload[3] = 1;
    Message ping_msg(MessageType::Ping, std::move(payload));

    if (!send_message(ping_msg)) {
        return -1.0;
    }

    auto response = recv_message(5);
    auto end = std::chrono::steady_clock::now();

    if (response && response->type == MessageType::Pong) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    return -1.0;
}

Error Client::store(const std::vector<uint8_t>& key,
                    const std::vector<uint8_t>& value) {
    if (!connected_) {
        return Error::ConnectionClosed;
    }

    // Convert to hex strings for JSON payload
    std::ostringstream json;
    json << "{\"key\":\"" << bytes_to_hex(key) << "\",\"value\":\"" << bytes_to_hex(value) << "\"}";

    std::vector<uint8_t> payload(json.str().begin(), json.str().end());
    Message msg(MessageType::StoreValue, std::move(payload));

    return send_message(msg) ? Error::Ok : Error::Io;
}

Error Client::store(const std::string& key, const std::string& value) {
    std::vector<uint8_t> key_bytes(key.begin(), key.end());
    std::vector<uint8_t> value_bytes(value.begin(), value.end());
    return store(key_bytes, value_bytes);
}

std::optional<std::vector<uint8_t>> Client::get(const std::vector<uint8_t>& key) {
    if (!connected_) {
        return std::nullopt;
    }

    std::ostringstream json;
    json << "{\"key\":\"" << bytes_to_hex(key) << "\"}";

    std::vector<uint8_t> payload(json.str().begin(), json.str().end());
    Message msg(MessageType::GetValue, std::move(payload));

    if (!send_message(msg)) {
        return std::nullopt;
    }

    auto response = recv_message(5);
    if (response && response->type == MessageType::GetValueResponse) {
        // Parse JSON response (simplified)
        std::string payload_str(response->payload.begin(), response->payload.end());

        // Look for "found":true and extract value
        if (payload_str.find("\"found\":true") != std::string::npos ||
            payload_str.find("\"found\": true") != std::string::npos) {
            // Extract value from JSON (simplified parsing)
            size_t pos = payload_str.find("\"value\":\"");
            if (pos != std::string::npos) {
                pos += 9;
                size_t end = payload_str.find('"', pos);
                if (end != std::string::npos) {
                    std::string value_hex = payload_str.substr(pos, end - pos);
                    // Convert hex to bytes
                    std::vector<uint8_t> value;
                    for (size_t i = 0; i < value_hex.length(); i += 2) {
                        std::string byte = value_hex.substr(i, 2);
                        value.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
                    }
                    return value;
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string> Client::get(const std::string& key) {
    std::vector<uint8_t> key_bytes(key.begin(), key.end());
    auto result = get(key_bytes);
    if (result) {
        return std::string(result->begin(), result->end());
    }
    return std::nullopt;
}

std::vector<BucketEntry> Client::find_node(const NodeId& target) {
    std::vector<BucketEntry> result;

    if (!connected_) {
        return result;
    }

    std::ostringstream json;
    json << "{\"target\":\"" << node_id_to_hex(target) << "\"}";

    std::vector<uint8_t> payload(json.str().begin(), json.str().end());
    Message msg(MessageType::FindNode, std::move(payload));

    if (!send_message(msg)) {
        return result;
    }

    auto response = recv_message(5);
    if (response && response->type == MessageType::FindNodeResponse) {
        // Parse nodes from response (simplified)
        // In production, use proper JSON parser
    }

    return result;
}

std::vector<Endpoint> Client::peer_exchange() {
    std::vector<Endpoint> result;

    if (!connected_) {
        return result;
    }

    Message msg(MessageType::PeerExchange, {});

    if (!send_message(msg)) {
        return result;
    }

    auto response = recv_message(5);
    if (response && response->type == MessageType::PeerExchange) {
        // Parse peers from response (simplified)
    }

    return result;
}

bool Client::send_message(const Message& msg) {
    if (socket_ < 0) {
        return false;
    }

    auto data = msg.encode();

#ifdef _WIN32
    int sent = send(static_cast<SOCKET>(socket_),
                    reinterpret_cast<const char*>(data.data()),
                    static_cast<int>(data.size()), 0);
    return sent == static_cast<int>(data.size());
#else
    ssize_t sent = send(socket_, data.data(), data.size(), 0);
    return sent == static_cast<ssize_t>(data.size());
#endif
}

std::optional<Message> Client::recv_message(int timeout) {
    if (socket_ < 0) {
        return std::nullopt;
    }

    // Set timeout
#ifdef _WIN32
    struct timeval tv;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    setsockopt(static_cast<SOCKET>(socket_), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // Read header (5 bytes)
    std::vector<uint8_t> header(5);
    size_t header_received = 0;

    while (header_received < 5) {
#ifdef _WIN32
        int n = recv(static_cast<SOCKET>(socket_),
                     reinterpret_cast<char*>(header.data() + header_received),
                     static_cast<int>(5 - header_received), 0);
#else
        ssize_t n = recv(socket_, header.data() + header_received, 5 - header_received, 0);
#endif
        if (n <= 0) {
            return std::nullopt;
        }
        header_received += n;
    }

    // Parse header
    uint32_t payload_len = (static_cast<uint32_t>(header[1]) << 24) |
                           (static_cast<uint32_t>(header[2]) << 16) |
                           (static_cast<uint32_t>(header[3]) << 8) |
                           static_cast<uint32_t>(header[4]);

    if (payload_len > MAX_MESSAGE_SIZE) {
        return std::nullopt;
    }

    // Read payload
    std::vector<uint8_t> payload(payload_len);
    size_t payload_received = 0;

    while (payload_received < payload_len) {
#ifdef _WIN32
        int n = recv(static_cast<SOCKET>(socket_),
                     reinterpret_cast<char*>(payload.data() + payload_received),
                     static_cast<int>(payload_len - payload_received), 0);
#else
        ssize_t n = recv(socket_, payload.data() + payload_received, payload_len - payload_received, 0);
#endif
        if (n <= 0) {
            return std::nullopt;
        }
        payload_received += n;
    }

    // Combine and decode
    std::vector<uint8_t> full_msg = header;
    full_msg.insert(full_msg.end(), payload.begin(), payload.end());

    return Message::decode(full_msg);
}

bool Client::do_handshake() {
    // Receive HELLO from server
    auto hello = recv_message(5);
    if (!hello || hello->type != MessageType::Hello) {
        return false;
    }

    // Send HELLO_ACK
    std::ostringstream json;
    json << "{\"version\":\"0.2.0\",\"node_id\":\""
         << node_id_to_hex(node_id_) << "\",\"timestamp\":0}";

    std::vector<uint8_t> payload(json.str().begin(), json.str().end());
    Message ack(MessageType::HelloAck, std::move(payload));

    return send_message(ack);
}

std::optional<Message> Client::try_recv_hello() {
    if (socket_ < 0) {
        return std::nullopt;
    }

    // Set short timeout
#ifdef _WIN32
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;  // 500ms
    setsockopt(static_cast<SOCKET>(socket_), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    return recv_message(0);
}

}  // namespace aether
