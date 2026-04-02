/**
 * DHT Implementation
 */

#include "dht.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Internal helper for XOR distance */
static void compute_distance(const uint8_t* a, const uint8_t* b, uint8_t* out) {
    for (int i = 0; i < 32; i++) {
        out[i] = a[i] ^ b[i];
    }
}

static int compare_distance(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 32; i++) {
        if (a[i] != b[i]) {
            return (a[i] < b[i]) ? -1 : 1;
        }
    }
    return 0;
}

static int get_bucket_index(const uint8_t* distance) {
    for (int i = 0; i < 256; i++) {
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);
        if (distance[byte_idx] & (1 << bit_idx)) {
            return i;
        }
    }
    return 0;
}

int dht_init(dht_t* dht, const dht_node_id_t* node_id, const dht_endpoint_t* endpoint) {
    memset(dht, 0, sizeof(dht_t));
    memcpy(dht->routing_table.node_id, *node_id, 32);
    
    if (endpoint) {
        memcpy(dht->routing_table.self_endpoint.address, endpoint->address, 16);
        dht->routing_table.self_endpoint.port = endpoint->port;
        dht->routing_table.self_endpoint.is_ipv6 = endpoint->is_ipv6;
    }
    
    /* Initialize storage */
    dht->storage.capacity = 1024;
    dht->storage.values = (dht_value_t**)calloc(dht->storage.capacity, sizeof(dht_value_t*));
    if (!dht->storage.values) {
        return -1;
    }
    dht->storage.count = 0;
    dht->storage.max_values = DHT_MAX_VALUES;
    
    return 0;
}

void dht_free(dht_t* dht) {
    for (size_t i = 0; i < dht->storage.count; i++) {
        if (dht->storage.values[i]) {
            free(dht->storage.values[i]->value);
            free(dht->storage.values[i]);
        }
    }
    free(dht->storage.values);
}

int dht_add_node(dht_t* dht, const dht_node_id_t* node_id, const dht_endpoint_t* endpoint) {
    /* Don't add self */
    if (memcmp(node_id, dht->routing_table.node_id, 32) == 0) {
        return 0;
    }
    
    /* Compute distance and find bucket */
    uint8_t distance[32];
    compute_distance(dht->routing_table.node_id, *node_id, distance);
    int bucket_idx = get_bucket_index(distance);
    
    dht_bucket_t* bucket = &dht->routing_table.buckets[bucket_idx];
    
    /* Check if already exists */
    for (size_t i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->entries[i].node_id, node_id, 32) == 0) {
            bucket->entries[i].last_seen = (uint64_t)time(NULL);
            if (endpoint) {
                memcpy(bucket->entries[i].endpoint.address, endpoint->address, 16);
                bucket->entries[i].endpoint.port = endpoint->port;
                bucket->entries[i].endpoint.is_ipv6 = endpoint->is_ipv6;
            }
            return 0;
        }
    }
    
    /* Add if space */
    if (bucket->count < DHT_K_BUCKET_SIZE) {
        dht_bucket_entry_t* entry = &bucket->entries[bucket->count++];
        memcpy(entry->node_id, node_id, 32);
        if (endpoint) {
            memcpy(entry->endpoint.address, endpoint->address, 16);
            entry->endpoint.port = endpoint->port;
            entry->endpoint.is_ipv6 = endpoint->is_ipv6;
        } else {
            memset(entry->endpoint.address, 0, 16);
            entry->endpoint.port = 0;
            entry->endpoint.is_ipv6 = 0;
        }
        entry->last_seen = (uint64_t)time(NULL);
        entry->is_self = 0;
        return 0;
    }
    
    /* Bucket full - could implement eviction */
    return -1;
}

void dht_remove_node(dht_t* dht, const dht_node_id_t* node_id) {
    uint8_t distance[32];
    compute_distance(dht->routing_table.node_id, *node_id, distance);
    int bucket_idx = get_bucket_index(distance);
    
    dht_bucket_t* bucket = &dht->routing_table.buckets[bucket_idx];
    
    for (size_t i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->entries[i].node_id, node_id, 32) == 0) {
            /* Remove by swapping with last */
            bucket->entries[i] = bucket->entries[bucket->count - 1];
            bucket->count--;
            return;
        }
    }
}

