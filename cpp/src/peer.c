/**
 * Peer Management Implementation
 */

#include "peer.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

int peer_manager_init(peer_manager_t* pm, const uint8_t* self_node_id, size_t max_connections) {
    pm->capacity = 256;
    pm->peers = (peer_t**)calloc(pm->capacity, sizeof(peer_t*));
    if (!pm->peers) return -1;
    
    pm->blacklist_capacity = 64;
    pm->blacklist = (uint8_t**)calloc(pm->blacklist_capacity, sizeof(uint8_t*));
    if (!pm->blacklist) {
        free(pm->peers);
        return -1;
    }
    
    pm->count = 0;
    pm->blacklist_count = 0;
    pm->max_connections = max_connections;
    memcpy(pm->self_node_id, self_node_id, 32);
    
    return 0;
}

void peer_manager_free(peer_manager_t* pm) {
    for (size_t i = 0; i < pm->count; i++) {
        free(pm->peers[i]);
    }
    free(pm->peers);
    
    for (size_t i = 0; i < pm->blacklist_count; i++) {
        free(pm->blacklist[i]);
    }
    free(pm->blacklist);
}

int peer_manager_add(peer_manager_t* pm, const peer_t* peer) {
    /* Check if exists */
    for (size_t i = 0; i < pm->count; i++) {
        if (memcmp(pm->peers[i]->node_id, peer->node_id, 32) == 0) {
            /* Update existing */
            memcpy(pm->peers[i], peer, sizeof(peer_t));
            return 0;
        }
    }
    
    /* Expand if needed */
    if (pm->count >= pm->capacity) {
        size_t new_cap = pm->capacity * 2;
        peer_t** new_peers = (peer_t**)realloc(pm->peers, new_cap * sizeof(peer_t*));
        if (!new_peers) return -1;
        pm->peers = new_peers;
        pm->capacity = new_cap;
    }
    
    /* Add new */
    peer_t* p = (peer_t*)malloc(sizeof(peer_t));
    if (!p) return -1;
    
    memcpy(p, peer, sizeof(peer_t));
    pm->peers[pm->count++] = p;
    return 0;
}

peer_t* peer_manager_get(peer_manager_t* pm, const uint8_t* node_id) {
    for (size_t i = 0; i < pm->count; i++) {
        if (memcmp(pm->peers[i]->node_id, node_id, 32) == 0) {
            return pm->peers[i];
        }
    }
    return NULL;
}

peer_t* peer_manager_get_by_addr(peer_manager_t* pm, const char* addr, uint16_t port) {
    for (size_t i = 0; i < pm->count; i++) {
        if (strcmp(pm->peers[i]->remote_addr, addr) == 0 && pm->peers[i]->remote_port == port) {
            return pm->peers[i];
        }
    }
    return NULL;
}

int peer_manager_remove(peer_manager_t* pm, const uint8_t* node_id) {
    for (size_t i = 0; i < pm->count; i++) {
        if (memcmp(pm->peers[i]->node_id, node_id, 32) == 0) {
            free(pm->peers[i]);
            pm->peers[i] = pm->peers[pm->count - 1];
            pm->count--;
            return 0;
        }
    }
    return -1;
}

void peer_manager_disconnect(peer_manager_t* pm, const uint8_t* node_id) {
    peer_t* p = peer_manager_get(pm, node_id);
    if (p) {
        p->state = PEER_DISCONNECTED;
    }
}

void peer_manager_blacklist(peer_manager_t* pm, const uint8_t* node_id) {
    /* Check if already blacklisted */
    for (size_t i = 0; i < pm->blacklist_count; i++) {
        if (memcmp(pm->blacklist[i], node_id, 32) == 0) {
            return;
        }
    }
    
    /* Expand if needed */
    if (pm->blacklist_count >= pm->blacklist_capacity) {
        size_t new_cap = pm->blacklist_capacity * 2;
        uint8_t** new_list = (uint8_t**)realloc(pm->blacklist, new_cap * sizeof(uint8_t*));
        if (!new_list) return;
        pm->blacklist = new_list;
        pm->blacklist_capacity = new_cap;
    }
    
    uint8_t* entry = (uint8_t*)malloc(32);
    if (!entry) return;
    
    memcpy(entry, node_id, 32);
    pm->blacklist[pm->blacklist_count++] = entry;
}

int peer_manager_is_blacklisted(peer_manager_t* pm, const uint8_t* node_id) {
    for (size_t i = 0; i < pm->blacklist_count; i++) {
        if (memcmp(pm->blacklist[i], node_id, 32) == 0) {
            return 1;
        }
    }
    return 0;
}

