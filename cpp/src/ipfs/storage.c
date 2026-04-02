/**
 * IPFS Local Storage
 * Block storage and LMDB indexing
 */

#include "ipfs/storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <lmdb.h>

#define IPFS_BLOCKS_SUBDIR    "blocks"
#define IPFS_MANIFESTS_SUBDIR "manifests"
#define IPFS_DB_NAME          "index.db"
#define IPFS_MAX_PATH_LEN     512

struct ipfs_storage {
    char base_path[IPFS_STORAGE_PATH_LEN];
    char blocks_path[IPFS_STORAGE_PATH_LEN];
    char manifests_path[IPFS_STORAGE_PATH_LEN];
    MDB_env* lmdb_env;
    MDB_dbi lmdb_dbi;
    int refs;
};

/* Convert hash to filename (use first 2 chars as subdir for sharding) */
static void hash_to_path(const char* base, const uint8_t* hash, char* out, size_t out_len) {
    char hash_hex[IPFS_HASH_SIZE * 2 + 1];
    for (int i = 0; i < IPFS_HASH_SIZE; i++) {
        sprintf(hash_hex + (i * 2), "%02x", hash[i]);
    }
    snprintf(out, out_len, "%s/%c%c/%s", base, hash_hex[0], hash_hex[1], hash_hex);
}

/* Create directory if it doesn't exist */
static int ensure_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;  /* Already exists */
        }
        return -1;  /* Exists but not a directory */
    }
    
    /* Create parent directory first */
    char* path_copy = strdup(path);
    if (!path_copy) return -1;
    
    char* last_slash = strrchr(path_copy, '/');
    if (last_slash && last_slash != path_copy) {
        *last_slash = '\0';
        if (ensure_dir(path_copy) != 0) {
            free(path_copy);
            return -1;
        }
        *last_slash = '/';
    }
    
    free(path_copy);
    
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int ipfs_storage_init(ipfs_storage_t** storage, const char* base_path) {
    if (!storage) {
        return -1;
    }
    
    *storage = calloc(1, sizeof(ipfs_storage_t));
    if (!*storage) {
        return -1;
    }
    
    ipfs_storage_t* s = *storage;
    
    /* Set base path */
    if (base_path) {
        strncpy(s->base_path, base_path, sizeof(s->base_path) - 1);
    } else {
        /* Default to ~/.my_ipfs */
        const char* home = getenv("HOME");
        if (!home) {
            home = getenv("USERPROFILE");  /* Windows */
        }
        if (home) {
            snprintf(s->base_path, sizeof(s->base_path), "%s/.my_ipfs", home);
        } else {
            snprintf(s->base_path, sizeof(s->base_path), "./.my_ipfs");
        }
    }
    
    /* Create base directory */
    if (ensure_dir(s->base_path) != 0) {
        fprintf(stderr, "Failed to create base directory: %s\n", s->base_path);
        free(s);
        *storage = NULL;
        return -1;
    }
    
    /* Set subdirectory paths */
    snprintf(s->blocks_path, sizeof(s->blocks_path), "%s/%s", 
             s->base_path, IPFS_BLOCKS_SUBDIR);
    snprintf(s->manifests_path, sizeof(s->manifests_path), "%s/%s",
             s->base_path, IPFS_MANIFESTS_SUBDIR);
    
    /* Create subdirectories */
    if (ensure_dir(s->blocks_path) != 0 || ensure_dir(s->manifests_path) != 0) {
        fprintf(stderr, "Failed to create subdirectories\n");
        free(s);
        *storage = NULL;
        return -1;
    }
    
    /* Initialize LMDB */
    char db_path[IPFS_STORAGE_PATH_LEN];
    snprintf(db_path, sizeof(db_path), "%s/%s", s->base_path, IPFS_DB_NAME);
    
    if (mdb_env_create(&s->lmdb_env) != 0) {
        fprintf(stderr, "Failed to create LMDB environment\n");
        free(s);
        *storage = NULL;
        return -1;
    }
    
    /* Set map size (1GB default) */
    if (mdb_env_set_mapsize(s->lmdb_env, 1024ULL * 1024 * 1024) != 0) {
        fprintf(stderr, "Failed to set LMDB map size\n");
        mdb_env_close(s->lmdb_env);
        free(s);
        *storage = NULL;
        return -1;
    }
    
    /* Set max databases */
    if (mdb_env_set_maxdbs(s->lmdb_env, 2) != 0) {
        fprintf(stderr, "Failed to set LMDB max databases\n");
        mdb_env_close(s->lmdb_env);
        free(s);
        *storage = NULL;
        return -1;
    }
    
    /* Open LMDB */
    if (mdb_env_open(s->lmdb_env, db_path, MDB_NOSUBDIR, 0664) != 0) {
        fprintf(stderr, "Failed to open LMDB: %s\n", db_path);
        mdb_env_close(s->lmdb_env);
        free(s);
        *storage = NULL;
        return -1;
    }
    
    /* Open/create database */
    MDB_txn* txn;
    if (mdb_txn_begin(s->lmdb_env, NULL, 0, &txn) != 0) {
        fprintf(stderr, "Failed to begin LMDB transaction\n");
        mdb_env_close(s->lmdb_env);
        free(s);
        *storage = NULL;
        return -1;
    }
    
    if (mdb_dbi_open(txn, NULL, MDB_CREATE, &s->lmdb_dbi) != 0) {
        fprintf(stderr, "Failed to open LMDB database\n");
        mdb_txn_abort(txn);
        mdb_env_close(s->lmdb_env);
        free(s);
        *storage = NULL;
        return -1;
    }
    
    if (mdb_txn_commit(txn) != 0) {
        fprintf(stderr, "Failed to commit LMDB transaction\n");
        mdb_env_close(s->lmdb_env);
        free(s);
        *storage = NULL;
        return -1;
    }
    
    s->refs = 1;
    return 0;
}

