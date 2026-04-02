/**
 * Cryptographic Primitives - C++ API
 * Ed25519 signatures and SHA-256 hashing
 */

#ifndef AETHER_CRYPTO_HPP
#define AETHER_CRYPTO_HPP

#include "aether.hpp"
#include <array>
#include <string>
#include <vector>

namespace aether {

/**
 * Key pair for Ed25519 signatures
 */
class KeyPair {
public:
    KeyPair();

    /**
     * Generate a new random keypair
     */
    static KeyPair generate();

    /**
     * Load keypair from file, or generate if not exists
     */
    static KeyPair load_or_generate(const std::string& path);

    /**
     * Load keypair from file
     */
    static KeyPair load(const std::string& path);

    /**
     * Save keypair to file
     */
    void save(const std::string& path) const;

    /**
     * Get the public key
     */
    const PublicKey& public_key() const { return public_key_; }

    /**
     * Get the secret key (keep secure!)
     */
    const SecretKey& secret_key() const { return secret_key_; }

    /**
     * Sign data
     */
    Signature sign(const std::vector<uint8_t>& data) const;
    Signature sign(const uint8_t* data, size_t len) const;

    /**
     * Verify a signature using a public key
     */
    static bool verify(const PublicKey& public_key,
                       const std::vector<uint8_t>& data,
                       const Signature& signature);

private:
    PublicKey public_key_;
    SecretKey secret_key_;
};

/**
 * Cryptographic utilities
 */
class Crypto {
public:
    /**
     * Initialize crypto library
     */
    static void init();

    /**
     * Compute SHA-256 hash
     */
    static Hash sha256(const std::vector<uint8_t>& data);
    static Hash sha256(const uint8_t* data, size_t len);

    /**
     * Compute node ID from public key (SHA-256)
     */
    static NodeId node_id(const PublicKey& public_key);

    /**
     * Compute XOR distance between two node IDs
     */
    static std::array<uint8_t, NODE_ID_SIZE> distance(
        const NodeId& a, const NodeId& b);

    /**
     * Compare two distances
     * @return -1 if a < b, 0 if equal, 1 if a > b
     */
    static int compare_distance(const std::array<uint8_t, NODE_ID_SIZE>& a,
                                const std::array<uint8_t, NODE_ID_SIZE>& b);

    /**
     * Generate random bytes
     */
    static std::vector<uint8_t> random_bytes(size_t len);
    static void random_bytes(uint8_t* buf, size_t len);

    /**
     * HMAC-SHA256
     */
    static Hash hmac_sha256(const std::vector<uint8_t>& key,
                            const std::vector<uint8_t>& data);
};

}  // namespace aether

#endif /* AETHER_CRYPTO_HPP */
