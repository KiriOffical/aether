/**
 * IPFS-like Content Addressable Storage
 * Chunking, BLAKE2b hashing, and Merkle tree manifest
 */

#include "ipfs/chunk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sodium.h>

#define JSON_BUFFER_SIZE (1024 * 1024)  /* 1MB max JSON */

/* Compute BLAKE2b hash of data */
static int compute_blake2b(uint8_t* hash, const uint8_t* data, size_t len) {
    if (crypto_generichash(hash, IPFS_HASH_SIZE, data, len, NULL, 0) != 0) {
        return -1;
    }
    return 0;
}

/* Compute Merkle root recursively */
static int merkle_root_recursive(const uint8_t hashes[][IPFS_HASH_SIZE], 
                                  size_t count, uint8_t* root) {
    if (count == 0) {
        return -1;
    }
    
    if (count == 1) {
        memcpy(root, hashes[0], IPFS_HASH_SIZE);
        return 0;
    }
    
    /* If odd number, duplicate last hash */
    size_t pair_count = (count + 1) / 2;
    uint8_t* paired = malloc(pair_count * IPFS_HASH_SIZE * 2);
    if (!paired) {
        return -1;
    }
    
    for (size_t i = 0; i < pair_count; i++) {
        uint8_t combined[IPFS_HASH_SIZE * 2];
        memcpy(combined, hashes[i * 2], IPFS_HASH_SIZE);
        
        if (i * 2 + 1 < count) {
            memcpy(combined + IPFS_HASH_SIZE, hashes[i * 2 + 1], IPFS_HASH_SIZE);
        } else {
            /* Duplicate last hash for odd count */
            memcpy(combined + IPFS_HASH_SIZE, hashes[i * 2], IPFS_HASH_SIZE);
        }
        
        if (compute_blake2b(paired + (i * IPFS_HASH_SIZE), combined, sizeof(combined)) != 0) {
            free(paired);
            return -1;
        }
    }
    
    int result = merkle_root_recursive((const uint8_t (*)[IPFS_HASH_SIZE])paired, 
                                        pair_count, root);
    free(paired);
    return result;
}

int ipfs_compute_merkle_root(const uint8_t hashes[][IPFS_HASH_SIZE], 
                              size_t count, uint8_t* root) {
    if (!hashes || !root || count == 0) {
        return -1;
    }
    return merkle_root_recursive(hashes, count, root);
}

int ipfs_chunk_file(const char* filepath, ipfs_manifest_t* manifest) {
    if (!filepath || !manifest) {
        return -1;
    }
    
    memset(manifest, 0, sizeof(ipfs_manifest_t));
    
    /* Initialize libsodium if not already done */
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        return -1;
    }
    
    /* Open file */
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return -1;
    }
    
    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fprintf(stderr, "Empty or invalid file\n");
        fclose(fp);
        return -1;
    }
    
    manifest->total_size = (uint64_t)file_size;
    manifest->block_size = IPFS_CHUNK_SIZE;
    manifest->block_count = (file_size + IPFS_CHUNK_SIZE - 1) / IPFS_CHUNK_SIZE;
    
    /* Extract filename */
    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    strncpy(manifest->filename, filename, sizeof(manifest->filename) - 1);
    
    /* Allocate entries */
    manifest->entries = calloc(manifest->block_count, sizeof(ipfs_manifest_entry_t));
    if (!manifest->entries) {
        fclose(fp);
        return -1;
    }
    
    /* Allocate buffer for block hashes */
    uint8_t (*block_hashes)[IPFS_HASH_SIZE] = calloc(manifest->block_count, IPFS_HASH_SIZE);
    if (!block_hashes) {
        free(manifest->entries);
        manifest->entries = NULL;
        fclose(fp);
        return -1;
    }
    
    /* Read and chunk file */
    uint8_t* buffer = malloc(IPFS_CHUNK_SIZE);
    if (!buffer) {
        free(block_hashes);
        free(manifest->entries);
        manifest->entries = NULL;
        fclose(fp);
        return -1;
    }
    
    for (uint32_t i = 0; i < manifest->block_count; i++) {
        size_t bytes_read = fread(buffer, 1, IPFS_CHUNK_SIZE, fp);
        if (bytes_read == 0) {
            break;
        }
        
        manifest->entries[i].block_index = i;
        manifest->entries[i].block_size = (uint32_t)bytes_read;
        
        /* Compute BLAKE2b hash */
        if (compute_blake2b(manifest->entries[i].block_hash, buffer, bytes_read) != 0) {
            fprintf(stderr, "Failed to hash block %u\n", i);
            free(buffer);
            free(block_hashes);
            free(manifest->entries);
            manifest->entries = NULL;
            fclose(fp);
            return -1;
        }
        
        memcpy(block_hashes[i], manifest->entries[i].block_hash, IPFS_HASH_SIZE);
    }
    
    free(buffer);
    fclose(fp);
    
    /* Compute Merkle root (CID) */
    if (ipfs_compute_merkle_root((const uint8_t (*)[IPFS_HASH_SIZE])block_hashes,
                                  manifest->block_count, manifest->cid) != 0) {
        fprintf(stderr, "Failed to compute Merkle root\n");
        free(block_hashes);
        free(manifest->entries);
        manifest->entries = NULL;
        return -1;
    }
    
    free(block_hashes);
    return 0;
}

