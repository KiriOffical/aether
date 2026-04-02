/**
 * IPFS HTTP Sidecar
 * Embedded web server for browser access
 */

#ifndef IPFS_HTTP_H
#define IPFS_HTTP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque HTTP server type */
typedef struct ipfs_http_server ipfs_http_server_t;

/* Forward declarations */
typedef struct ipfs_network ipfs_network_t;
typedef struct ipfs_storage ipfs_storage_t;

/* Server configuration */
typedef struct {
    const char* bind_addr;    /* e.g., "127.0.0.1" */
    uint16_t port;            /* e.g., 8080 */
    int max_connections;
    int request_timeout_ms;
} ipfs_http_config_t;

/* Create HTTP server */
int ipfs_http_server_create(ipfs_http_server_t** server, 
                            const ipfs_http_config_t* config,
                            ipfs_network_t* net,
                            ipfs_storage_t* storage);

/* Destroy HTTP server */
void ipfs_http_server_destroy(ipfs_http_server_t* server);

/* Start server in background thread */
int ipfs_http_server_start(ipfs_http_server_t* server);

/* Stop server */
void ipfs_http_server_stop(ipfs_http_server_t* server);

/* Check if server is running */
int ipfs_http_server_is_running(ipfs_http_server_t* server);

/* Get actual bound port (useful if port 0 was specified) */
uint16_t ipfs_http_server_get_port(ipfs_http_server_t* server);

/* Set custom handler for /ipfs/<cid> requests */
typedef int (*ipfs_http_handler_t)(const char* path, const char* query,
                                   void* user_data,
                                   void (*response_cb)(const void* data, size_t len, void* cb_data),
                                   void* cb_data);

int ipfs_http_set_handler(ipfs_http_server_t* server, const char* path_prefix,
                          ipfs_http_handler_t handler, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_HTTP_H */