size_t peer_manager_active_count(peer_manager_t* pm) {
    size_t count = 0;
    for (size_t i = 0; i < pm->count; i++) {
        if (pm->peers[i]->state == PEER_CONNECTED) {
            count++;
        }
    }
    return count;
}

int peer_manager_can_accept(peer_manager_t* pm) {
    return peer_manager_active_count(pm) < pm->max_connections;
}

peer_t** peer_manager_get_active(peer_manager_t* pm, size_t* count) {
    size_t active = peer_manager_active_count(pm);
    peer_t** result = (peer_t**)malloc(active * sizeof(peer_t*));
    if (!result) {
        *count = 0;
        return NULL;
    }
    
    size_t idx = 0;
    for (size_t i = 0; i < pm->count && idx < active; i++) {
        if (pm->peers[i]->state == PEER_CONNECTED) {
            result[idx++] = pm->peers[i];
        }
    }
    
    *count = active;
    return result;
}

peer_t** peer_manager_get_random(peer_manager_t* pm, size_t limit, size_t* count) {
    peer_t** active;
    size_t active_count;
    
    active = peer_manager_get_active(pm, &active_count);
    if (!active || active_count == 0) {
        *count = 0;
        return NULL;
    }
    
    size_t result_count = (limit < active_count) ? limit : active_count;
    peer_t** result = (peer_t**)malloc(result_count * sizeof(peer_t*));
    if (!result) {
        free(active);
        *count = 0;
        return NULL;
    }
    
    /* Simple random selection */
    for (size_t i = 0; i < result_count; i++) {
        size_t idx = (size_t)rand() % active_count;
        result[i] = active[idx];
        
        /* Swap to avoid duplicates */
        peer_t* tmp = active[idx];
        active[idx] = active[active_count - 1];
        active[active_count - 1] = tmp;
        active_count--;
    }
    
    free(active);
    *count = result_count;
    return result;
}

peer_t** peer_manager_get_closest(peer_manager_t* pm, const uint8_t* target, size_t k, size_t* count) {
    /* Collect active peers with distances */
    typedef struct {
        peer_t* peer;
        uint8_t distance[32];
    } peer_dist_t;
    
    peer_dist_t* peers = (peer_dist_t*)malloc(pm->count * sizeof(peer_dist_t));
    if (!peers) {
        *count = 0;
        return NULL;
    }
    
    size_t total = 0;
    for (size_t i = 0; i < pm->count; i++) {
        if (pm->peers[i]->state == PEER_CONNECTED) {
            peers[total].peer = pm->peers[i];
            for (int j = 0; j < 32; j++) {
                peers[total].distance[j] = pm->peers[i]->node_id[j] ^ target[j];
            }
            total++;
        }
    }
    
    /* Sort by distance */
    for (size_t i = 0; i < total - 1; i++) {
        for (size_t j = i + 1; j < total; j++) {
            if (memcmp(peers[i].distance, peers[j].distance, 32) > 0) {
                peer_dist_t tmp = peers[i];
                peers[i] = peers[j];
                peers[j] = tmp;
            }
        }
    }
    
    /* Return top k */
    *count = (k < total) ? k : total;
    peer_t** result = (peer_t**)malloc(*count * sizeof(peer_t*));
    for (size_t i = 0; i < *count; i++) {
        result[i] = peers[i].peer;
    }
    
    free(peers);
    return result;
}

void peer_manager_evict_stale(peer_manager_t* pm) {
    uint64_t now = (uint64_t)time(NULL);
    
    for (size_t i = 0; i < pm->count; ) {
        peer_t* p = pm->peers[i];
        if (p->state != PEER_CONNECTED && 
            now - p->last_activity > PEER_TTL_SECS) {
            free(p);
            pm->peers[i] = pm->peers[pm->count - 1];
            pm->count--;
        } else {
            i++;
        }
    }
}

void peer_manager_update_latency(peer_manager_t* pm, const uint8_t* node_id, uint64_t latency_ns) {
    peer_t* p = peer_manager_get(pm, node_id);
    if (p) {
        p->latency_ns = latency_ns;
    }
}

void peer_manager_adjust_trust(peer_manager_t* pm, const uint8_t* node_id, int delta) {
    peer_t* p = peer_manager_get(pm, node_id);
    if (p) {
        int new_score = (int)p->trust_score + delta;
        p->trust_score = (uint8_t)(new_score < 0 ? 0 : (new_score > 100 ? 100 : new_score));
    }
}
