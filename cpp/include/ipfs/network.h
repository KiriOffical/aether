/**
 * IPFS P2P Network
 * Simplified Kademlia DHT over UDP
 */

#ifndef IPFS_NETWORK_H
#define IPFS_NETWORK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IPFS_NODE_ID_SIZE     32
#define IPFS_K_BUCKET_SIZE    20
#define IPFS_DEFAULT_UDP_PORT 4001
#define IPFS_DEFAULT_TCP_PORT 4002
#define IPFS_RPC_TIMEOUT_MS   5000
#define IPFS_MAX_PEERS        1000

/* RPC Message types */
typedef enum {
    IPFS_RPC_PING = 1,
    IPFS_RPC_PONG = 2,
    IPFS_RPC_FIND_NODE = 3,
    IPFS_RPC_FIND_VALUE = 4,
    IPFS_RPC_PROVIDE = 5,
    IPFS_RPC_GET_BLOCK = 6,
    IPFS_RPC_HELO = 7,        /* Local broadcast discovery */
    IPFS_RPC_HELO_ACK = 8     /* Broadcast response */
} ipfs_rpc_type_t;

/* Node endpoint */
typedef struct {
    char addr[64];          /* IP address string */
    char public_addr[64];   /* Public IP (for NAT traversal) */
    uint16_t udp_port;
    uint16_t tcp_port;
    uint16_t public_udp_port;  /* Public port (for NAT traversal) */
    uint16_t public_tcp_port;  /* Public port (for NAT traversal) */
    int is_public_valid;    /* True if public address is valid */
} ipfs_endpoint_t;

/* Peer entry in routing table */
typedef struct {
    uint8_t node_id[IPFS_NODE_ID_SIZE];
    ipfs_endpoint_t endpoint;
    uint64_t last_seen;
    int is_alive;
} ipfs_peer_t;

/* K-bucket for routing table */
typedef struct {
    ipfs_peer_t peers[IPFS_K_BUCKET_SIZE];
    size_t count;
} ipfs_kbucket_t;

/* Routing table */
typedef struct {
    uint8_t self_id[IPFS_NODE_ID_SIZE];
    ipfs_kbucket_t buckets[IPFS_NODE_ID_SIZE];
    size_t total_peers;
} ipfs_routing_table_t;

/* Opaque network type */
typedef struct ipfs_network ipfs_network_t;

/* Initialize network with random node ID and STUN discovery */
int ipfs_network_init(ipfs_network_t** net, uint16_t udp_port, uint16_t tcp_port);

/* Shutdown network */
void ipfs_network_shutdown(ipfs_network_t* net);

/* Get local node ID */
void ipfs_network_get_id(ipfs_network_t* net, uint8_t* node_id);

/* Get public IP (discovered via STUN) */
int ipfs_network_get_public_ip(ipfs_network_t* net, char* ip, size_t ip_len, 
                                uint16_t* udp_port, uint16_t* tcp_port);

/* Send local broadcast discovery packet */
int ipfs_network_broadcast_helo(ipfs_network_t* net);

/* Enable/disable broadcast listening */
void ipfs_network_set_broadcast(ipfs_network_t* net, int enabled);

/* Get routing table */
ipfs_routing_table_t* ipfs_network_get_routing_table(ipfs_network_t* net);

/* Bootstrap: connect to known peers */
int ipfs_network_bootstrap(ipfs_network_t* net, const ipfs_endpoint_t* peers, size_t count);

/* Find closest nodes to a target ID */
int ipfs_network_find_node(ipfs_network_t* net, const uint8_t* target_id,
                           ipfs_endpoint_t* results, size_t* count);

/* Find value (block) in DHT */
int ipfs_network_find_value(ipfs_network_t* net, const uint8_t* hash,
                            ipfs_endpoint_t* providers, size_t* count);

/* Announce that we have a block */
int ipfs_network_provide(ipfs_network_t* net, const uint8_t* hash);

/* Request a block via TCP from a peer */
int ipfs_network_get_block(ipfs_network_t* net, const ipfs_endpoint_t* peer,
                           const uint8_t* hash, uint8_t** data, size_t* len);

/* Run network event loop (non-blocking, call repeatedly) */
int ipfs_network_poll(ipfs_network_t* net, int timeout_ms);

/* Add a peer to routing table */
int ipfs_network_add_peer(ipfs_network_t* net, const uint8_t* node_id, 
                          const ipfs_endpoint_t* endpoint);

/* Remove a peer */
void ipfs_network_remove_peer(ipfs_network_t* net, const uint8_t* node_id);

/* Get peer count */
size_t ipfs_network_peer_count(ipfs_network_t* net);

#ifdef __cplusplus
}
#endif

#endif /* IPFS_NETWORK_H */
