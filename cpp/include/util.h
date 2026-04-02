/**
 * Utility Functions
 */

#ifndef AETHER_UTIL_H
#define AETHER_UTIL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hex encoding */
void util_to_hex(char* out, const uint8_t* data, size_t len);
int util_from_hex(uint8_t* out, const char* hex, size_t hex_len);

/* Time utilities */
uint64_t util_timestamp_ms(void);
uint64_t util_timestamp_us(void);

/* String utilities */
int util_starts_with(const char* str, const char* prefix);
int util_ends_with(const char* str, const char* suffix);

/* Memory utilities */
void util_zero(void* ptr, size_t len);
int util_const_time_eq(const uint8_t* a, const uint8_t* b, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_UTIL_H */
