/**
 * A.E.T.H.E.R. Core - Public API
 * Asynchronous Edge-Tolerant Holographic Execution Runtime
 */

#ifndef AETHER_H
#define AETHER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* Version */
#define AETHER_VERSION_MAJOR 0
#define AETHER_VERSION_MINOR 1
#define AETHER_VERSION_PATCH 0
#define AETHER_VERSION_STRING "0.1.0"

/* Constants */
#define AETHER_NODE_ID_SIZE     32
#define AETHER_PUBLIC_KEY_SIZE  32
#define AETHER_SIGNATURE_SIZE   64
#define AETHER_HASH_SIZE        32
#define AETHER_DEFAULT_PORT     7821
#define AETHER_MAX_MESSAGE_SIZE (64 * 1024 * 1024)
#define AETHER_MAX_PEERS        10000
#define AETHER_K_BUCKET_SIZE    20

/* Opaque types */
typedef struct aether_node aether_node_t;
typedef struct aether_config aether_config_t;
typedef struct aether_peer aether_peer_t;

/* Node ID type */
typedef uint8_t aether_node_id_t[AETHER_NODE_ID_SIZE];

/* Error codes */
typedef enum {
    AETHER_OK = 0,
    AETHER_ERR_IO = -1,
    AETHER_ERR_INVALID_ARG = -2,
    AETHER_ERR_NO_MEMORY = -3,
    AETHER_ERR_CONNECTION_CLOSED = -4,
    AETHER_ERR_HANDSHAKE_FAILED = -5,
    AETHER_ERR_INVALID_MESSAGE = -6,
    AETHER_ERR_INVALID_SIGNATURE = -7,
    AETHER_ERR_MESSAGE_TOO_LARGE = -8,
    AETHER_ERR_NOT_FOUND = -9,
    AETHER_ERR_RATE_LIMITED = -10,
    AETHER_ERR_BLACKLISTED = -11,
    AETHER_ERR_VERSION_MISMATCH = -12,
    AETHER_ERR_TIMEOUT = -13,
    AETHER_ERR_CRYPTO = -14,
    AETHER_ERR_PROTOCOL = -15
} aether_error_t;

/* Configuration */
struct aether_config {
    char identity_path[256];
    char data_dir[256];
    uint16_t listen_port;
    char public_addr[64];
    size_t max_connections;
    size_t max_message_size;
    char bootstrap_nodes[8][256];
    size_t bootstrap_count;
    int log_level;
};

/* Callback types */
typedef void (*aether_message_cb)(aether_node_t* node, const uint8_t* from_id, 
                                   const void* data, size_t len, void* user_data);
typedef void (*aether_peer_cb)(aether_node_t* node, const uint8_t* peer_id, 
                                int connected, void* user_data);
typedef void (*aether_log_cb)(int level, const char* message, void* user_data);

/* API Functions */

/* Lifecycle */
aether_error_t aether_init(void);
void aether_cleanup(void);

/* Node creation/destruction */
aether_node_t* aether_node_create(const aether_config_t* config);
void aether_node_destroy(aether_node_t* node);

/* Node control */
aether_error_t aether_node_start(aether_node_t* node);
aether_error_t aether_node_stop(aether_node_t* node);
aether_error_t aether_node_run(aether_node_t* node);  /* Blocking */

/* Node info */
aether_error_t aether_node_get_id(aether_node_t* node, aether_node_id_t* out_id);
uint16_t aether_node_get_port(aether_node_t* node);
size_t aether_node_get_peer_count(aether_node_t* node);

/* Messaging */
aether_error_t aether_node_send(aether_node_t* node, const aether_node_id_t* target_id,
                                 const void* data, size_t len);
aether_error_t aether_node_broadcast(aether_node_t* node, const void* data, size_t len);

/* Callbacks */
void aether_node_set_message_callback(aether_node_t* node, aether_message_cb cb, void* user_data);
void aether_node_set_peer_callback(aether_node_t* node, aether_peer_cb cb, void* user_data);
void aether_node_set_log_callback(aether_node_t* node, aether_log_cb cb, void* user_data);

/* DHT operations */
aether_error_t aether_dht_store(aether_node_t* node, const uint8_t* key, size_t key_len,
                                 const uint8_t* value, size_t value_len);
aether_error_t aether_dht_get(aether_node_t* node, const uint8_t* key, size_t key_len,
                               uint8_t* out_value, size_t* out_len);
aether_error_t aether_dht_find_node(aether_node_t* node, const aether_node_id_t* target,
                                     aether_node_id_t* results, size_t* count);

/* Utility */
const char* aether_strerror(aether_error_t err);
int aether_version_major(void);
int aether_version_minor(void);
int aether_version_patch(void);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_H */
