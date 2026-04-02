/**
 * A.E.T.H.E.R. Core - C++ API Implementation
 */

#include "aether.hpp"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace aether {

const char* error_string(Error err) {
    switch (err) {
        case Error::Ok: return "OK";
        case Error::Io: return "IO error";
        case Error::InvalidArg: return "Invalid argument";
        case Error::NoMemory: return "Out of memory";
        case Error::ConnectionClosed: return "Connection closed";
        case Error::HandshakeFailed: return "Handshake failed";
        case Error::InvalidMessage: return "Invalid message";
        case Error::InvalidSignature: return "Invalid signature";
        case Error::MessageTooLarge: return "Message too large";
        case Error::NotFound: return "Not found";
        case Error::RateLimited: return "Rate limited";
        case Error::Blacklisted: return "Blacklisted";
        case Error::VersionMismatch: return "Version mismatch";
        case Error::Timeout: return "Timeout";
        case Error::Crypto: return "Crypto error";
        case Error::Protocol: return "Protocol error";
        default: return "Unknown error";
    }
}

Endpoint Endpoint::from_ipv4(const std::string& ip, uint16_t port) {
    Endpoint ep;
    ep.is_ipv6 = false;
    ep.port = port;

    // Parse IPv4 address
    unsigned int a, b, c, d;
    if (sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        ep.address[0] = static_cast<uint8_t>(a);
        ep.address[1] = static_cast<uint8_t>(b);
        ep.address[2] = static_cast<uint8_t>(c);
        ep.address[3] = static_cast<uint8_t>(d);
    }
    return ep;
}

Endpoint Endpoint::from_ipv6(const std::string& ip, uint16_t port) {
    Endpoint ep;
    ep.is_ipv6 = true;
    ep.port = port;
    // Simplified IPv6 parsing - in production use inet_pton
    std::copy(ip.begin(), ip.end(), ep.address.begin());
    return ep;
}

std::string Endpoint::to_string() const {
    std::ostringstream oss;
    if (is_ipv6) {
        oss << "[";
        for (int i = 0; i < 16; i++) {
            if (i > 0) oss << ":";
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)address[i];
        }
        oss << "]:" << port;
    } else {
        oss << (int)address[0] << "." << (int)address[1] << "."
            << (int)address[2] << "." << (int)address[3] << ":" << port;
    }
    return oss.str();
}

Config::Config() {
    // Default bootstrap nodes
    bootstrap_nodes = {
        "bootstrap.aether.network:7821",
        "archive.org.aether.network:7821",
        "myrient.org.aether.network:7821"
    };
}

}  // namespace aether
