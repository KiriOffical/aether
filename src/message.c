/**
 * Message Serialization Implementation
 */

#include "message.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Simple binary serialization */

static size_t write_u32(uint8_t* buf, uint32_t val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
    return 4;
}

static size_t read_u32(const uint8_t* buf, uint32_t* val) {
    *val = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3]);
    return 4;
}

static size_t write_u64(uint8_t* buf, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        buf[i] = (val >> (56 - i * 8)) & 0xFF;
    }
    return 8;
}

static size_t read_u64(const uint8_t* buf, uint64_t* val) {
    *val = 0;
    for (int i = 0; i < 8; i++) {
        *val |= ((uint64_t)buf[i]) << (56 - i * 8);
    }
    return 8;
}

int message_encode(const message_t* msg, uint8_t** out_data, size_t* out_len) {
    if (!msg || !out_data || !out_len) return -1;
    
    /* Calculate size based on message type */
    size_t payload_size = 0;
    
    switch (msg->type) {
        case MSG_HELLO:
            payload_size = 4 + 32 + 8 + 64 + 18;  /* version + node_id + timestamp + sig + overhead */
            break;
        case MSG_HELLO_ACK:
            payload_size = 32 + 8 + 64 + 32;
            break;
        case MSG_FIND_NODE:
            payload_size = 32 + 4;
            break;
        case MSG_FIND_NODE_RESPONSE:
            payload_size = 1 + msg->payload.find_node_response.node_count * (32 + 18 + 4);
            break;
        case MSG_PING:
        case MSG_PONG:
            payload_size = 8;
            break;
        case MSG_DISCONNECT:
            payload_size = 4 + 256;
            break;
        default:
            payload_size = 64;  /* Default estimate */
            break;
    }
    
    /* Allocate buffer */
    size_t total_size = 1 + payload_size;  /* type byte + payload */
    uint8_t* buf = (uint8_t*)malloc(total_size);
    if (!buf) return -1;
    
    size_t offset = 0;
    
    /* Write message type */
    buf[offset++] = (uint8_t)msg->type;
    
    /* Serialize payload based on type */
    switch (msg->type) {
        case MSG_HELLO: {
            const hello_t* h = &msg->payload.hello;
            offset += write_u32(buf + offset, h->version);
            memcpy(buf + offset, h->node_id, 32); offset += 32;
            offset += write_u64(buf + offset, h->timestamp);
            memcpy(buf + offset, h->signature, 64); offset += 64;
            break;
        }
        case MSG_HELLO_ACK: {
            const hello_ack_t* h = &msg->payload.hello_ack;
            memcpy(buf + offset, h->node_id, 32); offset += 32;
            offset += write_u64(buf + offset, h->timestamp);
            memcpy(buf + offset, h->signature, 64); offset += 64;
            memcpy(buf + offset, h->challenge, 32); offset += 32;
            break;
        }
        case MSG_FIND_NODE: {
            const find_node_t* f = &msg->payload.find_node;
            memcpy(buf + offset, f->target_id, 32); offset += 32;
            offset += write_u32(buf + offset, f->k);
            break;
        }
        case MSG_PING: {
            const ping_t* p = &msg->payload.ping;
            offset += write_u64(buf + offset, p->sequence);
            break;
        }
        case MSG_PONG: {
            const pong_t* p = &msg->payload.pong;
            offset += write_u64(buf + offset, p->sequence);
            offset += write_u64(buf + offset, p->latency_ns);
            break;
        }
        case MSG_DISCONNECT: {
            const disconnect_t* d = &msg->payload.disconnect;
            offset += write_u32(buf + offset, (uint32_t)d->reason);
            strncpy((char*)buf + offset, d->message, 256);
            offset += 256;
            break;
        }
        default:
            break;
    }
    
    *out_data = buf;
    *out_len = offset;
    return 0;
}

int message_decode(const uint8_t* data, size_t len, message_t* out_msg) {
    if (!data || len < 1 || !out_msg) return -1;
    
    size_t offset = 0;
    out_msg->type = (message_type_t)data[offset++];
    
    switch (out_msg->type) {
        case MSG_HELLO: {
            hello_t* h = &out_msg->payload.hello;
            if (len < offset + 4 + 32 + 8 + 64) return -1;
            offset += read_u32(data + offset, &h->version);
            memcpy(h->node_id, data + offset, 32); offset += 32;
            offset += read_u64(data + offset, &h->timestamp);
            memcpy(h->signature, data + offset, 64);
            break;
        }
        case MSG_HELLO_ACK: {
            hello_ack_t* h = &out_msg->payload.hello_ack;
            if (len < offset + 32 + 8 + 64 + 32) return -1;
            memcpy(h->node_id, data + offset, 32); offset += 32;
            offset += read_u64(data + offset, &h->timestamp);
            memcpy(h->signature, data + offset, 64); offset += 64;
            memcpy(h->challenge, data + offset, 32);
            break;
        }
        case MSG_FIND_NODE: {
            find_node_t* f = &out_msg->payload.find_node;
            if (len < offset + 32 + 4) return -1;
            memcpy(f->target_id, data + offset, 32); offset += 32;
            offset += read_u32(data + offset, &f->k);
            break;
        }
        case MSG_PING: {
            ping_t* p = &out_msg->payload.ping;
            if (len < offset + 8) return -1;
            offset += read_u64(data + offset, &p->sequence);
            break;
        }
        case MSG_PONG: {
            pong_t* p = &out_msg->payload.pong;
            if (len < offset + 16) return -1;
            offset += read_u64(data + offset, &p->sequence);
            offset += read_u64(data + offset, &p->latency_ns);
            break;
        }
        case MSG_DISCONNECT: {
            disconnect_t* d = &out_msg->payload.disconnect;
            if (len < offset + 4 + 256) return -1;
            uint32_t reason;
            offset += read_u32(data + offset, &reason);
            d->reason = (disconnect_reason_t)reason;
            strncpy(d->message, (const char*)data + offset, 256);
            break;
        }
        default:
            return -1;
    }
    
    return 0;
}

void message_free(message_t* msg) {
    if (!msg) return;
    
    /* Free any dynamically allocated payload data */
    switch (msg->type) {
        case MSG_STORE_VALUE:
            free(msg->payload.store_value.value);
            break;
        case MSG_GET_VALUE_RESPONSE:
            free(msg->payload.get_value_response.value);
            break;
        default:
            break;
    }
}

void hello_init(hello_t* h, uint32_t version, const uint8_t* node_id, uint64_t timestamp) {
    memset(h, 0, sizeof(hello_t));
    h->version = version;
    memcpy(h->node_id, node_id, 32);
    h->timestamp = timestamp;
}

void ping_init(ping_t* p, uint64_t sequence) {
    memset(p, 0, sizeof(ping_t));
    p->sequence = sequence;
}

void pong_init(pong_t* p, uint64_t sequence, uint64_t latency_ns) {
    memset(p, 0, sizeof(pong_t));
    p->sequence = sequence;
    p->latency_ns = latency_ns;
}
