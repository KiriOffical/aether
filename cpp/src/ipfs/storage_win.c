/**
 * IPFS Local Storage - Windows Version (No LMDB)
 * Simple file-based block storage
 */

#include "ipfs/storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <direct.h>

#ifdef _WIN32
    #include <windows.h>
    #define mkdir_win(path) _mkdir(path)
#else
    #define mkdir_win(path) mkdir(path, 0755)
#endif

#define IPFS_BLOCKS_SUBDIR    "blocks"
#define IPFS_MANIFESTS_SUBDIR "manifests"
#define IPFS_MAX_PATH_LEN     512

struct ipfs_storage {
    char base_path[IPFS_STORAGE_PATH_LEN];
    char blocks_path[IPFS_STORAGE_PATH_LEN];
    char manifests_path[IPFS_STORAGE_PATH_LEN];
    int refs;
};

/* Convert hash to filename */
static void hash_to_path(const char* base, const uint8_t* hash, char* out, size_t out_len) {
    char hash_hex[IPFS_HASH_SIZE * 2 + 1];
    for (int i = 0; i < IPFS_HASH_SIZE; i++) {
        sprintf(hash_hex + (i * 2), "%02x", hash[i]);
    }
    snprintf(out, out_len, "%s\\%c%c\\%s", base, hash_hex[0], hash_hex[1], hash_hex);
}

/* Create directory recursively */
static int ensure_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 0;
    }
    
    char* path_copy = strdup(path);
    if (!path_copy) return -1;
    
    char* p = path_copy;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            mkdir_win(path_copy);
            *p = '\\';
        }
        p++;
    }
    mkdir_win(path_copy);
    
    free(path_copy);
    return 0;
}

int ipfs_storage_init(ipfs_storage_t** storage, const char* base_path) {
    if (!storage) return -1;
    
    *storage = calloc(1, sizeof(ipfs_storage_t));
    if (!*storage) return -1;
    
    ipfs_storage_t* s = *storage;
    
    if (base_path) {
        strncpy(s->base_path, base_path, sizeof(s->base_path) - 1);
    } else {
        char* home = getenv("USERPROFILE");
        if (!home) home = ".";
        snprintf(s->base_path, sizeof(s->base_path), "%s\\.my_ipfs", home);
    }
    
    ensure_dir(s->base_path);
    
    snprintf(s->blocks_path, sizeof(s->blocks_path), "%s\\%s", s->base_path, IPFS_BLOCKS_SUBDIR);
    snprintf(s->manifests_path, sizeof(s->manifests_path), "%s\\%s", s->base_path, IPFS_MANIFESTS_SUBDIR);
    
    ensure_dir(s->blocks_path);
    ensure_dir(s->manifests_path);
    
    s->refs = 1;
    return 0;
}

void ipfs_storage_shutdown(ipfs_storage_t* storage) {
    if (!storage) return;
    storage->refs--;
    if (storage->refs > 0) return;
    free(storage);
}

int ipfs_storage_put(ipfs_storage_t* storage, const uint8_t* hash, 
                     const uint8_t* data, size_t len) {
    if (!storage || !hash || !data || len == 0) return -1;
    
    char file_path[IPFS_MAX_PATH_LEN];
    hash_to_path(storage->blocks_path, hash, file_path, sizeof(file_path));
    
    struct stat st;
    if (stat(file_path, &st) == 0) return 0;
    
    char* last_slash = strrchr(file_path, '\\');
    if (last_slash) {
        char dir_path[IPFS_MAX_PATH_LEN];
        strncpy(dir_path, file_path, last_slash - file_path);
        dir_path[last_slash - file_path] = '\0';
        ensure_dir(dir_path);
    }
    
    FILE* fp = fopen(file_path, "wb");
    if (!fp) return -1;
    
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    
    return (written == len) ? 0 : -1;
}

