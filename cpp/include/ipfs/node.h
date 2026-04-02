/**
 * IPFS Node - Main Interface
 * Unified API for the IPFS-like daemon
 */

#ifndef IPFS_NODE_H
#define IPFS_NODE_H

#include <stdint.h>
#include <stddef.h>
#include "chunk.h"
#include "storage.h"
#include "network.h"
#include "http.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Node configuration */
typedef struct {
    char data_dir[256];         /* ~/.my_ipfs or custom path */
    uint16_t udp_port;          /* P2P UDP port (default: 4001) */
    uint16_t tcp_port;          /* P2P TCP port (default: 4002) */
    uint16_t http_port;         /* HTTP port (default: 8080) */
    char http_bind[64];         /* HTTP bind address (default: "127.0.0.1") */
    char bootstrap_peers[8][64];/* Bootstrap peer addresses */
    size_t bootstrap_count;
    int log_level;              /* 0=quiet, 1=error, 2=warn, 3=info, 4=debug */
} ipfs_node_config_t;

/* Opaque node type */
typedef struct ipfs_node ipfs_node_t;

/* Default configuration */
void ipfs_node_config_default(ipfs_node_config_t* config);

/* Create node with configuration */
int ipfs_node_create(ipfs_node_t** node, const ipfs_node_config_t* config);

/* Destroy node */
void ipfs_node_destroy(ipfs_node_t* node);

/* Start all subsystems (network, HTTP, etc.) */
int ipfs_node_start(ipfs_node_t* node);

/* Stop all subsystems */
void ipfs_node_stop(ipfs_node_t* node);

/* Run main event loop (blocking) */
int ipfs_node_run(ipfs_node_t* node);

/* Add a file to the node (chunk, hash, store) */
int ipfs_node_add(ipfs_node_t* node, const char* filepath, uint8_t* cid);

/* Get a file by CID (fetch from network if needed, return to caller) */
int ipfs_node_get(ipfs_node_t* node, const uint8_t* cid, const char* output_path);

/* Get block data by hash (raw access) */
int ipfs_node_get_block(ipfs_node_t* node, const uint8_t* hash, 
                        uint8_t** data, size_t* len);

/* Pin a CID (keep locally) */
int ipfs_node_pin(ipfs_node_t* node, const uint8_t* cid);

/* Unpin a CID */
int ipfs_node_unpin(ipfs_node_t* node, const uint8_t* cid);

/* Get node status */
typedef struct {
    uint8_t node_id[IPFS_NODE_ID_SIZE];
    size_t peer_count;
    uint64_t storage_used;
    uint64_t storage_blocks;
    int http_running;
    uint16_t http_port;
} ipfs_node_status_t;

int ipfs_node_status(ipfs_node_t* node, ipfs_node_status_t* status);

/* Get the HTTP base URL */
const char* ipfs_node_get_http_url(ipfs_node_t* node);

/* Set up signal handlers for graceful shutdown */
void ipfs_node_set_signal_handler(void);

/* Request node to stop (safe to call from signal handler) */
void ipfs_node_request_stop(ipfs_node_t* node);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_NODE_H */
