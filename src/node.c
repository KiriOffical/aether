/**
 * A.E.T.H.E.R. Node Daemon
 * Main entry point
 */

#include "../include/aether.h"
#include "../include/protocol.h"
#include "../include/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void print_banner(void) {
    printf("=================================================\n");
    printf("     A.E.T.H.E.R. Node - Starting\n");
    printf("  Asynchronous Edge-Tolerant Holographic\n");
    printf("       Execution Runtime v%s\n", AETHER_VERSION_STRING);
    printf("=================================================\n\n");
}

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -c, --config <file>   Configuration file\n");
    printf("  -p, --port <port>     Listening port (default: 7821)\n");
    printf("  -d, --datadir <dir>   Data directory\n");
    printf("  -v, --verbose         Verbose logging\n");
    printf("  -h, --help            Show this help\n");
}

int main(int argc, char* argv[]) {
    aether_config_t config;
    memset(&config, 0, sizeof(config));
    
    /* Defaults */
    config.listen_port = AETHER_DEFAULT_PORT;
    config.max_connections = AETHER_MAX_PEERS;
    config.max_message_size = AETHER_MAX_MESSAGE_SIZE;
    config.log_level = 2;  /* INFO */
    strcpy(config.data_dir, "aether_data");
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                strncpy(config.identity_path, argv[++i], sizeof(config.identity_path) - 1);
            }
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                config.listen_port = (uint16_t)atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--datadir") == 0) {
            if (i + 1 < argc) {
                strncpy(config.data_dir, argv[++i], sizeof(config.data_dir) - 1);
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            config.log_level = 3;  /* DEBUG */
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    print_banner();
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    
    /* Create data directory */
#ifdef _WIN32
    CreateDirectoryA(config.data_dir, NULL);
#else
    mkdir(config.data_dir, 0755);
#endif
    
    /* Initialize library */
    if (aether_init() != AETHER_OK) {
        fprintf(stderr, "Failed to initialize A.E.T.H.E.R.\n");
        return 1;
    }
    
    /* Create node */
    aether_node_t* node = aether_node_create(&config);
    if (!node) {
        fprintf(stderr, "Failed to create node\n");
        aether_cleanup();
        return 1;
    }
    
    printf("Configuration:\n");
    printf("  Listen port:    %u\n", config.listen_port);
    printf("  Max connections: %zu\n", config.max_connections);
    printf("  Data directory: %s\n", config.data_dir);
    printf("  Log level:      %d\n", config.log_level);
    printf("\n");
    
    /* Start node */
    if (aether_node_start(node) != AETHER_OK) {
        fprintf(stderr, "Failed to start node\n");
        aether_node_destroy(node);
        aether_cleanup();
        return 1;
    }
    
    /* Get node info */
    aether_node_id_t node_id;
    if (aether_node_get_id(node, &node_id) == AETHER_OK) {
        printf("Node ID: %02x%02x", node_id[0], node_id[1]);
        for (int i = 2; i < 32; i++) {
            printf("%02x", node_id[i]);
        }
        printf("\n");
    }
    
    printf("Listening on port %u\n\n", aether_node_get_port(node));
    printf("Press Ctrl+C to stop...\n\n");
    
    /* Run main loop */
    aether_node_run(node);
    
    /* Shutdown */
    printf("\nShutting down...\n");
    aether_node_stop(node);
    aether_node_destroy(node);
    aether_cleanup();
    
    printf("A.E.T.H.E.R. Node stopped.\n");
    return 0;
}

/* Library implementation */

aether_error_t aether_init(void) {
    return crypto_init() == 0 ? AETHER_OK : AETHER_ERR_CRYPTO;
}

void aether_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

aether_node_t* aether_node_create(const aether_config_t* config) {
    aether_node_t* node = (aether_node_t*)calloc(1, sizeof(aether_node_t));
    if (!node) return NULL;
    
    if (protocol_init(node, config) != 0) {
        free(node);
        return NULL;
    }
    
    return node;
}

void aether_node_destroy(aether_node_t* node) {
    if (node) {
        protocol_free(node);
        free(node);
    }
}

aether_error_t aether_node_start(aether_node_t* node) {
    if (!node) return AETHER_ERR_INVALID_ARG;
    return protocol_start(node) == 0 ? AETHER_OK : AETHER_ERR_IO;
}

aether_error_t aether_node_stop(aether_node_t* node) {
    if (!node) return AETHER_ERR_INVALID_ARG;
    return protocol_stop(node) == 0 ? AETHER_OK : AETHER_ERR_IO;
}

aether_error_t aether_node_run(aether_node_t* node) {
    if (!node) return AETHER_ERR_INVALID_ARG;
    return protocol_run(node) == 0 ? AETHER_OK : AETHER_ERR_IO;
}

