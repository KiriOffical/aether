/**
 * Message Framing Implementation
 */

#include "framing.h"
#include "../include/aether.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int framer_init(framer_t* f, size_t max_frame_size) {
    f->buffer = (uint8_t*)malloc(4096);
    if (!f->buffer) return -1;
    
    f->buffer_size = 4096;
    f->buffer_len = 0;
    f->max_frame_size = max_frame_size;
    f->current_frame_size = 0;
    return 0;
}

void framer_free(framer_t* f) {
    free(f->buffer);
    f->buffer = NULL;
    f->buffer_size = 0;
    f->buffer_len = 0;
}

int framer_encode(framer_t* f, const uint8_t* payload, size_t payload_len,
                  uint8_t** out_frame, size_t* out_len) {
    if (payload_len > f->max_frame_size) {
        return -1;
    }
    
    size_t frame_len = FRAME_HEADER_SIZE + payload_len;
    *out_frame = (uint8_t*)malloc(frame_len);
    if (!*out_frame) return -1;
    
    /* Write length prefix (big-endian) */
    (*out_frame)[0] = (payload_len >> 24) & 0xFF;
    (*out_frame)[1] = (payload_len >> 16) & 0xFF;
    (*out_frame)[2] = (payload_len >> 8) & 0xFF;
    (*out_frame)[3] = payload_len & 0xFF;
    
    /* Copy payload */
    memcpy(*out_frame + FRAME_HEADER_SIZE, payload, payload_len);
    
    *out_len = frame_len;
    return 0;
}

int framer_receive(framer_t* f, const uint8_t* data, size_t len) {
    /* Ensure buffer has space */
    if (f->buffer_len + len > f->buffer_size) {
        size_t new_size = f->buffer_size * 2;
        while (new_size < f->buffer_len + len) {
            new_size *= 2;
        }
        uint8_t* new_buf = (uint8_t*)realloc(f->buffer, new_size);
        if (!new_buf) return -1;
        f->buffer = new_buf;
        f->buffer_size = new_size;
    }
    
    memcpy(f->buffer + f->buffer_len, data, len);
    f->buffer_len += len;
    return 0;
}

int framer_decode(framer_t* f, uint8_t** out_payload, size_t* out_len) {
    /* Need header */
    if (f->buffer_len < FRAME_HEADER_SIZE && f->current_frame_size == 0) {
        return 0;  /* Not enough data */
    }
    
    /* Read frame size if not known */
    if (f->current_frame_size == 0) {
        uint32_t frame_size = ((uint32_t)f->buffer[0] << 24) |
                              ((uint32_t)f->buffer[1] << 16) |
                              ((uint32_t)f->buffer[2] << 8) |
                              ((uint32_t)f->buffer[3]);
        
        if (frame_size > f->max_frame_size) {
            return -1;  /* Frame too large */
        }
        
        f->current_frame_size = frame_size;
    }
    
    /* Check if we have complete frame */
    size_t total_size = FRAME_HEADER_SIZE + f->current_frame_size;
    if (f->buffer_len < total_size) {
        return 0;  /* Not enough data */
    }
    
    /* Extract payload */
    *out_payload = (uint8_t*)malloc(f->current_frame_size);
    if (!*out_payload) return -1;
    
    memcpy(*out_payload, f->buffer + FRAME_HEADER_SIZE, f->current_frame_size);
    *out_len = f->current_frame_size;
    
    /* Advance buffer */
    size_t remaining = f->buffer_len - total_size;
    memmove(f->buffer, f->buffer + total_size, remaining);
    f->buffer_len = remaining;
    f->current_frame_size = 0;
    
    return 1;  /* Success */
}

void framer_clear(framer_t* f) {
    f->buffer_len = 0;
    f->current_frame_size = 0;
}

int frame_message(const uint8_t* payload, size_t payload_len,
                  uint8_t** out_frame, size_t* out_len) {
    framer_t f;
    if (framer_init(&f, AETHER_MAX_MESSAGE_SIZE) != 0) {
        return -1;
    }
    
    int result = framer_encode(&f, payload, payload_len, out_frame, out_len);
    framer_free(&f);
    return result;
}

int parse_frame(const uint8_t* data, size_t len, uint8_t** out_payload, size_t* out_len) {
    if (len < FRAME_HEADER_SIZE) {
        return -1;
    }
    
    uint32_t frame_size = ((uint32_t)data[0] << 24) |
                          ((uint32_t)data[1] << 16) |
                          ((uint32_t)data[2] << 8) |
                          ((uint32_t)data[3]);
    
    if (frame_size > AETHER_MAX_MESSAGE_SIZE) {
        return -1;
    }
    
    size_t total_size = FRAME_HEADER_SIZE + frame_size;
    if (len < total_size) {
        return -1;
    }
    
    *out_payload = (uint8_t*)malloc(frame_size);
    if (!*out_payload) return -1;
    
    memcpy(*out_payload, data + FRAME_HEADER_SIZE, frame_size);
    *out_len = frame_size;
    
    return (int)total_size;
}
