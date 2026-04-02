/**
 * A.E.T.H.E.R. Core - C++ API
 * Asynchronous Edge-Tolerant Holographic Execution Runtime
 */

#ifndef AETHER_HPP
#define AETHER_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>

namespace aether {

/* Version */
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;
constexpr const char* VERSION_STRING = "0.1.0";

/* Constants */
constexpr size_t NODE_ID_SIZE = 32;
constexpr size_t PUBLIC_KEY_SIZE = 32;
constexpr size_t SECRET_KEY_SIZE = 64;
constexpr size_t SIGNATURE_SIZE = 64;
constexpr size_t HASH_SIZE = 32;
constexpr uint16_t DEFAULT_PORT = 7821;
constexpr size_t MAX_MESSAGE_SIZE = 64 * 1024 * 1024;  // 64 MB
constexpr size_t MAX_PEERS = 10000;
constexpr size_t K_BUCKET_SIZE = 20;

/* Node ID type */
using NodeId = std::array<uint8_t, NODE_ID_SIZE>;
using PublicKey = std::array<uint8_t, PUBLIC_KEY_SIZE>;
using SecretKey = std::array<uint8_t, SECRET_KEY_SIZE>;
using Signature = std::array<uint8_t, SIGNATURE_SIZE>;
using Hash = std::array<uint8_t, HASH_SIZE>;

/* Error codes */
enum class Error {
    Ok = 0,
    Io = -1,
    InvalidArg = -2,
    NoMemory = -3,
    ConnectionClosed = -4,
    HandshakeFailed = -5,
    InvalidMessage = -6,
    InvalidSignature = -7,
    MessageTooLarge = -8,
    NotFound = -9,
    RateLimited = -10,
    Blacklisted = -11,
    VersionMismatch = -12,
    Timeout = -13,
    Crypto = -14,
    Protocol = -15
};

/* Convert error to string */
const char* error_string(Error err);

/* Endpoint for network addresses */
struct Endpoint {
    std::array<uint8_t, 16> address{};  // IPv4 or IPv6
    uint16_t port = 0;
    bool is_ipv6 = false;

    static Endpoint from_ipv4(const std::string& ip, uint16_t port);
    static Endpoint from_ipv6(const std::string& ip, uint16_t port);
    std::string to_string() const;
};

/* Log levels */
enum class LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3
};

/* Callback types */
using MessageCallback = std::function<void(const NodeId& from_id, const std::vector<uint8_t>& data)>;
using PeerCallback = std::function<void(const NodeId& peer_id, bool connected)>;
using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

/* Node configuration */
struct Config {
    std::string identity_path;
    std::string data_dir = "aether_data";
    uint16_t listen_port = DEFAULT_PORT;
    std::string public_addr;
    size_t max_connections = MAX_PEERS;
    size_t max_message_size = MAX_MESSAGE_SIZE;
    std::vector<std::string> bootstrap_nodes;
    LogLevel log_level = LogLevel::Info;
    std::string log_file;
    bool enable_tls = false;
    std::string auth_token;

    Config();
};

/* Forward declarations */
class Node;
class Client;
class Dht;
class PeerManager;

/* Node statistics */
struct NodeStats {
    NodeId node_id;
    uint16_t port;
    size_t peer_count;
    size_t dht_node_count;
    size_t dht_value_count;
    std::string version;
};

}  // namespace aether

#endif /* AETHER_HPP */