void ipfs_storage_shutdown(ipfs_storage_t* storage) {
    if (!storage) {
        return;
    }
    
    storage->refs--;
    if (storage->refs > 0) {
        return;
    }
    
    if (storage->lmdb_env) {
        mdb_env_close(storage->lmdb_env);
    }
    
    free(storage);
}

int ipfs_storage_put(ipfs_storage_t* storage, const uint8_t* hash, 
                     const uint8_t* data, size_t len) {
    if (!storage || !hash || !data || len == 0) {
        return -1;
    }
    
    /* Generate file path */
    char file_path[IPFS_MAX_PATH_LEN];
    hash_to_path(storage->blocks_path, hash, file_path, sizeof(file_path));
    
    /* Check if already exists */
    struct stat st;
    if (stat(file_path, &st) == 0) {
        return 0;  /* Already stored */
    }
    
    /* Create parent directory (shard subdir) */
    char* last_slash = strrchr(file_path, '/');
    if (last_slash) {
        char dir_path[IPFS_MAX_PATH_LEN];
        strncpy(dir_path, file_path, last_slash - file_path);
        dir_path[last_slash - file_path] = '\0';
        ensure_dir(dir_path);
    }
    
    /* Write block to file */
    FILE* fp = fopen(file_path, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to create block file: %s\n", file_path);
        return -1;
    }
    
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    
    if (written != len) {
        fprintf(stderr, "Failed to write block data\n");
        return -1;
    }
    
    /* Update LMDB index */
    MDB_txn* txn;
    if (mdb_txn_begin(storage->lmdb_env, NULL, 0, &txn) != 0) {
        return -1;
    }
    
    MDB_val key, val;
    key.mv_data = (void*)hash;
    key.mv_size = IPFS_HASH_SIZE;
    val.mv_data = (void*)file_path;
    val.mv_size = strlen(file_path);
    
    if (mdb_put(txn, storage->lmdb_dbi, &key, &val, 0) != 0) {
        mdb_txn_abort(txn);
        return -1;
    }
    
    if (mdb_txn_commit(txn) != 0) {
        return -1;
    }
    
    return 0;
}

