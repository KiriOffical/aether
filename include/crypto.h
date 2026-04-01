/**
 * Cryptographic Primitives
 * Ed25519 signatures and SHA-256 hashing
 */

#ifndef AETHER_CRYPTO_H
#define AETHER_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#define CRYPTO_PUBLIC_KEY_SIZE    32
#define CRYPTO_SECRET_KEY_SIZE    64
#define CRYPTO_SIGNATURE_SIZE     64
#define CRYPTO_HASH_SIZE          32
#define CRYPTO_SEED_SIZE          32

#ifdef __cplusplus
extern "C" {
#endif

/* Key pair structure */
typedef struct {
    uint8_t public_key[CRYPTO_PUBLIC_KEY_SIZE];
    uint8_t secret_key[CRYPTO_SECRET_KEY_SIZE];
} crypto_keypair_t;

/* Initialize crypto library */
int crypto_init(void);

/* Generate a new keypair from random seed */
int crypto_keypair_generate(crypto_keypair_t* kp);

/* Generate keypair from seed (deterministic) */
int crypto_keypair_from_seed(crypto_keypair_t* kp, const uint8_t* seed);

/* Sign a message */
int crypto_sign(uint8_t* signature, const uint8_t* message, size_t msg_len,
                const uint8_t* secret_key);

/* Verify a signature */
int crypto_verify(const uint8_t* signature, const uint8_t* message, size_t msg_len,
                  const uint8_t* public_key);

/* Compute SHA-256 hash */
int crypto_sha256(uint8_t* hash, const uint8_t* data, size_t len);

/* Compute node ID from public key (SHA-256) */
int crypto_node_id(uint8_t* node_id, const uint8_t* public_key);

/* Compute XOR distance between two node IDs */
void crypto_distance(uint8_t* out, const uint8_t* a, const uint8_t* b);

/* Compare two distances (returns -1, 0, or 1) */
int crypto_compare_distance(const uint8_t* a, const uint8_t* b);

/* Random bytes */
int crypto_random(uint8_t* buf, size_t len);

/* Load keypair from file */
int crypto_keypair_load(crypto_keypair_t* kp, const char* path);

/* Save keypair to file */
int crypto_keypair_save(const crypto_keypair_t* kp, const char* path);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_CRYPTO_H */
