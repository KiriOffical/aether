/**
 * Message Types and Serialization
 */

#ifndef AETHER_MESSAGE_H
#define AETHER_MESSAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Message types */
typedef enum {
    MSG_HELLO = 1,
    MSG_HELLO_ACK = 2,
    MSG_FIND_NODE = 3,
    MSG_FIND_NODE_RESPONSE = 4,
    MSG_STORE_VALUE = 5,
    MSG_GET_VALUE = 6,
    MSG_GET_VALUE_RESPONSE = 7,
    MSG_FRAGMENT_REQUEST = 8,
    MSG_FRAGMENT_RESPONSE = 9,
    MSG_PING = 10,
    MSG_PONG = 11,
    MSG_DISCONNECT = 12,
    MSG_PEER_EXCHANGE = 13,
    MSG_ERROR = 14
} message_type_t;

/* Error codes */
typedef enum {
    ERR_OK = 0,
    ERR_INVALID_FORMAT = 1,
    ERR_INVALID_SIGNATURE = 2,
    ERR_VERSION_MISMATCH = 3,
    ERR_NOT_FOUND = 4,
    ERR_RATE_LIMITED = 5,
    ERR_INTERNAL_ERROR = 6,
    ERR_BLACKLISTED = 7,
    ERR_PROTOCOL_ERROR = 8
} error_code_t;

/* Endpoint */
typedef enum {
    ENDPOINT_IP4 = 0,
    ENDPOINT_IP6 = 1
} endpoint_proto_t;

typedef struct {
    endpoint_proto_t proto;
    uint8_t address[16];
    uint16_t port;
} endpoint_t;

/* Hello message */
typedef struct {
    uint32_t version;
    uint8_t node_id[32];
    uint64_t timestamp;
    char protocols[8][64];
    size_t protocol_count;
    endpoint_t listen_addr;
    uint8_t signature[64];
} hello_t;

/* HelloAck message */
typedef struct {
    uint8_t node_id[32];
    uint64_t timestamp;
    char protocols[8][64];
    size_t protocol_count;
    endpoint_t listen_addr;
    uint8_t signature[64];
    uint8_t challenge[32];
} hello_ack_t;

/* FindNode message */
typedef struct {
    uint8_t target_id[32];
    uint32_t k;
} find_node_t;

/* Node info for responses */
typedef struct {
    uint8_t node_id[32];
    endpoint_t endpoint;
    uint32_t distance;
} node_info_t;

/* FindNode response */
typedef struct {
    node_info_t nodes[20];
    size_t node_count;
} find_node_response_t;

/* StoreValue message */
typedef struct {
    uint8_t key[32];
    uint8_t* value;
    size_t value_len;
    uint64_t expiration;
    uint8_t signature[64];
} store_value_t;

/* GetValue message */
typedef struct {
    uint8_t key[32];
} get_value_t;

/* GetValue response */
typedef struct {
    int found;
    uint8_t* value;
    size_t value_len;
    node_info_t closer_nodes[20];
    size_t closer_count;
} get_value_response_t;

/* Ping message */
typedef struct {
    uint64_t sequence;
} ping_t;

/* Pong message */
typedef struct {
    uint64_t sequence;
    uint64_t latency_ns;
} pong_t;

/* Disconnect message */
typedef enum {
    DISCONNECT_SHUTDOWN = 0,
    DISCONNECT_PROTOCOL_ERROR = 1,
    DISCONNECT_TIMEOUT = 2,
    DISCONNECT_MAINTENANCE = 3,
    DISCONNECT_BLACKLISTED = 4
} disconnect_reason_t;

typedef struct {
    disconnect_reason_t reason;
    char message[256];
} disconnect_t;

/* PeerExchange message */
typedef struct {
    endpoint_t peers[50];
    size_t peer_count;
    uint64_t timestamp;
} peer_exchange_t;

/* Error message */
typedef struct {
    error_code_t code;
    char message[256];
} error_msg_t;

/* Generic message wrapper */
typedef struct {
    message_type_t type;
    union {
        hello_t hello;
        hello_ack_t hello_ack;
        find_node_t find_node;
        find_node_response_t find_node_response;
        store_value_t store_value;
        get_value_t get_value;
        get_value_response_t get_value_response;
        ping_t ping;
        pong_t pong;
        disconnect_t disconnect;
        peer_exchange_t peer_exchange;
        error_msg_t error;
    } payload;
} message_t;

/* Serialization */
int message_encode(const message_t* msg, uint8_t** out_data, size_t* out_len);
int message_decode(const uint8_t* data, size_t len, message_t* out_msg);
void message_free(message_t* msg);

/* Message helpers */
void hello_init(hello_t* h, uint32_t version, const uint8_t* node_id, uint64_t timestamp);
void ping_init(ping_t* p, uint64_t sequence);
void pong_init(pong_t* p, uint64_t sequence, uint64_t latency_ns);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_MESSAGE_H */
