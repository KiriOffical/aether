/**
 * Utility Functions Implementation
 */

#include "util.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/time.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

static const char hex_chars[] = "0123456789abcdef";

void util_to_hex(char* out, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex_chars[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex_chars[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

int util_from_hex(uint8_t* out, const char* hex, size_t hex_len) {
    if (hex_len % 2 != 0) return -1;
    
    for (size_t i = 0; i < hex_len / 2; i++) {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        
        int high_val = (high >= '0' && high <= '9') ? high - '0' :
                       (high >= 'a' && high <= 'f') ? high - 'a' + 10 :
                       (high >= 'A' && high <= 'F') ? high - 'A' + 10 : -1;
        int low_val = (low >= '0' && low <= '9') ? low - '0' :
                      (low >= 'a' && low <= 'f') ? low - 'a' + 10 :
                      (low >= 'A' && low <= 'F') ? low - 'A' + 10 : -1;
        
        if (high_val < 0 || low_val < 0) return -1;
        
        out[i] = (uint8_t)((high_val << 4) | low_val);
    }
    return 0;
}

uint64_t util_timestamp_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

uint64_t util_timestamp_us(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (uint64_t)(count.QuadPart * 1000000 / freq.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

int util_starts_with(const char* str, const char* prefix) {
    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);
    if (prefix_len > str_len) return 0;
    return strncmp(str, prefix, prefix_len) == 0;
}

int util_ends_with(const char* str, const char* suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

void util_zero(void* ptr, size_t len) {
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    while (len--) {
        *p++ = 0;
    }
}

int util_const_time_eq(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}
