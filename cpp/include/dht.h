/**
 * Distributed Hash Table (DHT)
 * Kademlia-style routing
 */

#ifndef AETHER_DHT_H
#define AETHER_DHT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DHT_K_BUCKET_SIZE    20
#define DHT_ID_BITS          256
#define DHT_MAX_VALUES       100000
#define DHT_VALUE_TTL_SECS   (24 * 60 * 60)

/* Node ID type */
typedef uint8_t dht_node_id_t[32];

/* Endpoint */
typedef struct {
    uint8_t address[16];
    uint16_t port;
    int is_ipv6;
} dht_endpoint_t;

/* Bucket entry */
typedef struct {
    dht_node_id_t node_id;
    dht_endpoint_t endpoint;
    uint64_t last_seen;
    int is_self;
} dht_bucket_entry_t;

/* K-bucket */
typedef struct {
    dht_bucket_entry_t entries[DHT_K_BUCKET_SIZE];
    size_t count;
} dht_bucket_t;

/* Stored value */
typedef struct {
    uint8_t key[32];
    uint8_t* value;
    size_t value_len;
    uint8_t publisher[32];
    uint64_t created_at;
    uint64_t expires_at;
    uint8_t signature[64];
} dht_value_t;

/* Routing table */
typedef struct {
    dht_node_id_t node_id;
    dht_bucket_t buckets[DHT_ID_BITS];
    dht_endpoint_t self_endpoint;
} dht_routing_table_t;

/* DHT storage */
typedef struct {
    dht_value_t** values;
    size_t count;
    size_t capacity;
    size_t max_values;
} dht_storage_t;

/* Main DHT structure */
typedef struct {
    dht_routing_table_t routing_table;
    dht_storage_t storage;
} dht_t;

/* Lifecycle */
int dht_init(dht_t* dht, const dht_node_id_t* node_id, const dht_endpoint_t* endpoint);
void dht_free(dht_t* dht);

/* Routing table operations */
int dht_add_node(dht_t* dht, const dht_node_id_t* node_id, const dht_endpoint_t* endpoint);
void dht_remove_node(dht_t* dht, const dht_node_id_t* node_id);
int dht_find_closest(dht_t* dht, const dht_node_id_t* target, size_t k,
                     dht_node_id_t* results, dht_endpoint_t* endpoints, size_t* count);

/* Storage operations */
int dht_store(dht_t* dht, const uint8_t* key, size_t key_len,
              const uint8_t* value, size_t value_len,
              const dht_node_id_t* publisher, const uint8_t* signature);
int dht_get(dht_t* dht, const uint8_t* key, size_t key_len,
            uint8_t* out_value, size_t* out_len);

/* Maintenance */
void dht_cleanup(dht_t* dht);
size_t dht_node_count(dht_t* dht);
size_t dht_value_count(dht_t* dht);

/* Utility */
void dht_compute_distance(const dht_node_id_t* a, const dht_node_id_t* b, dht_node_id_t* out);
int dht_compare_distance(const dht_node_id_t* a, const dht_node_id_t* b);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_DHT_H */
