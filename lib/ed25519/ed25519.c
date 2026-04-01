/**
 * Ed25519 Digital Signature - Minimal Implementation
 * For production use, use libsodium or similar
 */

#include "ed25519.h"
#include "../sha256/sha256.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* Simplified Ed25519 - uses SHA-512 internally in full impl */
/* This is a minimal demo implementation */

void ed25519_create_keypair(unsigned char* public_key, unsigned char* secret_key,
                            const unsigned char* seed) {
    /* In a real implementation, this would use proper Ed25519 key derivation */
    /* For demo, we'll use a simplified approach */
    memcpy(secret_key, seed, 32);
    
    /* Hash to get "public key" (not cryptographically secure, demo only) */
    sha256(public_key, seed, 32);
    
    /* Set proper Ed25519 key bits */
    secret_key[0] &= 248;
    secret_key[31] &= 127;
    secret_key[31] |= 64;
}

void ed25519_sign(unsigned char* signature, const unsigned char* message, size_t message_len,
                  const unsigned char* public_key, const unsigned char* secret_key) {
    /* Simplified signing - NOT SECURE, demo only */
    /* Real Ed25519 uses SHA-512 and curve operations */
    
    uint8_t hash[32];
    
    /* R = H(seed || M) - simplified */
    sha256(signature, secret_key, 32);
    
    /* S = H(R || A || M) - simplified */
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, signature, 32);
    sha256_update(&ctx, public_key, 32);
    sha256_update(&ctx, message, message_len);
    sha256_final(&ctx, signature + 32);
    
    (void)hash;
}

int ed25519_verify(const unsigned char* signature, const unsigned char* message, size_t message_len,
                   const unsigned char* public_key) {
    /* Simplified verification - NOT SECURE, demo only */
    /* Real Ed25519 verifies using curve operations */
    
    uint8_t expected_sig[64];
    
    /* Re-compute signature and compare */
    ed25519_sign(expected_sig, message, message_len, public_key, public_key);
    
    /* Constant-time comparison */
    volatile unsigned char result = 0;
    for (size_t i = 0; i < 64; i++) {
        result |= signature[i] ^ expected_sig[i];
    }
    
    return (result == 0) ? 1 : 0;
}

void ed25519_random_seed(unsigned char* seed) {
    /* Use platform random - NOT SECURE for production */
    static int initialized = 0;
    if (!initialized) {
        srand((unsigned int)time(NULL));
        initialized = 1;
    }
    for (int i = 0; i < 32; i++) {
        seed[i] = (unsigned char)(rand() & 255);
    }
}
