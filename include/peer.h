/**
 * Peer Management
 */

#ifndef AETHER_PEER_H
#define AETHER_PEER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PEER_TTL_SECS        (24 * 60 * 60)
#define PEER_MAX_PEX_PEERS   50

/* Peer state */
typedef enum {
    PEER_CONNECTING = 0,
    PEER_HANDSHAKING = 1,
    PEER_CONNECTED = 2,
    PEER_DISCONNECTING = 3,
    PEER_DISCONNECTED = 4
} peer_state_t;

/* Peer info */
typedef struct {
    uint8_t node_id[32];
    char remote_addr[64];
    uint16_t remote_port;
    char listen_addr[64];
    uint16_t listen_port;
    peer_state_t state;
    uint64_t connected_at;
    uint64_t last_activity;
    uint64_t latency_ns;
    uint8_t trust_score;
    char protocols[8][64];
    size_t protocol_count;
} peer_t;

/* Peer manager */
typedef struct {
    peer_t** peers;
    size_t count;
    size_t capacity;
    size_t max_connections;
    uint8_t** blacklist;
    size_t blacklist_count;
    size_t blacklist_capacity;
    uint8_t self_node_id[32];
} peer_manager_t;

/* Lifecycle */
int peer_manager_init(peer_manager_t* pm, const uint8_t* self_node_id, size_t max_connections);
void peer_manager_free(peer_manager_t* pm);

/* Peer operations */
int peer_manager_add(peer_manager_t* pm, const peer_t* peer);
peer_t* peer_manager_get(peer_manager_t* pm, const uint8_t* node_id);
peer_t* peer_manager_get_by_addr(peer_manager_t* pm, const char* addr, uint16_t port);
int peer_manager_remove(peer_manager_t* pm, const uint8_t* node_id);
void peer_manager_disconnect(peer_manager_t* pm, const uint8_t* node_id);

/* Blacklist */
void peer_manager_blacklist(peer_manager_t* pm, const uint8_t* node_id);
int peer_manager_is_blacklisted(peer_manager_t* pm, const uint8_t* node_id);

/* Queries */
size_t peer_manager_active_count(peer_manager_t* pm);
int peer_manager_can_accept(peer_manager_t* pm);
peer_t** peer_manager_get_active(peer_manager_t* pm, size_t* count);
peer_t** peer_manager_get_random(peer_manager_t* pm, size_t limit, size_t* count);
peer_t** peer_manager_get_closest(peer_manager_t* pm, const uint8_t* target, size_t k, size_t* count);

/* Maintenance */
void peer_manager_evict_stale(peer_manager_t* pm);
void peer_manager_update_latency(peer_manager_t* pm, const uint8_t* node_id, uint64_t latency_ns);
void peer_manager_adjust_trust(peer_manager_t* pm, const uint8_t* node_id, int delta);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_PEER_H */
