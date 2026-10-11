#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "RuntimeImageCacheConfig.h"

#if RISC_APP_IMAGE_CACHE
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __APPLE__
extern bool risc_runtime_reclaim_app_images(void) __attribute__((weak_import));
#else
extern bool risc_runtime_reclaim_app_images(void) __attribute__((weak));
#endif
#ifdef __cplusplus
}
#endif
#endif

static inline bool risc_image_pressure_reclaim(void) {
#if RISC_APP_IMAGE_CACHE
    return risc_runtime_reclaim_app_images && risc_runtime_reclaim_app_images();
#else
    return false;
#endif
}

/* Retry only a real allocation failure. In particular, realloc(p,0) can free
 * p and return NULL successfully; retrying it would double-free that pointer. */
static inline void *risc_image_malloc(size_t size) {
    void *p=malloc(size);
    if(!p && size && risc_image_pressure_reclaim())p=malloc(size);
    return p;
}
static inline void *risc_image_calloc(size_t count,size_t size) {
    void *p=calloc(count,size);
    if(!p && count && size && count<=SIZE_MAX/size && risc_image_pressure_reclaim())p=calloc(count,size);
    return p;
}
static inline void *risc_image_realloc(void *pointer,size_t size) {
    void *p=realloc(pointer,size);
    if(!p && size && risc_image_pressure_reclaim())p=realloc(pointer,size);
    return p;
}
