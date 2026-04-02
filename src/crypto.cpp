/**
 * Cryptographic Primitives Implementation - C++
 */

#include "crypto.hpp"
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <algorithm>

extern "C" {
#include "../lib/ed25519/ed25519.h"
#include "../lib/sha256/sha256.h"
}

namespace aether {

// Static initialization flag
static bool crypto_initialized = false;

void Crypto::init() {
    if (!crypto_initialized) {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        crypto_initialized = true;
    }
}

Hash Crypto::sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

Hash Crypto::sha256(const uint8_t* data, size_t len) {
    Hash hash;
    ::sha256(hash.data(), data, len);
    return hash;
}

NodeId Crypto::node_id(const PublicKey& public_key) {
    Hash hash = sha256(public_key.data(), PUBLIC_KEY_SIZE);
    NodeId node_id;
    std::copy(hash.begin(), hash.end(), node_id.begin());
    return node_id;
}

std::array<uint8_t, NODE_ID_SIZE> Crypto::distance(
    const NodeId& a, const NodeId& b) {
    std::array<uint8_t, NODE_ID_SIZE> dist;
    for (size_t i = 0; i < NODE_ID_SIZE; i++) {
        dist[i] = a[i] ^ b[i];
    }
    return dist;
}

int Crypto::compare_distance(const std::array<uint8_t, NODE_ID_SIZE>& a,
                              const std::array<uint8_t, NODE_ID_SIZE>& b) {
    for (size_t i = 0; i < NODE_ID_SIZE; i++) {
        if (a[i] != b[i]) {
            return (a[i] < b[i]) ? -1 : 1;
        }
    }
    return 0;
}

std::vector<uint8_t> Crypto::random_bytes(size_t len) {
    std::vector<uint8_t> buf(len);
    random_bytes(buf.data(), len);
    return buf;
}

void Crypto::random_bytes(uint8_t* buf, size_t len) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 255);

    for (size_t i = 0; i < len; i++) {
        buf[i] = static_cast<uint8_t>(dis(gen));
    }
}

Hash Crypto::hmac_sha256(const std::vector<uint8_t>& key,
                          const std::vector<uint8_t>& data) {
    // Simplified HMAC-SHA256 implementation
    const size_t block_size = 64;

    std::vector<uint8_t> k(key);

    // If key is longer than block size, hash it
    if (k.size() > block_size) {
        Hash h = sha256(k);
        k.assign(h.begin(), h.end());
    }

    // Pad key to block size
    k.resize(block_size, 0);

    // Create inner and outer padding
    std::vector<uint8_t> o_key_pad(block_size);
    std::vector<uint8_t> i_key_pad(block_size);

    for (size_t i = 0; i < block_size; i++) {
        o_key_pad[i] = k[i] ^ 0x5c;
        i_key_pad[i] = k[i] ^ 0x36;
    }

    // Inner hash: H(i_key_pad || data)
    std::vector<uint8_t> inner_data = i_key_pad;
    inner_data.insert(inner_data.end(), data.begin(), data.end());
    Hash inner_hash = sha256(inner_data);

    // Outer hash: H(o_key_pad || inner_hash)
    std::vector<uint8_t> outer_data = o_key_pad;
    outer_data.insert(outer_data.end(), inner_hash.begin(), inner_hash.end());
    return sha256(outer_data);
}

// KeyPair implementation

KeyPair::KeyPair() {
    std::fill(public_key_.begin(), public_key_.end(), 0);
    std::fill(secret_key_.begin(), secret_key_.end(), 0);
}

KeyPair KeyPair::generate() {
    Crypto::init();

    KeyPair kp;
    uint8_t seed[32];
    ed25519_random_seed(seed);
    ed25519_create_keypair(kp.public_key_.data(), kp.secret_key_.data(), seed);

    return kp;
}

KeyPair KeyPair::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open keypair file: " + path);
    }

    KeyPair kp;
    file.read(reinterpret_cast<char*>(kp.secret_key_.data()), SECRET_KEY_SIZE);

    if (!file) {
        throw std::runtime_error("Invalid keypair file: " + path);
    }

    // Derive public key from secret key
    ed25519_create_keypair(kp.public_key_.data(), kp.secret_key_.data(), kp.secret_key_.data());

    return kp;
}

KeyPair KeyPair::load_or_generate(const std::string& path) {
    std::ifstream test(path);
    if (test.good()) {
        return load(path);
    }

    KeyPair kp = generate();
    kp.save(path);
    return kp;
}

void KeyPair::save(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create keypair file: " + path);
    }

    file.write(reinterpret_cast<const char*>(secret_key_.data()), SECRET_KEY_SIZE);
}

Signature KeyPair::sign(const std::vector<uint8_t>& data) const {
    return sign(data.data(), data.size());
}

Signature KeyPair::sign(const uint8_t* data, size_t len) const {
    Signature sig;
    ed25519_sign(sig.data(), data, len, public_key_.data(), secret_key_.data());
    return sig;
}

bool KeyPair::verify(const PublicKey& public_key,
                     const std::vector<uint8_t>& data,
                     const Signature& signature) {
    return ed25519_verify(signature.data(), data.data(), data.size(), public_key.data()) == 1;
}

}  // namespace aether
