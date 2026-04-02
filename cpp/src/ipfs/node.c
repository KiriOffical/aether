/**
 * IPFS Node - Main Interface
 * Unified API for the IPFS-like daemon
 */

#include "ipfs/node.h"
#include "ipfs/chunk.h"
#include "ipfs/storage.h"
#include "ipfs/network.h"
#include "ipfs/http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

struct ipfs_node {
    ipfs_node_config_t config;
    ipfs_storage_t* storage;
    ipfs_network_t* network;
    ipfs_http_server_t* http;
    int running;
    pthread_t main_thread;
    char http_url[128];
};

/* Global node for signal handling */
static ipfs_node_t* g_node = NULL;

void ipfs_node_config_default(ipfs_node_config_t* config) {
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(ipfs_node_config_t));
    
    /* Default data directory */
    const char* home = getenv("HOME");
    if (!home) {
        home = getenv("USERPROFILE");
    }
    if (home) {
        snprintf(config->data_dir, sizeof(config->data_dir), "%s/.my_ipfs", home);
    } else {
        snprintf(config->data_dir, sizeof(config->data_dir), "./.my_ipfs");
    }
    
    config->udp_port = IPFS_DEFAULT_UDP_PORT;
    config->tcp_port = IPFS_DEFAULT_TCP_PORT;
    config->http_port = 8080;
    strncpy(config->http_bind, "127.0.0.1", sizeof(config->http_bind) - 1);
    config->bootstrap_count = 0;
    config->log_level = 3;  /* info */
}

int ipfs_node_create(ipfs_node_t** node, const ipfs_node_config_t* config) {
    if (!node) {
        return -1;
    }
    
    *node = calloc(1, sizeof(ipfs_node_t));
    if (!*node) {
        return -1;
    }
    
    ipfs_node_t* n = *node;
    
    if (config) {
        n->config = *config;
    } else {
        ipfs_node_config_default(&n->config);
    }
    
    n->running = 0;
    g_node = n;
    
    return 0;
}

void ipfs_node_destroy(ipfs_node_t* node) {
    if (!node) {
        return;
    }
    
    ipfs_node_stop(node);
    g_node = NULL;
    free(node);
}

int ipfs_node_start(ipfs_node_t* node) {
    if (!node || node->running) {
        return -1;
    }
    
    printf("[IPFS Node] Starting...\n");
    printf("[IPFS Node] Data directory: %s\n", node->config.data_dir);
    printf("[IPFS Node] UDP port: %d\n", node->config.udp_port);
    printf("[IPFS Node] TCP port: %d\n", node->config.tcp_port);
    printf("[IPFS Node] HTTP port: %d\n", node->config.http_port);
    
    /* Initialize storage */
    printf("[IPFS Node] Initializing storage...\n");
    if (ipfs_storage_init(&node->storage, node->config.data_dir) != 0) {
        fprintf(stderr, "[IPFS Node] Failed to initialize storage\n");
        return -1;
    }
    
    /* Initialize network */
    printf("[IPFS Node] Initializing network...\n");
    if (ipfs_network_init(&node->network, node->config.udp_port, node->config.tcp_port) != 0) {
        fprintf(stderr, "[IPFS Node] Failed to initialize network\n");
        ipfs_storage_shutdown(node->storage);
        return -1;
    }
    
    /* Get node ID */
    uint8_t node_id[IPFS_NODE_ID_SIZE];
    ipfs_network_get_id(node->network, node_id);
    printf("[IPFS Node] Node ID: ");
    for (int i = 0; i < 8; i++) {
        printf("%02x", node_id[i]);
    }
    printf("...\n");
    
    /* Initialize HTTP server */
    printf("[IPFS Node] Starting HTTP server...\n");
    ipfs_http_config_t http_config = {
        .bind_addr = node->config.http_bind,
        .port = node->config.http_port,
        .max_connections = 100,
        .request_timeout_ms = 30000
    };
    
    if (ipfs_http_server_create(&node->http, &http_config, node->network, node->storage) != 0) {
        fprintf(stderr, "[IPFS Node] Failed to create HTTP server\n");
        ipfs_network_shutdown(node->network);
        ipfs_storage_shutdown(node->storage);
        return -1;
    }
    
    if (ipfs_http_server_start(node->http) != 0) {
        fprintf(stderr, "[IPFS Node] Failed to start HTTP server\n");
        ipfs_network_shutdown(node->network);
        ipfs_storage_shutdown(node->storage);
        return -1;
    }
    
    /* Build HTTP URL */
    uint16_t http_port = ipfs_http_server_get_port(node->http);
    snprintf(node->http_url, sizeof(node->http_url), "http://%s:%d",
             node->config.http_bind, http_port);
    
    printf("[IPFS Node] HTTP gateway: %s\n", node->http_url);
    
    /* Bootstrap with known peers */
    if (node->config.bootstrap_count > 0) {
        printf("[IPFS Node] Bootstrapping with %zu peers...\n", node->config.bootstrap_count);
        
        ipfs_endpoint_t peers[8];
        memset(peers, 0, sizeof(peers));
        
        for (size_t i = 0; i < node->config.bootstrap_count && i < 8; i++) {
            /* Parse bootstrap peer address (format: host:udp_port:tcp_port) */
            /* For MVP, this is simplified */
            strncpy(peers[i].addr, "127.0.0.1", sizeof(peers[i].addr) - 1);
            peers[i].udp_port = IPFS_DEFAULT_UDP_PORT;
            peers[i].tcp_port = IPFS_DEFAULT_TCP_PORT;
        }
        
        ipfs_network_bootstrap(node->network, peers, node->config.bootstrap_count);
    }
    
    node->running = 1;
    printf("[IPFS Node] Started successfully!\n");
    
    return 0;
}