int ipfs_storage_get(ipfs_storage_t* storage, const uint8_t* hash, 
                     uint8_t** data, size_t* len) {
    if (!storage || !hash || !data || !len) {
        return -1;
    }
    
    *data = NULL;
    *len = 0;
    
    /* Look up path in LMDB */
    MDB_txn* txn;
    if (mdb_txn_begin(storage->lmdb_env, NULL, MDB_RDONLY, &txn) != 0) {
        return -1;
    }

    MDB_val key, val;
    key.mv_data = (void*)hash;
    key.mv_size = IPFS_HASH_SIZE;

    int rc = mdb_get(txn, storage->lmdb_dbi, &key, &val);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return -1;  /* Not found */
    }
    
    char file_path[IPFS_MAX_PATH_LEN];
    if (val.mv_size >= sizeof(file_path)) {
        mdb_txn_abort(txn);
        return -1;
    }
    memcpy(file_path, val.mv_data, val.mv_size);
    file_path[val.mv_size] = '\0';
    mdb_txn_abort(txn);
    
    /* Read file */
    FILE* fp = fopen(file_path, "rb");
    if (!fp) {
        return -1;
    }
    
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
    if (!storage || !hash) {
        return 0;
    }
    
    /* Check LMDB */
    MDB_txn* txn;
    if (mdb_txn_begin(storage->lmdb_env, NULL, MDB_RDONLY, &txn) != 0) {
        return 0;
    }

    MDB_val key, val;
    key.mv_data = (void*)hash;
    key.mv_size = IPFS_HASH_SIZE;

    int rc = mdb_get(txn, storage->lmdb_dbi, &key, &val);
    mdb_txn_abort(txn);
    
    if (rc != 0) {
        return 0;
    }
    
    /* Also verify file exists */
    char file_path[IPFS_MAX_PATH_LEN];
    if (val.mv_size >= sizeof(file_path)) {
        return 0;
    }
    memcpy(file_path, val.mv_data, val.mv_size);
    file_path[val.mv_size] = '\0';
    
    struct stat st;
    return (stat(file_path, &st) == 0) ? 1 : 0;
}

int ipfs_storage_delete(ipfs_storage_t* storage, const uint8_t* hash) {
    if (!storage || !hash) {
        return -1;
    }
    
    /* Get path first */
    MDB_txn* txn;
    if (mdb_txn_begin(storage->lmdb_env, NULL, MDB_RDONLY, &txn) != 0) {
        return -1;
    }

    MDB_val key, val;
    key.mv_data = (void*)hash;
    key.mv_size = IPFS_HASH_SIZE;

    int rc = mdb_get(txn, storage->lmdb_dbi, &key, &val);
    mdb_txn_abort(txn);
    
    if (rc != 0) {
        return -1;  /* Not found */
    }
    
    char file_path[IPFS_MAX_PATH_LEN];
    if (val.mv_size >= sizeof(file_path)) {
        return -1;
    }
    memcpy(file_path, val.mv_data, val.mv_size);
    file_path[val.mv_size] = '\0';
    
    /* Delete file */
    if (remove(file_path) != 0) {
        return -1;
    }
    
    /* Delete from LMDB */
    if (mdb_txn_begin(storage->lmdb_env, NULL, 0, &txn) != 0) {
        return -1;
    }

    if (mdb_del(txn, storage->lmdb_dbi, &key, NULL) != 0) {
        mdb_txn_abort(txn);
        return -1;
    }
    
    if (mdb_txn_commit(txn) != 0) {
        return -1;
    }
    
    return 0;
}

int ipfs_storage_put_manifest(ipfs_storage_t* storage, const ipfs_manifest_t* manifest) {
    if (!storage || !manifest) {
        return -1;
    }
    
    char manifest_path[IPFS_MAX_PATH_LEN];
    char cid_hex[IPFS_CID_SIZE * 2 + 1];
    for (int i = 0; i < IPFS_CID_SIZE; i++) {
        sprintf(cid_hex + (i * 2), "%02x", manifest->cid[i]);
    }
    
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s.json",
             storage->manifests_path, cid_hex);
    
    return ipfs_manifest_save(manifest, manifest_path);
}

int ipfs_storage_get_manifest(ipfs_storage_t* storage, const uint8_t* cid,
                              ipfs_manifest_t* manifest) {
    if (!storage || !cid || !manifest) {
        return -1;
    }

    char cid_hex[IPFS_CID_SIZE * 2 + 1];
    for (int i = 0; i < IPFS_CID_SIZE; i++) {
        sprintf(cid_hex + (i * 2), "%02x", cid[i]);
    }
    cid_hex[IPFS_CID_SIZE * 2] = '\0';

    char manifest_path[IPFS_MAX_PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s.json",
             storage->manifests_path, cid_hex);

    return ipfs_manifest_load(manifest_path, manifest);
}

