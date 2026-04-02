/**
 * Message Framing Layer
 * Length-prefixed binary protocol
 */

#ifndef AETHER_FRAMING_H
#define AETHER_FRAMING_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_HEADER_SIZE 4

/* Framer state */
typedef struct {
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_len;
    size_t max_frame_size;
    uint32_t current_frame_size;
} framer_t;

/* Initialize framer */
int framer_init(framer_t* f, size_t max_frame_size);
void framer_free(framer_t* f);

/* Encode a message into a frame */
int framer_encode(framer_t* f, const uint8_t* payload, size_t payload_len,
                  uint8_t** out_frame, size_t* out_len);

/* Add received data to buffer */
int framer_receive(framer_t* f, const uint8_t* data, size_t len);

/* Try to decode a complete frame */
int framer_decode(framer_t* f, uint8_t** out_payload, size_t* out_len);

/* Clear buffer */
void framer_clear(framer_t* f);

/* Utility: frame a message (allocates memory, caller must free) */
int frame_message(const uint8_t* payload, size_t payload_len,
                  uint8_t** out_frame, size_t* out_len);

/* Utility: parse a frame */
int parse_frame(const uint8_t* data, size_t len, uint8_t** out_payload, size_t* out_len);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_FRAMING_H */