void ipfs_node_stop(ipfs_node_t* node) {
    if (!node || !node->running) {
        return;
    }
    
    printf("[IPFS Node] Stopping...\n");
    
    node->running = 0;
    
    /* Stop HTTP server */
    if (node->http) {
        ipfs_http_server_stop(node->http);
        ipfs_http_server_destroy(node->http);
        node->http = NULL;
    }
    
    /* Shutdown network */
    if (node->network) {
        ipfs_network_shutdown(node->network);
        node->network = NULL;
    }
    
    /* Shutdown storage */
    if (node->storage) {
        ipfs_storage_shutdown(node->storage);
        node->storage = NULL;
    }
    
    printf("[IPFS Node] Stopped.\n");
}

/* Main event loop thread */
static void* node_main_thread(void* arg) {
    ipfs_node_t* node = (ipfs_node_t*)arg;
    
    while (node->running) {
        /* Poll network for incoming messages */
        if (node->network) {
            ipfs_network_poll(node->network, 100);
        }
        
        /* Small sleep to avoid busy-waiting */
        usleep(10000);  /* 10ms */
    }
    
    return NULL;
}

int ipfs_node_run(ipfs_node_t* node) {
    if (!node || !node->running) {
        return -1;
    }
    
    /* Start main loop in background */
    if (pthread_create(&node->main_thread, NULL, node_main_thread, node) != 0) {
        return -1;
    }
    
    /* Wait for main thread */
    pthread_join(node->main_thread, NULL);
    
    return 0;
}

int ipfs_node_add(ipfs_node_t* node, const char* filepath, uint8_t* cid) {
    if (!node || !filepath || !cid || !node->running) {
        return -1;
    }
    
    printf("[IPFS Node] Adding file: %s\n", filepath);
    
    /* Chunk and hash file */
    ipfs_manifest_t manifest;
    if (ipfs_chunk_file(filepath, &manifest) != 0) {
        fprintf(stderr, "[IPFS Node] Failed to chunk file\n");
        return -1;
    }
    
    printf("[IPFS Node] Chunked into %u blocks\n", manifest.block_count);
    printf("[IPFS Node] CID: ");
    for (int i = 0; i < IPFS_CID_SIZE; i++) {
        printf("%02x", manifest.cid[i]);
    }
    printf("\n");
    
    /* Read file again and store blocks */
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        ipfs_manifest_free(&manifest);
        return -1;
    }
    
    uint8_t* buffer = malloc(IPFS_CHUNK_SIZE);
    if (!buffer) {
        fclose(fp);
        ipfs_manifest_free(&manifest);
        return -1;
    }
    
    for (uint32_t i = 0; i < manifest.block_count; i++) {
        size_t bytes_read = fread(buffer, 1, manifest.entries[i].block_size, fp);
        if (bytes_read == 0) {
            break;
        }
        
        /* Store block */
        if (ipfs_storage_put(node->storage, manifest.entries[i].block_hash, 
                             buffer, bytes_read) != 0) {
            fprintf(stderr, "[IPFS Node] Failed to store block %u\n", i);
            free(buffer);
            fclose(fp);
            ipfs_manifest_free(&manifest);
            return -1;
        }
    }
    
    free(buffer);
    fclose(fp);
    
    /* Store manifest */
    if (ipfs_storage_put_manifest(node->storage, &manifest) != 0) {
        fprintf(stderr, "[IPFS Node] Failed to store manifest\n");
        ipfs_manifest_free(&manifest);
        return -1;
    }
    
    /* Announce to network */
    ipfs_network_provide(node->network, manifest.cid);
    
    /* Return CID */
    memcpy(cid, manifest.cid, IPFS_CID_SIZE);
    
    ipfs_manifest_free(&manifest);
    return 0;
}