int ipfs_storage_get_block_path(ipfs_storage_t* storage, const uint8_t* hash,
                                 char* path, size_t path_len) {
    if (!storage || !hash || !path || path_len < IPFS_MAX_PATH_LEN) {
        return -1;
    }
    
    /* Look up in LMDB */
    MDB_txn* txn;
    if (mdb_txn_begin(storage->lmdb_env, NULL, MDB_RDONLY, &txn) != 0) {
        return -1;
    }

    MDB_val key, val;
    key.mv_data = (void*)hash;
    key.mv_size = IPFS_HASH_SIZE;

    int rc = mdb_get(txn, storage->lmdb_dbi, &key, &val);
    mdb_txn_abort(txn);
    
    if (rc != 0 || val.mv_size >= path_len) {
        return -1;
    }
    
    memcpy(path, val.mv_data, val.mv_size);
    path[val.mv_size] = '\0';
    return 0;
}

int ipfs_storage_stats(ipfs_storage_t* storage, ipfs_storage_stats_t* stats) {
    if (!storage || !stats) {
        return -1;
    }
    
    memset(stats, 0, sizeof(ipfs_storage_stats_t));
    
    /* Count blocks in LMDB */
    MDB_txn* txn;
    if (mdb_txn_begin(storage->lmdb_env, NULL, MDB_RDONLY, &txn) != 0) {
        return -1;
    }

    MDB_cursor* cursor;
    if (mdb_cursor_open(txn, storage->lmdb_dbi, &cursor) != 0) {
        mdb_txn_abort(txn);
        return -1;
    }
    
    MDB_val key, val;
    MDB_stat db_stat;
    
    if (mdb_stat(txn, storage->lmdb_dbi, &db_stat) == 0) {
        stats->block_count = db_stat.ms_entries;
    }
    
    /* Iterate to count total bytes */
    MDB_val get_key, get_val;
    int rc = mdb_cursor_get(cursor, &get_key, &get_val, MDB_FIRST);
    while (rc == 0) {
        char file_path[IPFS_MAX_PATH_LEN];
        if (get_val.mv_size < sizeof(file_path)) {
            memcpy(file_path, get_val.mv_data, get_val.mv_size);
            file_path[get_val.mv_size] = '\0';
            
            struct stat st;
            if (stat(file_path, &st) == 0) {
                stats->total_bytes += st.st_size;
            }
        }
        rc = mdb_cursor_get(cursor, &get_key, &get_val, MDB_NEXT);
    }
    
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    
    /* Count manifests */
    char pattern[IPFS_STORAGE_PATH_LEN];
    snprintf(pattern, sizeof(pattern), "%s/*.json", storage->manifests_path);
    
    /* Simple glob-like counting (production would use proper glob) */
    FILE* fp = popen("find 2>/dev/null", "r");  /* Placeholder */
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, ".json")) {
                stats->manifest_count++;
            }
        }
        pclose(fp);
    }
    
    return 0;
}

int ipfs_storage_pin(ipfs_storage_t* storage, const uint8_t* hash) {
    /* For MVP, pins are stored in a separate LMDB database */
    /* Simplified: just mark in the main DB with a prefix */
    if (!storage || !hash) {
        return -1;
    }
    
    MDB_txn* txn;
    if (mdb_txn_begin(storage->lmdb_env, NULL, 0, &txn) != 0) {
        return -1;
    }

    /* Store with "pin:" prefix */
    MDB_val key, val;
    char pin_key[IPFS_HASH_SIZE + 8];
    memcpy(pin_key, "pin:", 4);
    memcpy(pin_key + 4, hash, IPFS_HASH_SIZE);
    key.mv_data = pin_key;
    key.mv_size = IPFS_HASH_SIZE + 4;
    val.mv_data = (void*)"pinned";
    val.mv_size = 6;
    
    int rc = mdb_put(txn, storage->lmdb_dbi, &key, &val, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return -1;
    }
    
    if (mdb_txn_commit(txn) != 0) {
        return -1;
    }
    
    return 0;
}

int ipfs_storage_unpin(ipfs_storage_t* storage, const uint8_t* hash) {
    if (!storage || !hash) {
        return -1;
    }
    
    MDB_txn* txn;
    if (mdb_txn_begin(storage->lmdb_env, NULL, 0, &txn) != 0) {
        return -1;
    }

    char pin_key[IPFS_HASH_SIZE + 8];
    memcpy(pin_key, "pin:", 4);
    memcpy(pin_key + 4, hash, IPFS_HASH_SIZE);
    
    MDB_val key;
    key.mv_data = pin_key;
    key.mv_size = IPFS_HASH_SIZE + 4;
    
    int rc = mdb_del(txn, storage->lmdb_dbi, &key, NULL);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return -1;
    }
    
    if (mdb_txn_commit(txn) != 0) {
        return -1;
    }
    
    return 0;
}
