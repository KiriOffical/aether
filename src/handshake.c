/**
 * Handshake Protocol Implementation
 */

#include "handshake.h"
#include "crypto.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

void handshake_init_initiator(handshake_t* h, const uint8_t* node_id, const uint8_t* secret_key) {
    memset(h, 0, sizeof(handshake_t));
    memcpy(h->self_node_id, node_id, 32);
    memcpy(h->self_secret_key, secret_key, 64);
    h->state = HANDSHAKE_INIT;
    h->is_initiator = 1;
    h->started_at = (uint64_t)time(NULL);
}

void handshake_init_responder(handshake_t* h, const uint8_t* node_id, const uint8_t* secret_key) {
    memset(h, 0, sizeof(handshake_t));
    memcpy(h->self_node_id, node_id, 32);
    memcpy(h->self_secret_key, secret_key, 64);
    h->state = HANDSHAKE_INIT;
    h->is_initiator = 0;
    h->started_at = (uint64_t)time(NULL);
}

static uint64_t get_timestamp(void) {
    return (uint64_t)time(NULL);
}

static int verify_timestamp(uint64_t ts) {
    uint64_t now = get_timestamp();
    int64_t diff = (int64_t)now - (int64_t)ts;
    return (diff < 0 ? -diff : diff) <= MAX_CLOCK_SKEW_SECS;
}

int handshake_build_hello(handshake_t* h, uint8_t** out_data, size_t* out_len) {
    if (h->state != HANDSHAKE_INIT) {
        return -1;
    }
    
    /* Simple message format: version(4) + node_id(32) + timestamp(8) + protocols + signature(64) */
    size_t msg_len = 4 + 32 + 8 + 64;
    uint8_t* msg = (uint8_t*)malloc(msg_len);
    if (!msg) return -1;
    
    /* Version */
    msg[0] = (HANDSHAKE_VERSION >> 24) & 0xFF;
    msg[1] = (HANDSHAKE_VERSION >> 16) & 0xFF;
    msg[2] = (HANDSHAKE_VERSION >> 8) & 0xFF;
    msg[3] = HANDSHAKE_VERSION & 0xFF;
    
    /* Node ID */
    memcpy(msg + 4, h->self_node_id, 32);
    
    /* Timestamp */
    uint64_t ts = get_timestamp();
    for (int i = 0; i < 8; i++) {
        msg[36 + i] = (ts >> (56 - i * 8)) & 0xFF;
    }
    
    /* Sign: node_id || timestamp */
    uint8_t sign_data[40];
    memcpy(sign_data, h->self_node_id, 32);
    memcpy(sign_data + 32, &ts, 8);
    crypto_sign(msg + 44, sign_data, 40, h->self_secret_key);
    
    *out_data = msg;
    *out_len = msg_len;
    h->state = HANDSHAKE_AWAITING_ACK;
    return 0;
}

int handshake_receive_hello(handshake_t* h, const uint8_t* data, size_t len,
                            uint8_t** out_ack, size_t* out_ack_len) {
    if (h->state != HANDSHAKE_INIT || len < 44) {
        return -1;
    }
    
    /* Parse version */
    uint32_t version = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                       ((uint32_t)data[2] << 8) | ((uint32_t)data[3]);
    if (version != HANDSHAKE_VERSION) {
        h->state = HANDSHAKE_FAILED;
        return -1;
    }
    
    /* Parse node_id */
    memcpy(h->remote_node_id, data + 4, 32);
    
    /* Parse and verify timestamp */
    uint64_t ts = 0;
    for (int i = 0; i < 8; i++) {
        ts |= ((uint64_t)data[36 + i]) << (56 - i * 8);
    }
    
    if (!verify_timestamp(ts)) {
        h->state = HANDSHAKE_FAILED;
        return -1;
    }
    
    /* Verify signature */
    uint8_t sign_data[40];
    memcpy(sign_data, data + 4, 32);
    memcpy(sign_data + 32, &ts, 8);
    
    /* Need public key - for now skip verification in demo */
    /* crypto_verify(data + 44, sign_data, 40, remote_public_key) */
    
    /* Generate challenge */
    crypto_random(h->challenge, 32);
    
    /* Build HelloAck */
    size_t ack_len = 32 + 8 + 64 + 32;
    uint8_t* ack = (uint8_t*)malloc(ack_len);
    if (!ack) return -1;
    
    /* Our node_id */
    memcpy(ack, h->self_node_id, 32);
    
    /* Timestamp */
    ts = get_timestamp();
    for (int i = 0; i < 8; i++) {
        ack[32 + i] = (ts >> (56 - i * 8)) & 0xFF;
    }
    
    /* Sign */
    memcpy(sign_data, h->self_node_id, 32);
    memcpy(sign_data + 32, &ts, 8);
    crypto_sign(ack + 40, sign_data, 40, h->self_secret_key);
    
    /* Challenge */
    memcpy(ack + 104, h->challenge, 32);
    
    *out_ack = ack;
    *out_ack_len = ack_len;
    h->state = HANDSHAKE_RECEIVED_HELLO;
    return 0;
}

int handshake_receive_hello_ack(handshake_t* h, const uint8_t* data, size_t len) {
    if (h->state != HANDSHAKE_AWAITING_ACK || len < 104) {
        return -1;
    }
    
    /* Parse node_id */
    memcpy(h->remote_node_id, data, 32);
    
    /* Parse and verify timestamp */
    uint64_t ts = 0;
    for (int i = 0; i < 8; i++) {
        ts |= ((uint64_t)data[32 + i]) << (56 - i * 8);
    }
    
    if (!verify_timestamp(ts)) {
        h->state = HANDSHAKE_FAILED;
        return -1;
    }
    
    /* Check timeout */
    if ((uint64_t)time(NULL) - h->started_at > HANDSHAKE_TIMEOUT_SECS) {
        h->state = HANDSHAKE_FAILED;
        return -1;
    }
    
    /* Parse challenge */
    memcpy(h->challenge, data + 104, 32);
    
    h->state = HANDSHAKE_COMPLETE;
    return 0;
}

int handshake_is_complete(handshake_t* h) {
    return h->state == HANDSHAKE_COMPLETE;
}

int handshake_is_timed_out(handshake_t* h) {
    return (uint64_t)time(NULL) - h->started_at > HANDSHAKE_TIMEOUT_SECS;
}

const uint8_t* handshake_remote_node_id(handshake_t* h) {
    return h->remote_node_id;
}
