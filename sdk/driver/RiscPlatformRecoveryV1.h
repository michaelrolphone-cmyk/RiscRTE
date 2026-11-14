#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit product-selected supervision. No storage/config erase or cleanup.
 * Feed only from the owning execution loop; blocking SDK calls must not feed. */
typedef struct { uint32_t struct_size,reset_reason,boot_count,free_heap,min_free_heap,largest_free_block; int32_t wifi_rssi; uint32_t boot_count_valid; } risc_recovery_snapshot_v1;
typedef struct { uint32_t api_version,struct_size; void* context;
 int32_t (*feed)(void*);
 int32_t (*snapshot)(void*,risc_recovery_snapshot_v1*);
 void (*restart)(void*);
} risc_platform_recovery_v1;
#ifdef __cplusplus
}
#endif