int ipfs_storage_get(ipfs_storage_t* storage, const uint8_t* hash, 
                     uint8_t** data, size_t* len) {
    if (!storage || !hash || !data || !len) return -1;
    
    *data = NULL;
    *len = 0;
    
    char file_path[IPFS_MAX_PATH_LEN];
    hash_to_path(storage->blocks_path, hash, file_path, sizeof(file_path));
    
    FILE* fp = fopen(file_path, "rb");
    if (!fp) return -1;
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(fp);
        return -1;
    }
    
    *data = malloc(file_size);
    if (!*data) {
        fclose(fp);
        return -1;
    }
    
    size_t read_bytes = fread(*data, 1, file_size, fp);
    fclose(fp);
    
    if ((long)read_bytes != file_size) {
        free(*data);
        *data = NULL;
        return -1;
    }
    
    *len = read_bytes;
    return 0;
}

int ipfs_storage_has(ipfs_storage_t* storage, const uint8_t* hash) {
    if (!storage || !hash) return 0;
    
    char file_path[IPFS_MAX_PATH_LEN];
    hash_to_path(storage->blocks_path, hash, file_path, sizeof(file_path));
    
    struct stat st;
    return (stat(file_path, &st) == 0) ? 1 : 0;
}

int ipfs_storage_delete(ipfs_storage_t* storage, const uint8_t* hash) {
    if (!storage || !hash) return -1;
    
    char file_path[IPFS_MAX_PATH_LEN];
    hash_to_path(storage->blocks_path, hash, file_path, sizeof(file_path));
    
    return remove(file_path);
}

int ipfs_storage_put_manifest(ipfs_storage_t* storage, const ipfs_manifest_t* manifest) {
    if (!storage || !manifest) return -1;
    
    char cid_hex[IPFS_CID_SIZE * 2 + 1];
    for (int i = 0; i < IPFS_CID_SIZE; i++) {
        sprintf(cid_hex + (i * 2), "%02x", manifest->cid[i]);
    }
    
    char manifest_path[IPFS_MAX_PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s\\%s.json",
             storage->manifests_path, cid_hex);
    
    return ipfs_manifest_save(manifest, manifest_path);
}

int ipfs_storage_get_manifest(ipfs_storage_t* storage, const uint8_t* cid,
                              ipfs_manifest_t* manifest) {
    if (!storage || !cid || !manifest) return -1;
    
    char cid_hex[IPFS_CID_SIZE * 2 + 1];
    for (int i = 0; i < IPFS_CID_SIZE; i++) {
        sprintf(cid_hex + (i * 2), "%02x", cid[i]);
    }
    
    char manifest_path[IPFS_MAX_PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s\\%s.json",
             storage->manifests_path, cid_hex);
    
    return ipfs_manifest_load(manifest_path, manifest);
}

int ipfs_storage_get_block_path(ipfs_storage_t* storage, const uint8_t* hash,
                                 char* path, size_t path_len) {
    if (!storage || !hash || !path || path_len < IPFS_MAX_PATH_LEN) return -1;
    
    hash_to_path(storage->blocks_path, hash, path, path_len);
    
    struct stat st;
    return (stat(path, &st) == 0) ? 0 : -1;
}

int ipfs_storage_stats(ipfs_storage_t* storage, ipfs_storage_stats_t* stats) {
    if (!storage || !stats) return -1;
    
    memset(stats, 0, sizeof(ipfs_storage_stats_t));
    
    /* Simple file counting - production would use proper directory traversal */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "dir /b /s \"%s\\*.json\" 2>nul | find /c /v \"\"", storage->manifests_path);
    FILE* fp = _popen(cmd, "r");
    if (fp) {
        fscanf(fp, "%llu", &stats->manifest_count);
        _pclose(fp);
    }
    
    snprintf(cmd, sizeof(cmd), "dir /b /s \"%s\\*\" 2>nul | find /c /v \"\"", storage->blocks_path);
    fp = _popen(cmd, "r");
    if (fp) {
        fscanf(fp, "%llu", &stats->block_count);
        _pclose(fp);
    }
    
    return 0;
}

int ipfs_storage_pin(ipfs_storage_t* storage, const uint8_t* hash) {
    /* Placeholder - pins stored in separate file */
    (void)storage;
    (void)hash;
    return 0;
}

int ipfs_storage_unpin(ipfs_storage_t* storage, const uint8_t* hash) {
    (void)storage;
    (void)hash;
    return 0;
}
