/**
 * Ed25519 Digital Signature Implementation
 * Public domain implementation by Orson Peters
 */

#ifndef ED25519_H
#define ED25519_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ED25519_PUBLIC_KEY_SIZE  32
#define ED25519_SECRET_KEY_SIZE  64
#define ED25519_SIGNATURE_SIZE   64
#define ED25519_SEED_SIZE        32

/* Generate a new keypair from a seed */
void ed25519_create_keypair(unsigned char* public_key, unsigned char* secret_key,
                            const unsigned char* seed);

/* Sign a message */
void ed25519_sign(unsigned char* signature, const unsigned char* message, size_t message_len,
                  const unsigned char* public_key, const unsigned char* secret_key);

/* Verify a signature, returns 1 on success, 0 on failure */
int ed25519_verify(const unsigned char* signature, const unsigned char* message, size_t message_len,
                   const unsigned char* public_key);

/* Generate seed from random */
void ed25519_random_seed(unsigned char* seed);

#ifdef __cplusplus
}
#endif

#endif /* ED25519_H */