int dht_find_closest(dht_t* dht, const dht_node_id_t* target, size_t k,
                     dht_node_id_t* results, dht_endpoint_t* endpoints, size_t* count) {
    /* Collect all nodes with distances */
    typedef struct {
        dht_bucket_entry_t* entry;
        uint8_t distance[32];
    } node_dist_t;
    
    node_dist_t* all_nodes = (node_dist_t*)malloc(DHT_ID_BITS * DHT_K_BUCKET_SIZE * sizeof(node_dist_t));
    if (!all_nodes) return -1;
    
    size_t total = 0;
    for (int i = 0; i < DHT_ID_BITS; i++) {
        dht_bucket_t* bucket = &dht->routing_table.buckets[i];
        for (size_t j = 0; j < bucket->count; j++) {
            all_nodes[total].entry = &bucket->entries[j];
            compute_distance(*target, bucket->entries[j].node_id, all_nodes[total].distance);
            total++;
        }
    }
    
    /* Sort by distance (simple bubble sort for now) */
    for (size_t i = 0; i < total - 1; i++) {
        for (size_t j = i + 1; j < total; j++) {
            if (compare_distance(all_nodes[i].distance, all_nodes[j].distance) > 0) {
                node_dist_t tmp = all_nodes[i];
                all_nodes[i] = all_nodes[j];
                all_nodes[j] = tmp;
            }
        }
    }
    
    /* Return top k */
    *count = (k < total) ? k : total;
    for (size_t i = 0; i < *count; i++) {
        memcpy(results[i], all_nodes[i].entry->node_id, 32);
        if (endpoints) {
            memcpy(endpoints[i].address, all_nodes[i].entry->endpoint.address, 16);
            endpoints[i].port = all_nodes[i].entry->endpoint.port;
            endpoints[i].is_ipv6 = all_nodes[i].entry->endpoint.is_ipv6;
        }
    }
    
    free(all_nodes);
    return 0;
}

int dht_store(dht_t* dht, const uint8_t* key, size_t key_len,
              const uint8_t* value, size_t value_len,
              const dht_node_id_t* publisher, const uint8_t* signature) {
    /* Check if we need to expand */
    if (dht->storage.count >= dht->storage.capacity) {
        size_t new_cap = dht->storage.capacity * 2;
        dht_value_t** new_vals = (dht_value_t**)realloc(dht->storage.values, new_cap * sizeof(dht_value_t*));
        if (!new_vals) return -1;
        dht->storage.values = new_vals;
        dht->storage.capacity = new_cap;
    }
    
    /* Check if key already exists */
    for (size_t i = 0; i < dht->storage.count; i++) {
        if (memcmp(dht->storage.values[i]->key, key, key_len) == 0) {
            /* Update existing */
            free(dht->storage.values[i]->value);
            dht->storage.values[i]->value = (uint8_t*)malloc(value_len);
            if (!dht->storage.values[i]->value) return -1;
            memcpy(dht->storage.values[i]->value, value, value_len);
            dht->storage.values[i]->value_len = value_len;
            dht->storage.values[i]->expires_at = (uint64_t)time(NULL) + DHT_VALUE_TTL_SECS;
            return 0;
        }
    }
    
    /* Create new value */
    dht_value_t* v = (dht_value_t*)calloc(1, sizeof(dht_value_t));
    if (!v) return -1;
    
    memcpy(v->key, key, key_len < 32 ? key_len : 32);
    v->value = (uint8_t*)malloc(value_len);
    if (!v->value) {
        free(v);
        return -1;
    }
    memcpy(v->value, value, value_len);
    v->value_len = value_len;
    memcpy(v->publisher, publisher, 32);
    v->created_at = (uint64_t)time(NULL);
    v->expires_at = v->created_at + DHT_VALUE_TTL_SECS;
    if (signature) memcpy(v->signature, signature, 64);
    
    dht->storage.values[dht->storage.count++] = v;
    return 0;
}

int dht_get(dht_t* dht, const uint8_t* key, size_t key_len,
            uint8_t* out_value, size_t* out_len) {
    uint64_t now = (uint64_t)time(NULL);
    
    for (size_t i = 0; i < dht->storage.count; i++) {
        dht_value_t* v = dht->storage.values[i];
        if (memcmp(v->key, key, key_len < 32 ? key_len : 32) == 0) {
            if (v->expires_at > now) {
                if (out_value && out_len) {
                    size_t copy_len = (*out_len < v->value_len) ? *out_len : v->value_len;
                    memcpy(out_value, v->value, copy_len);
                    *out_len = copy_len;
                }
                return 0;
            }
        }
    }
    return -1;  /* Not found */
}

void dht_cleanup(dht_t* dht) {
    uint64_t now = (uint64_t)time(NULL);
    
    for (size_t i = 0; i < dht->storage.count; ) {
        if (dht->storage.values[i]->expires_at <= now) {
            free(dht->storage.values[i]->value);
            free(dht->storage.values[i]);
            dht->storage.values[i] = dht->storage.values[dht->storage.count - 1];
            dht->storage.count--;
        } else {
            i++;
        }
    }
}

size_t dht_node_count(dht_t* dht) {
    size_t count = 0;
    for (int i = 0; i < DHT_ID_BITS; i++) {
        count += dht->routing_table.buckets[i].count;
    }
    return count;
}

size_t dht_value_count(dht_t* dht) {
    return dht->storage.count;
}

void dht_compute_distance(const dht_node_id_t* a, const dht_node_id_t* b, dht_node_id_t* out) {
    compute_distance(*a, *b, *out);
}

int dht_compare_distance(const dht_node_id_t* a, const dht_node_id_t* b) {
    return compare_distance(*a, *b);
}
