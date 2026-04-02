/**
 * IPFS P2P Network
 * Simplified Kademlia DHT over UDP with NAT Hole Punching (STUN)
 */

#include "ipfs/network.h"
#include "ipfs/stun.h"
#include "ipfs/chunk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
#endif

#include <sodium.h>

#define IPFS_RPC_MAX_SIZE     4096
#define IPFS_PACKET_MAGIC     0x49504653  /* "IPFS" */
#define IPFS_PROTOCOL_VERSION 1

/* RPC Packet header */
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t rpc_type;
    uint8_t reserved[2];
    uint8_t sender_id[IPFS_NODE_ID_SIZE];
    uint32_t payload_len;
    /* Payload follows */
} ipfs_rpc_header_t;

/* FIND_VALUE response with provider info */
typedef struct {
    uint8_t provider_id[IPFS_NODE_ID_SIZE];
    char provider_addr[64];
    uint16_t tcp_port;
} ipfs_provider_t;

struct ipfs_network {
    uint8_t node_id[IPFS_NODE_ID_SIZE];
    int udp_socket;
    int tcp_socket;
    uint16_t udp_port;
    uint16_t tcp_port;
    ipfs_routing_table_t routing_table;
    int running;
    
    /* NAT traversal */
    char public_ip[64];
    uint16_t public_udp_port;
    uint16_t public_tcp_port;
    int public_ip_valid;
    
    /* Local discovery */
    int broadcast_enabled;
    uint64_t last_broadcast;
    uint32_t broadcast_interval_secs;

#ifdef _WIN32
    WSADATA wsa_data;
#endif
};

/* XOR distance comparison */
static int compare_distance(const uint8_t* a, const uint8_t* b, const uint8_t* target) {
    for (int i = 0; i < IPFS_NODE_ID_SIZE; i++) {
        uint8_t dist_a = a[i] ^ target[i];
        uint8_t dist_b = b[i] ^ target[i];
        if (dist_a != dist_b) {
            return (dist_a < dist_b) ? -1 : 1;
        }
    }
    return 0;
}

/* Compute XOR distance */
static void xor_distance(const uint8_t* a, const uint8_t* b, uint8_t* out) {
    for (int i = 0; i < IPFS_NODE_ID_SIZE; i++) {
        out[i] = a[i] ^ b[i];
    }
}

/* Get bucket index for a target ID (based on common prefix length) */
static int get_bucket_index(const uint8_t* self_id, const uint8_t* target_id) {
    for (int i = 0; i < IPFS_NODE_ID_SIZE; i++) {
        uint8_t xor = self_id[i] ^ target_id[i];
        if (xor != 0) {
            return i * 8 + __builtin_clz(xor ^ 0xFF) - 24;
        }
    }
    return IPFS_NODE_ID_SIZE * 8 - 1;
}

/* Initialize network */
int ipfs_network_init(ipfs_network_t** net, uint16_t udp_port, uint16_t tcp_port) {
    if (!net) {
        return -1;
    }
    
    *net = calloc(1, sizeof(ipfs_network_t));
    if (!*net) {
        return -1;
    }
    
    ipfs_network_t* n = *net;
    
#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &n->wsa_data) != 0) {
        free(n);
        *net = NULL;
        return -1;
    }
#endif
    
    /* Initialize libsodium */
    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
#ifdef _WIN32
        WSACleanup();
