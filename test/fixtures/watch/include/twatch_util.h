#pragma once
#include <stdbool.h>
#include <stddef.h>
static inline bool twatch_equal(const char *a, const char *b) {
    if (!a || !b)
        return false;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static inline void twatch_copy_error(char *dst, size_t cap, const char *msg) {
    if (!dst || !cap)
        return;
    size_t i = 0;
    if (msg)
        while (msg[i] && i + 1u < cap) {
            dst[i] = msg[i];
            ++i;
        }
    dst[i] = 0;
}
