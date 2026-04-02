/**
 * SHA-256 Implementation
 * Public domain implementation
 */

#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_BLOCK_SIZE 64
#define SHA256_HASH_SIZE  32

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t* ctx);
void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t len);
void sha256_final(sha256_ctx_t* ctx, uint8_t* hash);

/* One-shot SHA-256 */
void sha256(uint8_t* hash, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
