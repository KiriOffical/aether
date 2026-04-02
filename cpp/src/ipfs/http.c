/**
 * IPFS HTTP Sidecar
 * Embedded web server for browser access
 */

#include "ipfs/http.h"
#include "ipfs/network.h"
#include "ipfs/storage.h"
#include "ipfs/chunk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sodium.h>

/* Mongoose embedded web server */
#define MG_ENABLE_SOCKET 1
#define MG_ENABLE_LINES 1
#define MG_ENABLE_DIRLIST 0
#define MG_ENABLE_LOG 0

#include "mongoose.h"

#define MAX_HANDLERS 16
#define HTTP_BUFFER_SIZE (256 * 1024)  /* 256KB response buffer */

struct handler_entry {
    char path_prefix[128];
    ipfs_http_handler_t handler;
    void* user_data;
};

struct ipfs_http_server {
    struct mg_mgr mgr;
    struct mg_connection* nc;
    ipfs_network_t* network;
    ipfs_storage_t* storage;
    ipfs_http_config_t config;
    struct handler_entry handlers[MAX_HANDLERS];
    size_t handler_count;
    int running;
    pthread_t thread;
    uint16_t actual_port;
};

/* Thread function for HTTP server */
static void* http_server_thread(void* arg) {
    ipfs_http_server_t* server = (ipfs_http_server_t*)arg;
    
    while (server->running) {
        mg_mgr_poll(&server->mgr, 100);
    }
    
    return NULL;
}

/* Helper to extract string from mg_str */
static char* mg_str_to_c(const struct mg_str s) {
    char* result = malloc(s.len + 1);
    if (result) {
        memcpy(result, s.buf, s.len);
        result[s.len] = '\0';
    }
    return result;
}

/* HTTP event handler */
static void http_handler(struct mg_connection* c, int ev, void* ev_data) {
    ipfs_http_server_t* server = (ipfs_http_server_t*)c->fn_data;
    
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        
        /* Extract URI */
        char* uri = mg_str_to_c(hm->uri);
        if (!uri) {
            mg_http_reply(c, 500, "", "Internal Error\n");
            return;
        }
        
        /* Check if path matches any registered handler */
        int handled = 0;
        
        for (size_t i = 0; i < server->handler_count; i++) {
            struct handler_entry* h = &server->handlers[i];
            size_t prefix_len = strlen(h->path_prefix);
            
            if (strncmp(uri, h->path_prefix, prefix_len) == 0) {
                char query[256] = {0};
                
                /* Extract query string */
                if (hm->query.len > 0) {
                    size_t query_len = hm->query.len;
                    if (query_len >= sizeof(query)) {
                        query_len = sizeof(query) - 1;
                    }
                    memcpy(query, hm->query.buf, query_len);
                }
                
                /* Prepare response callback data */
                typedef struct {
                    struct mg_connection* conn;
                    int headers_sent;
                } resp_cb_data_t;
                
                resp_cb_data_t resp_data = {c, 0};
                
                /* Response callback for streaming */
                void response_callback(const void* data, size_t len, void* cb_data) {
                    resp_cb_data_t* r = (resp_cb_data_t*)cb_data;
                    if (!r->headers_sent) {
                        mg_printf(r->conn, 
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/octet-stream\r\n"
                            "Transfer-Encoding: chunked\r\n"
                            "\r\n");
                        r->headers_sent = 1;
                    }
                    if (data && len > 0) {
                        mg_http_write_chunk(r->conn, (const char*)data, len);
                    } else {
                        mg_http_write_chunk(r->conn, NULL, 0);  /* End chunks */
                    }
                }
                
                /* Call handler */
                int result = h->handler(uri, query, h->user_data, response_callback, &resp_data);
                
                if (result == 0) {
                    /* Handler completed successfully */
                    if (!resp_data.headers_sent) {
                        /* Empty response */
                        mg_http_reply(c, 200, "", "");
                    }
                    handled = 1;
                }
                
                break;
            }
        }
        
        if (!handled) {
            /* Default: 404 */
            mg_http_reply(c, 404, "", "Not Found\n");
        }
        
        free(uri);
    }
}