#endif
        free(n);
        *net = NULL;
        return -1;
    }
    
    /* Generate random node ID */
    randombytes_buf(n->node_id, IPFS_NODE_ID_SIZE);
    memcpy(n->routing_table.self_id, n->node_id, IPFS_NODE_ID_SIZE);
    
    n->udp_port = udp_port;
    n->tcp_port = tcp_port;
    
    /* Initialize NAT traversal fields */
    memset(n->public_ip, 0, sizeof(n->public_ip));
    n->public_udp_port = udp_port;
    n->public_tcp_port = tcp_port;
    n->public_ip_valid = 0;
    
    /* Initialize local discovery fields */
    n->broadcast_enabled = 1;  /* Enabled by default */
    n->last_broadcast = 0;
    n->broadcast_interval_secs = 60;  /* Broadcast every 60 seconds */

    /* Create UDP socket */
    n->udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (n->udp_socket < 0) {
        fprintf(stderr, "Failed to create UDP socket: %s\n", strerror(errno));
#ifdef _WIN32
        WSACleanup();
#endif
        free(n);
        *net = NULL;
        return -1;
    }
    
    /* Set UDP socket options */
    int reuse = 1;
    setsockopt(n->udp_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    
    /* Enable broadcast */
    int broadcast = 1;
    setsockopt(n->udp_socket, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast));

    /* Bind UDP socket */
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    udp_addr.sin_port = htons(udp_port);
    
    if (bind(n->udp_socket, (struct sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
        fprintf(stderr, "Failed to bind UDP socket: %s\n", strerror(errno));
#ifdef _WIN32
        closesocket(n->udp_socket);
        WSACleanup();
#else
        close(n->udp_socket);
#endif
        free(n);
        *net = NULL;
        return -1;
    }
    
    /* Create TCP socket */
    n->tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (n->tcp_socket < 0) {
        fprintf(stderr, "Failed to create TCP socket: %s\n", strerror(errno));
#ifdef _WIN32
        closesocket(n->udp_socket);
        WSACleanup();
#else
        close(n->udp_socket);
#endif
        free(n);
        *net = NULL;
        return -1;
    }
    
    /* Set TCP socket options */
    setsockopt(n->tcp_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    
    /* Bind TCP socket */
    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    tcp_addr.sin_port = htons(tcp_port);
    
    if (bind(n->tcp_socket, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) < 0) {
        fprintf(stderr, "Failed to bind TCP socket: %s\n", strerror(errno));
#ifdef _WIN32
        closesocket(n->tcp_socket);
        closesocket(n->udp_socket);
        WSACleanup();
#else
        close(n->tcp_socket);
        close(n->udp_socket);
#endif
        free(n);
        *net = NULL;
        return -1;
    }
    
    /* Listen on TCP */
    if (listen(n->tcp_socket, 10) < 0) {
        fprintf(stderr, "Failed to listen on TCP socket: %s\n", strerror(errno));
#ifdef _WIN32
        closesocket(n->tcp_socket);
        closesocket(n->udp_socket);
        WSACleanup();
#else
        close(n->tcp_socket);
        close(n->udp_socket);
#endif
        free(n);
        *net = NULL;
        return -1;
    }
    
    /* Set non-blocking */
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(n->udp_socket, FIONBIO, &mode);
    ioctlsocket(n->tcp_socket, FIONBIO, &mode);
#else
    int flags = fcntl(n->udp_socket, F_GETFL, 0);
    fcntl(n->udp_socket, F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(n->tcp_socket, F_GETFL, 0);
    fcntl(n->tcp_socket, F_SETFL, flags | O_NONBLOCK);
#endif
    
    /* Discover public IP via STUN (non-blocking, best effort) */
    fprintf(stderr, "[NAT] Discovering public IP via STUN...\n");
    uint16_t stun_port;
    if (stun_get_public_ip(n->public_ip, sizeof(n->public_ip), &stun_port) == 0) {
        n->public_ip_valid = 1;
        n->public_udp_port = stun_port;  /* STUN discovers the mapped UDP port */
        n->public_tcp_port = tcp_port;    /* Assume same for TCP */
        fprintf(stderr, "[NAT] Public IP: %s:%d (UDP), %s:%d (TCP)\n",
                n->public_ip, n->public_udp_port, n->public_ip, n->public_tcp_port);
    } else {
        fprintf(stderr, "[NAT] STUN failed, using local addresses only\n");
        n->public_ip_valid = 0;
    }

    n->running = 1;
    return 0;
}

void ipfs_network_shutdown(ipfs_network_t* net) {
    if (!net) {
        return;
    }
    
    net->running = 0;
    
#ifdef _WIN32
    closesocket(net->udp_socket);
    closesocket(net->tcp_socket);
    WSACleanup();
#else
    close(net->udp_socket);
    close(net->tcp_socket);
#endif
    
    free(net);
}

void ipfs_network_get_id(ipfs_network_t* net, uint8_t* node_id) {
    if (net && node_id) {
        memcpy(node_id, net->node_id, IPFS_NODE_ID_SIZE);
    }
}

int ipfs_network_get_public_ip(ipfs_network_t* net, char* ip, size_t ip_len, 
                                uint16_t* udp_port, uint16_t* tcp_port) {
    if (!net || !ip || ip_len < 16) {
        return -1;
    }
    
    if (!net->public_ip_valid) {
        return -1;
    }
    
    strncpy(ip, net->public_ip, ip_len - 1);
    ip[ip_len - 1] = '\0';
    
    if (udp_port) *udp_port = net->public_udp_port;
    if (tcp_port) *tcp_port = net->public_tcp_port;
    
    return 0;
}

/* Send HELLO broadcast packet */
int ipfs_network_broadcast_helo(ipfs_network_t* net) {
    if (!net || !net->broadcast_enabled) {
        return -1;
    }
    
    /* Build HELLO packet */
    uint8_t packet[256];
    ipfs_rpc_header_t* header = (ipfs_rpc_header_t*)packet;
    
    header->magic = htonl(IPFS_PACKET_MAGIC);
    header->version = IPFS_PROTOCOL_VERSION;
    header->rpc_type = IPFS_RPC_HELO;
    header->reserved[0] = 0;
    header->reserved[1] = 0;
    memcpy(header->sender_id, net->node_id, IPFS_NODE_ID_SIZE);
    header->payload_len = htonl(0);  /* No payload */
    
    /* Send to broadcast address */
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    broadcast_addr.sin_port = htons(net->udp_port);
    
    ssize_t sent = sendto(net->udp_socket, (char*)packet, sizeof(ipfs_rpc_header_t), 0,
                          (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    
    if (sent > 0) {
        net->last_broadcast = time(NULL);
        fprintf(stderr, "[DISCOVERY] HELLO broadcast sent\n");
        return 0;
    }
    
    return -1;
}

/* Enable/disable broadcast */
void ipfs_network_set_broadcast(ipfs_network_t* net, int enabled) {
    if (net) {
        net->broadcast_enabled = enabled;
    }
}

ipfs_routing_table_t* ipfs_network_get_routing_table(ipfs_network_t* net) {
    return net ? &net->routing_table : NULL;
}

int ipfs_network_add_peer(ipfs_network_t* net, const uint8_t* node_id,
                          const ipfs_endpoint_t* endpoint) {
    if (!net || !node_id || !endpoint) {
        return -1;
    }
    
    /* Don't add self */
    if (memcmp(node_id, net->node_id, IPFS_NODE_ID_SIZE) == 0) {
        return 0;
    }
    
    /* Find bucket */
    int bucket_idx = get_bucket_index(net->node_id, node_id);
    if (bucket_idx < 0 || bucket_idx >= IPFS_NODE_ID_SIZE * 8) {
        bucket_idx = 0;
    }
    
    ipfs_kbucket_t* bucket = &net->routing_table.buckets[bucket_idx];
    
    /* Check if already exists */
    for (size_t i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->peers[i].node_id, node_id, IPFS_NODE_ID_SIZE) == 0) {
            /* Update existing entry */
            memcpy(bucket->peers[i].endpoint.addr, endpoint->addr, 
                   sizeof(bucket->peers[i].endpoint.addr));
            bucket->peers[i].endpoint.udp_port = endpoint->udp_port;
            bucket->peers[i].endpoint.tcp_port = endpoint->tcp_port;
            bucket->peers[i].last_seen = time(NULL);
            bucket->peers[i].is_alive = 1;
            return 0;
        }
    }
    
    /* Add new entry if space */
    if (bucket->count < IPFS_K_BUCKET_SIZE) {
        ipfs_peer_t* peer = &bucket->peers[bucket->count++];
        memcpy(peer->node_id, node_id, IPFS_NODE_ID_SIZE);
        memcpy(peer->endpoint.addr, endpoint->addr, sizeof(peer->endpoint.addr));
        peer->endpoint.udp_port = endpoint->udp_port;
        peer->endpoint.tcp_port = endpoint->tcp_port;
        peer->last_seen = time(NULL);
        peer->is_alive = 1;
        net->routing_table.total_peers++;
        return 0;
    }
    
    /* Bucket full - could implement splitting or eviction, for now just drop oldest */
    return -1;
}

void ipfs_network_remove_peer(ipfs_network_t* net, const uint8_t* node_id) {
    if (!net || !node_id) {
        return;
    }
    
    /* Find and remove from bucket */
    for (int i = 0; i < IPFS_NODE_ID_SIZE * 8; i++) {
        ipfs_kbucket_t* bucket = &net->routing_table.buckets[i];
        for (size_t j = 0; j < bucket->count; j++) {
            if (memcmp(bucket->peers[j].node_id, node_id, IPFS_NODE_ID_SIZE) == 0) {
                /* Remove by swapping with last */
                if (j < bucket->count - 1) {
                    memcpy(&bucket->peers[j], &bucket->peers[bucket->count - 1], 
                           sizeof(ipfs_peer_t));
                }
                bucket->count--;
                net->routing_table.total_peers--;
                return;
            }
        }
    }
}

size_t ipfs_network_peer_count(ipfs_network_t* net) {
    return net ? net->routing_table.total_peers : 0;
}

/* Send RPC packet */
static int send_rpc(ipfs_network_t* net, const char* addr, uint16_t port,
                    uint8_t rpc_type, const void* payload, size_t payload_len) {
    if (!net || !addr) {
        return -1;
    }
    
    size_t packet_size = sizeof(ipfs_rpc_header_t) + payload_len;
    if (packet_size > IPFS_RPC_MAX_SIZE) {
        return -1;
    }
    
    uint8_t packet[IPFS_RPC_MAX_SIZE];
    ipfs_rpc_header_t* header = (ipfs_rpc_header_t*)packet;
    
    header->magic = htonl(IPFS_PACKET_MAGIC);
    header->version = IPFS_PROTOCOL_VERSION;
    header->rpc_type = rpc_type;
    header->reserved[0] = 0;
    header->reserved[1] = 0;
    memcpy(header->sender_id, net->node_id, IPFS_NODE_ID_SIZE);
    header->payload_len = htonl((uint32_t)payload_len);
    
    if (payload && payload_len > 0) {
        memcpy(packet + sizeof(ipfs_rpc_header_t), payload, payload_len);
    }
    
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, addr, &dest.sin_addr);
    
    ssize_t sent = sendto(net->udp_socket, (char*)packet, packet_size, 0,
                          (struct sockaddr*)&dest, sizeof(dest));
    
    return (sent == (ssize_t)packet_size) ? 0 : -1;
}

/* Receive and process RPC packet */
static int recv_rpc(ipfs_network_t* net, uint8_t* rpc_type, uint8_t* sender_id,
                    uint8_t* payload, size_t* payload_len, char* sender_addr,
                    uint16_t* sender_port) {
    if (!net) {
        return -1;
    }
    
    uint8_t packet[IPFS_RPC_MAX_SIZE];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    ssize_t received = recvfrom(net->udp_socket, (char*)packet, sizeof(packet), 0,
                                 (struct sockaddr*)&from, &from_len);
    
    if (received < 0) {
        return -1;
    }
    
    if ((size_t)received < sizeof(ipfs_rpc_header_t)) {
        return -1;
    }
    
    ipfs_rpc_header_t* header = (ipfs_rpc_header_t*)packet;
    
    /* Validate */
    if (ntohl(header->magic) != IPFS_PACKET_MAGIC) {
        return -1;
    }
    
    if (header->version != IPFS_PROTOCOL_VERSION) {
        return -1;
    }
    
    uint32_t payload_size = ntohl(header->payload_len);
    if (sizeof(ipfs_rpc_header_t) + payload_size > (size_t)received) {
        return -1;
    }
    
    /* Extract data */
    *rpc_type = header->rpc_type;
    memcpy(sender_id, header->sender_id, IPFS_NODE_ID_SIZE);
    
    if (payload && payload_len && payload_size > 0) {
        size_t copy_len = (payload_size < *payload_len) ? payload_size : *payload_len;
        memcpy(payload, packet + sizeof(ipfs_rpc_header_t), copy_len);
        *payload_len = copy_len;
    }
    
    /* Extract sender info */
    if (sender_addr) {
        inet_ntop(AF_INET, &from.sin_addr, sender_addr, 64);
    }
    if (sender_port) {
        *sender_port = ntohs(from.sin_port);
    }
    
    return 0;
}

/* Send PING to a peer */
static int send_ping(ipfs_network_t* net, const char* addr, uint16_t port) {
    return send_rpc(net, addr, port, IPFS_RPC_PING, NULL, 0);
}

/* Send PONG response */
static int send_pong(ipfs_network_t* net, const char* addr, uint16_t port) {
    return send_rpc(net, addr, port, IPFS_RPC_PONG, NULL, 0);
}

/* Send FIND_NODE */
static int send_find_node(ipfs_network_t* net, const char* addr, uint16_t port,
                          const uint8_t* target_id) {
    return send_rpc(net, addr, port, IPFS_RPC_FIND_NODE, target_id, IPFS_NODE_ID_SIZE);
}

/* Send FIND_VALUE */
static int send_find_value(ipfs_network_t* net, const char* addr, uint16_t port,
                           const uint8_t* hash) {
    return send_rpc(net, addr, port, IPFS_RPC_FIND_VALUE, hash, IPFS_HASH_SIZE);
}

/* Find closest nodes to target */
int ipfs_network_find_node(ipfs_network_t* net, const uint8_t* target_id,
                           ipfs_endpoint_t* results, size_t* count) {
    if (!net || !target_id || !results || !count) {
        return -1;
    }
    
    /* Collect up to IPFS_K_BUCKET_SIZE closest peers from routing table */
    size_t max_results = *count;
    *count = 0;
    
    /* For MVP, just return peers from routing table sorted by distance */
    typedef struct {
        ipfs_peer_t* peer;
        uint8_t distance[IPFS_NODE_ID_SIZE];
    } peer_dist_t;
    
    peer_dist_t* peers = malloc(net->routing_table.total_peers * sizeof(peer_dist_t));
    if (!peers) {
        return -1;
    }
    
    size_t peer_idx = 0;
    for (int i = 0; i < IPFS_NODE_ID_SIZE * 8; i++) {
        ipfs_kbucket_t* bucket = &net->routing_table.buckets[i];
        for (size_t j = 0; j < bucket->count; j++) {
            if (peer_idx < net->routing_table.total_peers) {
                peers[peer_idx].peer = &bucket->peers[j];
                xor_distance(bucket->peers[j].node_id, target_id, peers[peer_idx].distance);
                peer_idx++;
            }
        }
    }
    
    /* Sort by distance (simple bubble sort for MVP) */
    for (size_t i = 0; i < peer_idx - 1; i++) {
        for (size_t j = 0; j < peer_idx - i - 1; j++) {
            if (memcmp(peers[j].distance, peers[j+1].distance, IPFS_NODE_ID_SIZE) > 0) {
                peer_dist_t tmp = peers[j];
                peers[j] = peers[j+1];
                peers[j+1] = tmp;
            }
        }
    }
    
    /* Return closest */
    for (size_t i = 0; i < peer_idx && i < max_results; i++) {
        memcpy(results[i].addr, peers[i].peer->endpoint.addr, sizeof(results[i].addr));
        results[i].udp_port = peers[i].peer->endpoint.udp_port;
        results[i].tcp_port = peers[i].peer->endpoint.tcp_port;
        (*count)++;
    }
    
    free(peers);
    return 0;
}

/* Find value providers */
int ipfs_network_find_value(ipfs_network_t* net, const uint8_t* hash,
                            ipfs_endpoint_t* providers, size_t* count) {
    if (!net || !hash || !providers || !count) {
        return -1;
    }
    
    *count = 0;
    
    /* For MVP, query closest nodes from routing table */
    ipfs_endpoint_t closest[IPFS_K_BUCKET_SIZE];
    size_t closest_count = IPFS_K_BUCKET_SIZE;
    
    if (ipfs_network_find_node(net, hash, closest, &closest_count) != 0) {
        return -1;
    }
    
    /* Send FIND_VALUE to each and collect responses */
    /* For MVP, just return the closest nodes as potential providers */
    for (size_t i = 0; i < closest_count && i < *count; i++) {
        memcpy(&providers[i], &closest[i], sizeof(ipfs_endpoint_t));
        (*count)++;
    }
    
    return 0;
}

/* Announce that we have a block */
int ipfs_network_provide(ipfs_network_t* net, const uint8_t* hash) {
    if (!net || !hash) {
        return -1;
    }
    
    /* For MVP, this would broadcast PROVIDE message to closest nodes */
    /* Implementation would store provider info in DHT */
    return 0;
}

/* Request block via TCP */
int ipfs_network_get_block(ipfs_network_t* net, const ipfs_endpoint_t* peer,
                           const uint8_t* hash, uint8_t** data, size_t* len) {
    if (!net || !peer || !hash || !data || !len) {
        return -1;
    }
    
    *data = NULL;
    *len = 0;
    
    /* Connect to peer's TCP port */
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer->tcp_port);
    
    if (inet_pton(AF_INET, peer->addr, &addr.sin_addr) <= 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    
    /* Set timeout */
#ifdef _WIN32
    DWORD timeout = IPFS_RPC_TIMEOUT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = IPFS_RPC_TIMEOUT_MS / 1000;
    tv.tv_usec = (IPFS_RPC_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    
    /* Send request: REQUEST_BLOCK:<hash> */
    char request[256];
    char hash_hex[IPFS_HASH_SIZE * 2 + 1];
    for (int i = 0; i < IPFS_HASH_SIZE; i++) {
        sprintf(hash_hex + (i * 2), "%02x", hash[i]);
    }
    snprintf(request, sizeof(request), "REQUEST_BLOCK:%s\n", hash_hex);
    
    if (send(sock, request, strlen(request), 0) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    
    /* Read response header: BLOCK:<size>\n */
    char response[256] = {0};
    size_t resp_idx = 0;
    char c;
    
    while (resp_idx < sizeof(response) - 1) {
        if (recv(sock, &c, 1, 0) <= 0) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return -1;
        }
        response[resp_idx++] = c;
        if (c == '\n') break;
    }
    
    /* Parse size */
    if (strncmp(response, "BLOCK:", 6) != 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    
    size_t block_size = strtoul(response + 6, NULL, 10);
    if (block_size == 0 || block_size > IPFS_CHUNK_SIZE * 2) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    
    /* Read block data */
    *data = malloc(block_size);
    if (!*data) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    
    size_t total_read = 0;
    while (total_read < block_size) {
        ssize_t n = recv(sock, (char*)*data + total_read, block_size - total_read, 0);
        if (n <= 0) {
            free(*data);
            *data = NULL;
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return -1;
        }
        total_read += n;
    }
    
    *len = total_read;
    
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    
    return 0;
}

/* Bootstrap with known peers */
int ipfs_network_bootstrap(ipfs_network_t* net, const ipfs_endpoint_t* peers, size_t count) {
    if (!net || !peers || count == 0) {
        return -1;
    }
    
    for (size_t i = 0; i < count; i++) {
        /* Send PING to bootstrap peer */
        if (send_ping(net, peers[i].addr, peers[i].udp_port) == 0) {
            /* We'll add them to routing table when we get PONG */
        }
    }
    
    return 0;
}

/* Poll network for incoming messages */
int ipfs_network_poll(ipfs_network_t* net, int timeout_ms) {
    if (!net) {
        return -1;
    }
    
#ifdef _WIN32
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(net->udp_socket, &read_fds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    if (select(0, &read_fds, NULL, NULL, &tv) <= 0) {
        return 0;
    }
    
    if (!FD_ISSET(net->udp_socket, &read_fds)) {
        return 0;
    }
#else
    struct pollfd fds[1];
    fds[0].fd = net->udp_socket;
    fds[0].events = POLLIN;
    
    if (poll(fds, 1, timeout_ms) <= 0) {
        return 0;
    }
    
    if (!(fds[0].revents & POLLIN)) {
        return 0;
    }
#endif
    
    /* Process incoming UDP packet */
    uint8_t rpc_type, sender_id[IPFS_NODE_ID_SIZE], payload[IPFS_RPC_MAX_SIZE];
    char sender_addr[64];
    uint16_t sender_port;
    size_t payload_len = sizeof(payload);
    
    if (recv_rpc(net, &rpc_type, sender_id, payload, &payload_len, 
                 sender_addr, &sender_port) != 0) {
        return 0;
    }
    
    /* Add sender to routing table */
    ipfs_endpoint_t endpoint;
    strncpy(endpoint.addr, sender_addr, sizeof(endpoint.addr) - 1);
    endpoint.udp_port = sender_port;
    endpoint.tcp_port = net->tcp_port;  /* Assume same TCP port */
    ipfs_network_add_peer(net, sender_id, &endpoint);
    
    /* Handle RPC */
    switch (rpc_type) {
        case IPFS_RPC_PING:
            /* Respond with PONG */
            send_pong(net, sender_addr, sender_port);
            break;
            
        case IPFS_RPC_PONG:
            /* Peer is alive, already added to routing table */
            break;
            
        case IPFS_RPC_HELO:
            /* Local discovery - respond with HELO_ACK and add to local bucket */
            fprintf(stderr, "[DISCOVERY] Received HELLO from %s:%d\n", sender_addr, sender_port);
            send_rpc(net, sender_addr, sender_port, IPFS_RPC_HELO_ACK, NULL, 0);
            break;
            
        case IPFS_RPC_HELO_ACK:
            /* Received HELLO response - add peer to local routing table */
            fprintf(stderr, "[DISCOVERY] Received HELLO_ACK from %s:%d\n", sender_addr, sender_port);
            break;
            
        case IPFS_RPC_FIND_NODE: {
            /* Return closest nodes to target */
            uint8_t target_id[IPFS_NODE_ID_SIZE];
            memcpy(target_id, payload, IPFS_NODE_ID_SIZE);
            
            ipfs_endpoint_t closest[IPFS_K_BUCKET_SIZE];
            size_t count = IPFS_K_BUCKET_SIZE;
            
            if (ipfs_network_find_node(net, target_id, closest, &count) == 0) {
                /* Send back closest nodes (simplified - just send count) */
                /* Production would serialize full endpoint list */
            }
            break;
        }
        
        case IPFS_RPC_FIND_VALUE: {
            /* Check if we have the block, otherwise return closest nodes */
            uint8_t hash[IPFS_HASH_SIZE];
            memcpy(hash, payload, IPFS_HASH_SIZE);
            
            /* For MVP, just return closest nodes */
            ipfs_endpoint_t closest[IPFS_K_BUCKET_SIZE];
            size_t count = IPFS_K_BUCKET_SIZE;
            
            if (ipfs_network_find_node(net, hash, closest, &count) == 0) {
                /* Send back providers */
            }
            break;
        }
        
        default:
            break;
    }
    
    /* Send periodic HELLO broadcast if enabled */
    if (net->broadcast_enabled && net->last_broadcast + net->broadcast_interval_secs < time(NULL)) {
        ipfs_network_broadcast_helo(net);
    }
    
    return 1;  /* Message processed */
}
