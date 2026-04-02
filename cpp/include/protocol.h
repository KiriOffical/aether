/**
 * Core Protocol
 */

#ifndef AETHER_PROTOCOL_H
#define AETHER_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "aether.h"
#include "dht.h"
#include "peer.h"
#include "framing.h"

#define PROTOCOL_PING_INTERVAL_SECS     30
#define PROTOCOL_PEX_INTERVAL_SECS      60
#define PROTOCOL_CLEANUP_INTERVAL_SECS  300

/* Protocol state */
struct aether_node {
    aether_config_t config;
    uint8_t node_id[32];
    uint8_t secret_key[64];
    dht_t dht;
    peer_manager_t peer_manager;
    int running;
    void* socket;  /* Platform-specific */
    
    /* Callbacks */
    aether_message_cb message_cb;
    aether_peer_cb peer_cb;
    aether_log_cb log_cb;
    void* user_data;
    
    /* Logging */
    int log_level;
};

/* Protocol functions */
int protocol_init(aether_node_t* node, const aether_config_t* config);
void protocol_free(aether_node_t* node);

int protocol_start(aether_node_t* node);
int protocol_stop(aether_node_t* node);
int protocol_run(aether_node_t* node);

/* Message handling */
int protocol_send(aether_node_t* node, const uint8_t* target_id, const uint8_t* data, size_t len);
int protocol_broadcast(aether_node_t* node, const uint8_t* data, size_t len);

/* Logging */
void protocol_log(aether_node_t* node, int level, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_PROTOCOL_H */