aether_error_t aether_node_get_id(aether_node_t* node, aether_node_id_t* out_id) {
    if (!node || !out_id) return AETHER_ERR_INVALID_ARG;
    memcpy(out_id, node->node_id, 32);
    return AETHER_OK;
}

uint16_t aether_node_get_port(aether_node_t* node) {
    return node ? node->config.listen_port : 0;
}

size_t aether_node_get_peer_count(aether_node_t* node) {
    return node ? peer_manager_active_count(&node->peer_manager) : 0;
}

aether_error_t aether_node_send(aether_node_t* node, const aether_node_id_t* target_id,
                                 const void* data, size_t len) {
    if (!node || !target_id || !data) return AETHER_ERR_INVALID_ARG;
    return protocol_send(node, *target_id, (const uint8_t*)data, len) == 0 ? AETHER_OK : AETHER_ERR_IO;
}

aether_error_t aether_node_broadcast(aether_node_t* node, const void* data, size_t len) {
    if (!node || !data) return AETHER_ERR_INVALID_ARG;
    return protocol_broadcast(node, (const uint8_t*)data, len) == 0 ? AETHER_OK : AETHER_ERR_IO;
}

void aether_node_set_message_callback(aether_node_t* node, aether_message_cb cb, void* user_data) {
    if (node) {
        node->message_cb = cb;
        node->user_data = user_data;
    }
}

void aether_node_set_peer_callback(aether_node_t* node, aether_peer_cb cb, void* user_data) {
    if (node) {
        node->peer_cb = cb;
        node->user_data = user_data;
    }
}

void aether_node_set_log_callback(aether_node_t* node, aether_log_cb cb, void* user_data) {
    if (node) {
        node->log_cb = cb;
        node->user_data = user_data;
    }
}

aether_error_t aether_dht_store(aether_node_t* node, const uint8_t* key, size_t key_len,
                                 const uint8_t* value, size_t value_len) {
    if (!node || !key || !value) return AETHER_ERR_INVALID_ARG;
    return dht_store(&node->dht, key, key_len, value, value_len, (const dht_node_id_t*)&node->node_id, NULL) == 0 ? AETHER_OK : AETHER_ERR_NO_MEMORY;
}

aether_error_t aether_dht_get(aether_node_t* node, const uint8_t* key, size_t key_len,
                               uint8_t* out_value, size_t* out_len) {
    if (!node || !key) return AETHER_ERR_INVALID_ARG;
    return dht_get(&node->dht, key, key_len, out_value, out_len) == 0 ? AETHER_OK : AETHER_ERR_NOT_FOUND;
}

aether_error_t aether_dht_find_node(aether_node_t* node, const aether_node_id_t* target,
                                     aether_node_id_t* results, size_t* count) {
    if (!node || !target || !results || !count) return AETHER_ERR_INVALID_ARG;
    
    dht_endpoint_t* endpoints = (dht_endpoint_t*)malloc(*count * sizeof(dht_endpoint_t));
    if (!endpoints) return AETHER_ERR_NO_MEMORY;
    
    int ret = dht_find_closest(&node->dht, target, *count, results, endpoints, count);
    free(endpoints);
    
    return ret == 0 ? AETHER_OK : AETHER_ERR_NOT_FOUND;
}

const char* aether_strerror(aether_error_t err) {
    switch (err) {
        case AETHER_OK: return "OK";
        case AETHER_ERR_IO: return "IO error";
        case AETHER_ERR_INVALID_ARG: return "Invalid argument";
        case AETHER_ERR_NO_MEMORY: return "Out of memory";
        case AETHER_ERR_CONNECTION_CLOSED: return "Connection closed";
        case AETHER_ERR_HANDSHAKE_FAILED: return "Handshake failed";
        case AETHER_ERR_INVALID_MESSAGE: return "Invalid message";
        case AETHER_ERR_INVALID_SIGNATURE: return "Invalid signature";
        case AETHER_ERR_MESSAGE_TOO_LARGE: return "Message too large";
        case AETHER_ERR_NOT_FOUND: return "Not found";
        case AETHER_ERR_RATE_LIMITED: return "Rate limited";
        case AETHER_ERR_BLACKLISTED: return "Blacklisted";
        case AETHER_ERR_VERSION_MISMATCH: return "Version mismatch";
        case AETHER_ERR_TIMEOUT: return "Timeout";
        case AETHER_ERR_CRYPTO: return "Crypto error";
        case AETHER_ERR_PROTOCOL: return "Protocol error";
        default: return "Unknown error";
    }
}

int aether_version_major(void) { return AETHER_VERSION_MAJOR; }
int aether_version_minor(void) { return AETHER_VERSION_MINOR; }
int aether_version_patch(void) { return AETHER_VERSION_PATCH; }