/* IPFS GET handler - streams blocks from P2P network */
static int ipfs_get_handler(const char* path, const char* query,
                            void* user_data,
                            void (*response_cb)(const void* data, size_t len, void* cb_data),
                            void* cb_data) {
    (void)query;  /* Unused in MVP */
    ipfs_http_server_t* server = (ipfs_http_server_t*)user_data;
    
    /* Parse path: /ipfs/<cid> */
    const char* cid_start = path;
    
    /* Skip /ipfs/ prefix */
    if (strncmp(path, "/ipfs/", 6) == 0) {
        cid_start = path + 6;
    } else if (strncmp(path, "/ipfs", 5) == 0) {
        cid_start = path + 5;
    }
    
    /* Skip leading slash */
    while (*cid_start == '/') cid_start++;
    
    /* Parse CID (hex encoded) */
    if (strlen(cid_start) < IPFS_CID_SIZE * 2) {
        return -1;
    }
    
    uint8_t cid[IPFS_CID_SIZE];
    for (int i = 0; i < IPFS_CID_SIZE; i++) {
        int high = 0, low = 0;
        char c = cid_start[i * 2];
        if (c >= '0' && c <= '9') high = c - '0';
        else if (c >= 'a' && c <= 'f') high = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') high = c - 'A' + 10;
        else return -1;
        
        c = cid_start[i * 2 + 1];
        if (c >= '0' && c <= '9') low = c - '0';
        else if (c >= 'a' && c <= 'f') low = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') low = c - 'A' + 10;
        else return -1;
        
        cid[i] = (uint8_t)((high << 4) | low);
    }
    
    /* Load manifest from local storage */
    ipfs_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    
    int has_manifest = (ipfs_storage_get_manifest(server->storage, cid, &manifest) == 0);
    
    if (!has_manifest) {
        /* For MVP, assume single block if no manifest */
        manifest.block_count = 1;
        manifest.entries = calloc(1, sizeof(ipfs_manifest_entry_t));
        if (!manifest.entries) {
            return -1;
        }
        memcpy(manifest.entries[0].block_hash, cid, IPFS_HASH_SIZE);
        manifest.entries[0].block_index = 0;
        manifest.entries[0].block_size = IPFS_CHUNK_SIZE;
    }
    
    /* Stream blocks */
    uint8_t* block_data = NULL;
    size_t block_len = 0;
    
    for (uint32_t i = 0; i < manifest.block_count; i++) {
        uint8_t* hash = manifest.entries[i].block_hash;
        
        /* Try local storage first */
        if (ipfs_storage_get(server->storage, hash, &block_data, &block_len) == 0) {
            /* Found locally */
            response_cb(block_data, block_len, cb_data);
            free(block_data);
            block_data = NULL;
        } else {
            /* Need to fetch from P2P network */
            /* For MVP, return error if not local */
            /* Production would: find_value, get_block from peer, verify, store, stream */
            if (!has_manifest) {
                free(manifest.entries);
            }
            return -1;
        }
    }
    
    if (!has_manifest) {
        free(manifest.entries);
    }
    
    /* Signal end of stream */
    response_cb(NULL, 0, cb_data);
    return 0;
}

int ipfs_http_server_create(ipfs_http_server_t** server,
                            const ipfs_http_config_t* config,
                            ipfs_network_t* net,
                            ipfs_storage_t* storage) {
    if (!server || !config) {
        return -1;
    }
    
    *server = calloc(1, sizeof(ipfs_http_server_t));
    if (!*server) {
        return -1;
    }
    
    ipfs_http_server_t* s = *server;
    
    s->network = net;
    s->storage = storage;
    s->config = *config;
    s->handler_count = 0;
    s->running = 0;
    s->actual_port = config->port;
    
    /* Initialize mongoose */
    mg_mgr_init(&s->mgr);
    
    /* Register default IPFS handler */
    ipfs_http_set_handler(s, "/ipfs", ipfs_get_handler, s);
    
    return 0;
}

void ipfs_http_server_destroy(ipfs_http_server_t* server) {
    if (!server) {
        return;
    }
    
    ipfs_http_server_stop(server);
    mg_mgr_free(&server->mgr);
    free(server);
}

int ipfs_http_server_start(ipfs_http_server_t* server) {
    if (!server || server->running) {
        return -1;
    }
    
    /* Create listening address */
    char addr[128];
    snprintf(addr, sizeof(addr), "http://%s:%d", 
             server->config.bind_addr ? server->config.bind_addr : "127.0.0.1",
             server->config.port);
    
    /* Create HTTP connection */
    server->nc = mg_http_listen(&server->mgr, addr, http_handler, server);
    if (!server->nc) {
        fprintf(stderr, "Failed to start HTTP server on %s\n", addr);
        return -1;
    }
    
    /* Get actual port - for mongoose, we need to extract from the URL or use config */
    server->actual_port = server->config.port;
    
    server->running = 1;
    
    /* Start server thread */
    if (pthread_create(&server->thread, NULL, http_server_thread, server) != 0) {
        server->running = 0;
        return -1;
    }
    
    return 0;
}

void ipfs_http_server_stop(ipfs_http_server_t* server) {
    if (!server || !server->running) {
        return;
    }
    
    server->running = 0;
    
    /* Wait for thread to finish */
    pthread_join(server->thread, NULL);
    
    /* Close connection */
    if (server->nc) {
        server->nc->is_closing = 1;
        server->nc = NULL;
    }
    
    /* Final poll to process close */
    mg_mgr_poll(&server->mgr, 10);
}

int ipfs_http_server_is_running(ipfs_http_server_t* server) {
    return server ? server->running : 0;
}

uint16_t ipfs_http_server_get_port(ipfs_http_server_t* server) {
    return server ? server->actual_port : 0;
}

int ipfs_http_set_handler(ipfs_http_server_t* server, const char* path_prefix,
                          ipfs_http_handler_t handler, void* user_data) {
    if (!server || !path_prefix || !handler) {
        return -1;
    }
    
    if (server->handler_count >= MAX_HANDLERS) {
        return -1;
    }
    
    struct handler_entry* h = &server->handlers[server->handler_count];
    strncpy(h->path_prefix, path_prefix, sizeof(h->path_prefix) - 1);
    h->handler = handler;
    h->user_data = user_data;
    server->handler_count++;
    
    return 0;
}
