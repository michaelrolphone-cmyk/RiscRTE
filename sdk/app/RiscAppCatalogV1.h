#pragma once
/* Read-only admitted installed-app inventory. The native owner discovers and
 * admits packages; callers cannot select directories, paths or grant policy.
 * Entries explicitly marked launcher_visible:false in their installed manifest
 * are omitted; visibility never changes their native admission or authority. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_APP_CATALOG_CAPABILITY "app.catalog"
#define RISC_APP_CATALOG_API_V1 1u
#define RISC_APP_CATALOG_MAX 128u
#define RISC_APP_CATALOG_ROUTE_MAX 256u
#define RISC_APP_CATALOG_ID_MAX 96u
#define RISC_APP_CATALOG_OK 0
#define RISC_APP_CATALOG_NOT_FOUND 1
#define RISC_APP_CATALOG_INVALID (-1)
#define RISC_APP_CATALOG_UNAVAILABLE (-2)
#define RISC_APP_CATALOG_STALE (-3)
#define RISC_APP_CATALOG_CONTEXT (-4)
#define RISC_APP_CATALOG_RETAINED (-5)
#define RISC_APP_CATALOG_AVAILABLE 1u
typedef struct {
    uint32_t struct_size, count;
    /* Nonzero publication identity. Changes whenever entries, ordering,
     * descriptive metadata or availability change. Never reused in a grant. */
    uint64_t generation;
} risc_app_catalog_snapshot_v1;
typedef struct {
    uint32_t struct_size, flags;
    /* All strings are NUL-terminated, never truncated. file_name is the native
     * launch identity, relative to its authority root, not an arbitrary path. */
    char display_name[96], file_name[RISC_APP_CATALOG_ROUTE_MAX], min_firmware_version[32], icon[24];
    /* Native admitted identity. Legacy UI bridges may use this exact key when
     * the canonical launch route exceeds their historical filename buffer. */
    char id[RISC_APP_CATALOG_ID_MAX];
} risc_app_catalog_entry_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    /* Copies the current native publication; does not initiate media scanning.
     * Empty successful inventories are valid. Failure never fabricates rows. */
    int32_t (*snapshot)(void *, risc_app_catalog_snapshot_v1 *);
    /* Returns STALE when the requested publication changed. An unavailable
     * admitted entry may be returned with AVAILABLE clear; absent, rejected or
     * malformed packages must not become synthetic entries. */
    int32_t (*get)(void *, uint64_t generation, uint32_t index,
                   risc_app_catalog_entry_v1 *);
} risc_app_catalog_v1;
/* The standard Runtime acquire/release owns this read-only grant. CONTEXT or
 * RETAINED is a terminal custody failure: no further callbacks or release.
 * INVALID/UNAVAILABLE/STALE are ordinary failures with releasable custody. */
#ifdef __cplusplus
}
#endif