void ipfs_manifest_free(ipfs_manifest_t* manifest) {
    if (!manifest) {
        return;
    }
    if (manifest->entries) {
        free(manifest->entries);
        manifest->entries = NULL;
    }
    memset(manifest, 0, sizeof(ipfs_manifest_t));
}

int ipfs_verify_block(const uint8_t* data, size_t len, const uint8_t* expected_hash) {
    if (!data || !expected_hash) {
        return -1;
    }
    
    uint8_t computed_hash[IPFS_HASH_SIZE];
    if (compute_blake2b(computed_hash, data, len) != 0) {
        return -1;
    }
    
    return memcmp(computed_hash, expected_hash, IPFS_HASH_SIZE) == 0 ? 0 : -1;
}

/* Simple JSON serialization */
static char hex_char(int v) {
    return "0123456789abcdef"[v & 0xF];
}

static void bytes_to_hex(const uint8_t* bytes, size_t len, char* hex) {
    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_char(bytes[i] >> 4);
        hex[i * 2 + 1] = hex_char(bytes[i] & 0xF);
    }
    hex[len * 2] = '\0';
}

int ipfs_manifest_serialize(const ipfs_manifest_t* manifest, char* json, size_t* json_len) {
    if (!manifest || !json || !json_len) {
        return -1;
    }
    
    char cid_hex[IPFS_CID_SIZE * 2 + 1];
    bytes_to_hex(manifest->cid, IPFS_CID_SIZE, cid_hex);
    
    size_t offset = 0;
    int written;
    
    written = snprintf(json + offset, *json_len - offset,
        "{\"cid\":\"%s\",\"filename\":\"%s\",\"total_size\":%lu,\"block_count\":%u,\"blocks\":[",
        cid_hex, manifest->filename, (unsigned long)manifest->total_size, 
        manifest->block_count);
    
    if (written < 0 || (size_t)written >= *json_len - offset) {
        return -1;
    }
    offset += written;
    
    for (uint32_t i = 0; i < manifest->block_count; i++) {
        char hash_hex[IPFS_HASH_SIZE * 2 + 1];
        bytes_to_hex(manifest->entries[i].block_hash, IPFS_HASH_SIZE, hash_hex);
        
        written = snprintf(json + offset, *json_len - offset,
            "{\"index\":%u,\"hash\":\"%s\",\"size\":%u}",
            manifest->entries[i].block_index, hash_hex, manifest->entries[i].block_size);
        
        if (written < 0 || (size_t)written >= *json_len - offset) {
            return -1;
        }
        offset += written;
        
        if (i < manifest->block_count - 1) {
            json[offset++] = ',';
        }
    }
    
    written = snprintf(json + offset, *json_len - offset, "]}");
    if (written < 0 || (size_t)written >= *json_len - offset) {
        return -1;
    }
    offset += written;
    
    *json_len = offset;
    json[offset] = '\0';
    return 0;
}

