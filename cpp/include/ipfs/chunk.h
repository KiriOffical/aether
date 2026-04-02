/**
 * IPFS-like Content Addressable Storage
 * Chunking, BLAKE2b hashing, and Merkle tree manifest
 */

#ifndef IPFS_CHUNK_H
#define IPFS_CHUNK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IPFS_CHUNK_SIZE       (256 * 1024)  /* 256 KB */
#define IPFS_HASH_SIZE        32             /* BLAKE2b-256 */
#define IPFS_MAX_BLOCKS       100000
#define IPFS_CID_SIZE         IPFS_HASH_SIZE

/* Block structure */
typedef struct {
    uint8_t hash[IPFS_HASH_SIZE];
    uint8_t* data;
    size_t size;
} ipfs_block_t;

/* Manifest entry */
typedef struct {
    uint8_t block_hash[IPFS_HASH_SIZE];
    uint32_t block_index;
    uint32_t block_size;
} ipfs_manifest_entry_t;

/* File manifest */
typedef struct {
    uint8_t cid[IPFS_CID_SIZE];           /* Merkle root / Content ID */
    char filename[256];
    uint64_t total_size;
    uint32_t block_count;
    uint32_t block_size;
    ipfs_manifest_entry_t* entries;
} ipfs_manifest_t;

/* Chunk a file into 256KB blocks and compute BLAKE2b hashes */
int ipfs_chunk_file(const char* filepath, ipfs_manifest_t* manifest);

/* Free manifest resources */
void ipfs_manifest_free(ipfs_manifest_t* manifest);

/* Compute Merkle root from block hashes */
int ipfs_compute_merkle_root(const uint8_t hashes[][IPFS_HASH_SIZE], 
                              size_t count, uint8_t* root);

/* Verify a block against its expected hash */
int ipfs_verify_block(const uint8_t* data, size_t len, const uint8_t* expected_hash);

/* Serialize manifest to JSON */
int ipfs_manifest_serialize(const ipfs_manifest_t* manifest, char* json, size_t* json_len);

/* Deserialize manifest from JSON */
int ipfs_manifest_deserialize(const char* json, size_t json_len, ipfs_manifest_t* manifest);

/* Save manifest to disk */
int ipfs_manifest_save(const ipfs_manifest_t* manifest, const char* path);

/* Load manifest from disk */
int ipfs_manifest_load(const char* path, ipfs_manifest_t* manifest);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_CHUNK_H */
