/* Runtime-owned immutable installed-image bytes. Not an ELF import/API. */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_elf_image.h"
#ifdef __cplusplus
extern "C" {
#endif

#define ESP_DL_IMAGE_CACHE_ENTRIES 4
#define ESP_DL_IMAGE_CACHE_BYTES (1024u * 1024u)
typedef struct esp_dl_image_cache esp_dl_image_cache;

/* One serialized owner and one immutable installed-store session per cache.
 * Supply only exact prepared default/app-policy paths. Destroy before replacing
 * that store or ending its session. No stat/hash/freshness probes are performed.
 * Allocation failure returns NULL; opening with NULL preserves uncached loading.
 * Memory pressure releases entries and the control block and clears *cache.
 * Mappings own all relocated sections and symbol names, never these raw bytes;
 * destroying this cache is safe even when a failed app must remain mapped. */
esp_dl_image_cache *esp_dl_image_cache_create(void);
void esp_dl_image_cache_destroy(esp_dl_image_cache *cache);
/* Detach first, then free. Does not touch an in-progress detached file or any
 * mapping; owner-only callers serialize this with cached opens. */
bool esp_dl_image_cache_reclaim(esp_dl_image_cache **cache);
/* Mutable-source snapshots are always reread and never cached. */
void *esp_dlopen_reader_instance(const char*,esp_dl_image_reader,void*);
/* Immutable-session variant: the same exact-path/session contract above applies
 * to the supplied reader/context. A hit transfers owned bytes without calling
 * the reader; a miss or allocation-pressure retry uses only that reader, never
 * the POSIX loader. NULL cache preserves uncached reader loading; NULL reader
 * is invalid even on a hit. Destroy the cache before changing its source. */
void *esp_dlopen_cached_reader_instance(esp_dl_image_cache **cache, const char *path,
                                        esp_dl_image_reader reader, void *context);
void *esp_dlopen_cached_instance(esp_dl_image_cache **cache, const char *path);

#ifdef __cplusplus
}
#endif
