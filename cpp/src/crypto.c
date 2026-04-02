/**
 * Cryptographic Primitives Implementation
 */

#include "crypto.h"
#include "../lib/ed25519/ed25519.h"
#include "../lib/sha256/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int initialized = 0;

int crypto_init(void) {
    if (!initialized) {
        srand((unsigned int)time(NULL));
        initialized = 1;
    }
    return 0;
}

int crypto_keypair_generate(crypto_keypair_t* kp) {
    unsigned char seed[ED25519_SEED_SIZE];
    ed25519_random_seed(seed);
    return crypto_keypair_from_seed(kp, seed);
}

int crypto_keypair_from_seed(crypto_keypair_t* kp, const uint8_t* seed) {
    ed25519_create_keypair(kp->public_key, kp->secret_key, seed);
    return 0;
}

int crypto_sign(uint8_t* signature, const uint8_t* message, size_t msg_len,
                const uint8_t* secret_key) {
    /* Need public key from secret key for full Ed25519 */
    uint8_t public_key[CRYPTO_PUBLIC_KEY_SIZE];
    /* Derive public key from secret key (first 32 bytes are seed) */
    ed25519_create_keypair(public_key, (unsigned char*)secret_key, secret_key);
    ed25519_sign(signature, message, msg_len, public_key, secret_key);
    return 0;
}

int crypto_verify(const uint8_t* signature, const uint8_t* message, size_t msg_len,
                  const uint8_t* public_key) {
    return ed25519_verify(signature, message, msg_len, public_key);
}

int crypto_sha256(uint8_t* hash, const uint8_t* data, size_t len) {
    sha256(hash, data, len);
    return 0;
}

int crypto_node_id(uint8_t* node_id, const uint8_t* public_key) {
    return crypto_sha256(node_id, public_key, CRYPTO_PUBLIC_KEY_SIZE);
}

void crypto_distance(uint8_t* out, const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < CRYPTO_HASH_SIZE; i++) {
        out[i] = a[i] ^ b[i];
    }
}

int crypto_compare_distance(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < CRYPTO_HASH_SIZE; i++) {
        if (a[i] != b[i]) {
            return (a[i] < b[i]) ? -1 : 1;
        }
    }
    return 0;
}

int crypto_random(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(rand() & 255);
    }
    return 0;
}

int crypto_keypair_load(crypto_keypair_t* kp, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    
    size_t read = fread(kp->secret_key, 1, CRYPTO_SECRET_KEY_SIZE, f);
    if (read != CRYPTO_SECRET_KEY_SIZE) {
        fclose(f);
        return -1;
    }
    
    /* Derive public key from secret key */
    ed25519_create_keypair(kp->public_key, kp->secret_key, kp->secret_key);
    
    fclose(f);
    return 0;
}

int crypto_keypair_save(const crypto_keypair_t* kp, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    
    if (fwrite(kp->secret_key, 1, CRYPTO_SECRET_KEY_SIZE, f) != CRYPTO_SECRET_KEY_SIZE) {
        fclose(f);
        return -1;
    }
    
    fclose(f);
    return 0;
}
