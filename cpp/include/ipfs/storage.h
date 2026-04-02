/**
 * IPFS Local Storage
 * Block storage and LMDB indexing
 */

#ifndef IPFS_STORAGE_H
#define IPFS_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include "chunk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IPFS_STORAGE_PATH_LEN   512
#define IPFS_DB_PATH_LEN        256

/* Opaque LMDB types */
typedef struct ipfs_storage ipfs_storage_t;

/* Initialize storage system and create ~/.my_ipfs/blocks/ directory */
int ipfs_storage_init(ipfs_storage_t** storage, const char* base_path);

/* Shutdown and cleanup storage */
void ipfs_storage_shutdown(ipfs_storage_t* storage);

/* Store a block by its hash */
int ipfs_storage_put(ipfs_storage_t* storage, const uint8_t* hash, const uint8_t* data, size_t len);

/* Retrieve a block by its hash */
int ipfs_storage_get(ipfs_storage_t* storage, const uint8_t* hash, uint8_t** data, size_t* len);

/* Check if a block exists */
int ipfs_storage_has(ipfs_storage_t* storage, const uint8_t* hash);

/* Delete a block */
int ipfs_storage_delete(ipfs_storage_t* storage, const uint8_t* hash);

/* Store a manifest */
int ipfs_storage_put_manifest(ipfs_storage_t* storage, const ipfs_manifest_t* manifest);

/* Retrieve a manifest */
int ipfs_storage_get_manifest(ipfs_storage_t* storage, const uint8_t* cid, ipfs_manifest_t* manifest);

/* Get the physical file path for a block hash */
int ipfs_storage_get_block_path(ipfs_storage_t* storage, const uint8_t* hash, 
                                 char* path, size_t path_len);

/* Get storage statistics */
typedef struct {
    uint64_t block_count;
    uint64_t total_bytes;
    uint64_t manifest_count;
} ipfs_storage_stats_t;

int ipfs_storage_stats(ipfs_storage_t* storage, ipfs_storage_stats_t* stats);

/* Pin a block (mark as important, don't garbage collect) */
int ipfs_storage_pin(ipfs_storage_t* storage, const uint8_t* hash);

/* Unpin a block */
int ipfs_storage_unpin(ipfs_storage_t* storage, const uint8_t* hash);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_STORAGE_H */