/* Simple hex parser */
static int hex_to_bytes(const char* hex, uint8_t* bytes, size_t max_len) {
    /* Find the end of the hex string (quote or null terminator) */
    const char* end = strchr(hex, '"');
    size_t hex_len = end ? (size_t)(end - hex) : strlen(hex);
    
    if (hex_len % 2 != 0 || hex_len / 2 > max_len) {
        return -1;
    }

    for (size_t i = 0; i < hex_len / 2; i++) {
        int high = 0, low = 0;
        char c = hex[i * 2];
        if (c >= '0' && c <= '9') high = c - '0';
        else if (c >= 'a' && c <= 'f') high = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') high = c - 'A' + 10;
        else return -1;

        c = hex[i * 2 + 1];
        if (c >= '0' && c <= '9') low = c - '0';
        else if (c >= 'a' && c <= 'f') low = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') low = c - 'A' + 10;
        else return -1;

        bytes[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
}

int ipfs_manifest_deserialize(const char* json, size_t json_len, ipfs_manifest_t* manifest) {
    /* Simplified JSON parser - production would use cJSON or similar */
    if (!json || !manifest || json_len == 0) {
        return -1;
    }
    
    memset(manifest, 0, sizeof(ipfs_manifest_t));
    
    /* Find CID */
    const char* cid_pos = strstr(json, "\"cid\":\"");
    if (!cid_pos) return -1;
    cid_pos += 7;
    if (hex_to_bytes(cid_pos, manifest->cid, IPFS_CID_SIZE) != 0) {
        return -1;
    }
    
    /* Find filename */
    const char* fn_pos = strstr(json, "\"filename\":\"");
    if (!fn_pos) return -1;
    fn_pos += 12;
    const char* fn_end = strchr(fn_pos, '"');
    if (!fn_end) return -1;
    size_t fn_len = fn_end - fn_pos;
    if (fn_len >= sizeof(manifest->filename)) {
        fn_len = sizeof(manifest->filename) - 1;
    }
    memcpy(manifest->filename, fn_pos, fn_len);
    manifest->filename[fn_len] = '\0';
    
    /* Find total_size */
    const char* size_pos = strstr(json, "\"total_size\":");
    if (!size_pos) return -1;
    manifest->total_size = strtoull(size_pos + 13, NULL, 10);
    
    /* Find block_count */
    const char* count_pos = strstr(json, "\"block_count\":");
    if (!count_pos) return -1;
    manifest->block_count = (uint32_t)strtoul(count_pos + 14, NULL, 10);
    
    if (manifest->block_count == 0 || manifest->block_count > IPFS_MAX_BLOCKS) {
        return -1;
    }
    
    /* Allocate entries */
    manifest->entries = calloc(manifest->block_count, sizeof(ipfs_manifest_entry_t));
    if (!manifest->entries) {
        return -1;
    }
    
    /* Parse blocks - look for each hash in order */
    const char* blocks_pos = strstr(json, "\"blocks\":[");
    if (!blocks_pos) {
        free(manifest->entries);
        manifest->entries = NULL;
        return -1;
    }
    
    /* Find all hash entries */
    const char* hash_search = blocks_pos;
    for (uint32_t i = 0; i < manifest->block_count; i++) {
        const char* hash_pos = strstr(hash_search, "\"hash\":\"");
        if (!hash_pos) {
            free(manifest->entries);
            manifest->entries = NULL;
            return -1;
        }
        if (hex_to_bytes(hash_pos + 8, manifest->entries[i].block_hash, IPFS_HASH_SIZE) != 0) {
            free(manifest->entries);
            manifest->entries = NULL;
            return -1;
        }
        manifest->entries[i].block_index = i;
        
        /* Find size for this block */
        const char* bsize_pos = strstr(hash_pos, "\"size\":");
        if (!bsize_pos) {
            free(manifest->entries);
            manifest->entries = NULL;
            return -1;
        }
        manifest->entries[i].block_size = (uint32_t)strtoul(bsize_pos + 7, NULL, 10);
        
        hash_search = bsize_pos + 7;
    }
    
    manifest->block_size = IPFS_CHUNK_SIZE;
    return 0;
}

int ipfs_manifest_save(const ipfs_manifest_t* manifest, const char* path) {
    if (!manifest || !path) {
        return -1;
    }
    
    char* json = malloc(JSON_BUFFER_SIZE);
    if (!json) {
        return -1;
    }
    
    size_t json_len = JSON_BUFFER_SIZE;
    if (ipfs_manifest_serialize(manifest, json, &json_len) != 0) {
        free(json);
        return -1;
    }
    
    FILE* fp = fopen(path, "w");
    if (!fp) {
        free(json);
        return -1;
    }
    
    fwrite(json, 1, json_len, fp);
    fclose(fp);
    free(json);
    return 0;
}

int ipfs_manifest_load(const char* path, ipfs_manifest_t* manifest) {
    if (!path || !manifest) {
        return -1;
    }
    
    FILE* fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > JSON_BUFFER_SIZE) {
        fclose(fp);
        return -1;
    }
    
    char* json = malloc(file_size + 1);
    if (!json) {
        fclose(fp);
        return -1;
    }
    
    size_t read_size = fread(json, 1, file_size, fp);
    json[read_size] = '\0';
    fclose(fp);
    
    int result = ipfs_manifest_deserialize(json, read_size, manifest);
    free(json);
    return result;
}
