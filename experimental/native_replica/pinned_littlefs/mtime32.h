#ifndef RISC_LITTLEFS_MTIME32_H
#define RISC_LITTLEFS_MTIME32_H
/* The shipped ESP32-S3 image stores signed 32-bit, little-endian seconds in
 * attribute 't'. Native libc time_t is an API type, never an on-disk schema. */
#include <stdint.h>
#include <time.h>
#include <errno.h>
static inline int risc_littlefs_mtime_encode(time_t value, uint8_t bytes[4]) {
    const int64_t seconds = (int64_t)value;
    if (seconds < INT32_MIN || seconds > INT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    const uint32_t bits = (uint32_t)seconds;
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(bits >> (8 * i));
    return 0;
}
static inline time_t risc_littlefs_mtime_decode(const uint8_t bytes[4]) {
    const uint32_t bits = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return (time_t)(bits <= INT32_MAX ? (int64_t)bits : (int64_t)bits - INT64_C(4294967296));
}
#endif
