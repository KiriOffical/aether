/**
 * Handshake Protocol
 */

#ifndef AETHER_HANDSHAKE_H
#define AETHER_HANDSHAKE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HANDSHAKE_VERSION       1
#define HANDSHAKE_TIMEOUT_SECS  10
#define MAX_CLOCK_SKEW_SECS     (5 * 60)

/* Handshake state */
typedef enum {
    HANDSHAKE_INIT = 0,
    HANDSHAKE_AWAITING_ACK = 1,
    HANDSHAKE_RECEIVED_HELLO = 2,
    HANDSHAKE_COMPLETE = 3,
    HANDSHAKE_FAILED = 4
} handshake_state_t;

/* Handshake context */
typedef struct {
    handshake_state_t state;
    uint8_t self_node_id[32];
    uint8_t self_secret_key[64];
    uint8_t remote_node_id[32];
    uint64_t started_at;
    char remote_protocols[8][64];
    size_t remote_protocol_count;
    char remote_addr[64];
    uint16_t remote_port;
    uint8_t challenge[32];
    int is_initiator;
} handshake_t;

/* Lifecycle */
void handshake_init_initiator(handshake_t* h, const uint8_t* node_id, const uint8_t* secret_key);
void handshake_init_responder(handshake_t* h, const uint8_t* node_id, const uint8_t* secret_key);

/* Build outbound Hello message */
int handshake_build_hello(handshake_t* h, uint8_t** out_data, size_t* out_len);

/* Process received Hello, build HelloAck response */
int handshake_receive_hello(handshake_t* h, const uint8_t* data, size_t len,
                            uint8_t** out_ack, size_t* out_ack_len);

/* Process received HelloAck */
int handshake_receive_hello_ack(handshake_t* h, const uint8_t* data, size_t len);

/* State queries */
int handshake_is_complete(handshake_t* h);
int handshake_is_timed_out(handshake_t* h);
const uint8_t* handshake_remote_node_id(handshake_t* h);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_HANDSHAKE_H */
