#pragma once
/* Composition-only declarations; no production header is replaced in builds. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t esp_get_free_heap_size(void);
#ifdef __cplusplus
}
#endif