int ipfs_node_get(ipfs_node_t* node, const uint8_t* cid, const char* output_path) {
    if (!node || !cid || !output_path || !node->running) {
        return -1;
    }
    
    printf("[IPFS Node] Getting CID: ");
    for (int i = 0; i < 8; i++) {
        printf("%02x", cid[i]);
    }
    printf("...\n");
    
    /* Load manifest */
    ipfs_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    
    int has_manifest = (ipfs_storage_get_manifest(node->storage, cid, &manifest) == 0);
    
    if (!has_manifest) {
        fprintf(stderr, "[IPFS Node] Manifest not found locally\n");
        ipfs_manifest_free(&manifest);
        return -1;
    }
    
    /* Validate manifest */
    if (manifest.block_count == 0 || manifest.entries == NULL) {
        fprintf(stderr, "[IPFS Node] Invalid manifest\n");
        ipfs_manifest_free(&manifest);
        return -1;
    }
    
    printf("[IPFS Node] Loading %u blocks...\n", manifest.block_count);
    
    /* Open output file */
    FILE* fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "[IPFS Node] Cannot open output file: %s\n", output_path);
        ipfs_manifest_free(&manifest);
        return -1;
    }
    
    /* Fetch and write blocks */
    uint8_t* block_data = NULL;
    size_t block_len = 0;
    uint32_t blocks_written = 0;
    
    for (uint32_t i = 0; i < manifest.block_count; i++) {
        if (manifest.entries == NULL) {
            fprintf(stderr, "[IPFS Node] Manifest entries error at block %u\n", i);
            fclose(fp);
            ipfs_manifest_free(&manifest);
            return -1;
        }
        
        uint8_t* hash = manifest.entries[i].block_hash;
        
        /* Try local storage first */
        if (ipfs_storage_get(node->storage, hash, &block_data, &block_len) == 0) {
            if (fwrite(block_data, 1, block_len, fp) != block_len) {
                fprintf(stderr, "[IPFS Node] Failed to write block %u\n", i);
                free(block_data);
                fclose(fp);
                ipfs_manifest_free(&manifest);
                return -1;
            }
            free(block_data);
            block_data = NULL;
            blocks_written++;
        } else {
            fprintf(stderr, "[IPFS Node] Block %u not found locally\n", i+1);
            fclose(fp);
            ipfs_manifest_free(&manifest);
            return -1;
        }
    }
    
    fclose(fp);
    printf("[IPFS Node] File saved to: %s\n", output_path);
    
    ipfs_manifest_free(&manifest);
    return 0;
}

int ipfs_node_get_block(ipfs_node_t* node, const uint8_t* hash,
                        uint8_t** data, size_t* len) {
    if (!node || !hash || !data || !len || !node->running) {
        return -1;
    }
    
    return ipfs_storage_get(node->storage, hash, data, len);
}

int ipfs_node_pin(ipfs_node_t* node, const uint8_t* cid) {
    if (!node || !cid || !node->storage) {
        return -1;
    }
    
    /* Pin all blocks in the manifest */
    ipfs_manifest_t manifest;
    if (ipfs_storage_get_manifest(node->storage, cid, &manifest) == 0) {
        for (uint32_t i = 0; i < manifest.block_count; i++) {
            ipfs_storage_pin(node->storage, manifest.entries[i].block_hash);
        }
        ipfs_manifest_free(&manifest);
    } else {
        /* Pin the CID itself (single block) */
        ipfs_storage_pin(node->storage, cid);
    }
    
    return 0;
}

int ipfs_node_unpin(ipfs_node_t* node, const uint8_t* cid) {
    if (!node || !cid || !node->storage) {
        return -1;
    }
    
    /* Unpin all blocks in the manifest */
    ipfs_manifest_t manifest;
    if (ipfs_storage_get_manifest(node->storage, cid, &manifest) == 0) {
        for (uint32_t i = 0; i < manifest.block_count; i++) {
            ipfs_storage_unpin(node->storage, manifest.entries[i].block_hash);
        }
        ipfs_manifest_free(&manifest);
    } else {
        ipfs_storage_unpin(node->storage, cid);
    }
    
    return 0;
}

int ipfs_node_status(ipfs_node_t* node, ipfs_node_status_t* status) {
    if (!node || !status) {
        return -1;
    }
    
    memset(status, 0, sizeof(ipfs_node_status_t));
    
    if (node->network) {
        ipfs_network_get_id(node->network, status->node_id);
        status->peer_count = ipfs_network_peer_count(node->network);
    }
    
    if (node->storage) {
        ipfs_storage_stats_t storage_stats;
        if (ipfs_storage_stats(node->storage, &storage_stats) == 0) {
            status->storage_used = storage_stats.total_bytes;
            status->storage_blocks = storage_stats.block_count;
        }
    }
    
    if (node->http) {
        status->http_running = ipfs_http_server_is_running(node->http);
        status->http_port = ipfs_http_server_get_port(node->http);
    }
    
    return 0;
}

const char* ipfs_node_get_http_url(ipfs_node_t* node) {
    return node ? node->http_url : NULL;
}

/* Signal handler */
static void signal_handler(int sig) {
    if (g_node && g_node->running) {
        printf("\n[IPFS Node] Received signal %d, shutting down...\n", sig);
        ipfs_node_stop(g_node);
    }
}

void ipfs_node_set_signal_handler(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

/* Public API to stop node from signal handler */
void ipfs_node_request_stop(ipfs_node_t* node) {
    if (node) {
        node->running = 0;
    }
}
