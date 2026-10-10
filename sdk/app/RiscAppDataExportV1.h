#pragma once
#include <RiscAppDataV1.h>
#include <RiscStorageVolumeV1.h>
#define RISC_APP_DATA_EXPORT_CAPABILITY "storage.app-data.export"
#define RISC_APP_DATA_EXPORT_API_V1 1u
#define RISC_APP_DATA_EXPORT_TAG 0x41444531u
#define RISC_APP_DATA_EXPORT_PATH_MAX 191u
typedef struct {
    char path[RISC_APP_DATA_EXPORT_PATH_MAX+1u];
    uint8_t writable;
    uint8_t reserved[3];
} risc_app_data_export_entry_v1;
/* Explicit configured projection of existing app-data files. The volume prefix
 * supports virtual directory enumeration, snapshot reads and exclusive creates.
 * No remove, rename, mkdir, power or sleep operations are advertised. The full
 * known volume suffix is reserved/zero so generic extension probes never mistake
 * the revision callbacks for power callbacks. Revision operations use existing
 * RiscAppDataV1 status, size, atomicity and mount-session token semantics.
 * Paths are logical configured names, never native paths or namespace numbers.
 */
typedef struct {
    risc_storage_volume_api_v1_sleep volume;
    uint32_t export_tag, export_version;
    int32_t (*stat_revision)(void*,const char*,uint32_t*,uint64_t*);
    int32_t (*read_revision)(void*,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
    int32_t (*replace_revision)(void*,const char*,uint64_t,const void*,uint32_t);
    /* PENDING (1), last failed commit status (<0), or CONTEXT for other handles.
     * A failed non-retained writer can be cancelled with file_close(false).
     * Cancellation releases memory only; COMMIT_UNKNOWN never deletes a possibly
     * published file. Retrying file_close(true) never repeats replacement. */
    int32_t (*write_status)(void*,risc_storage_file_t);
    /* Optional copied policy metadata, with no filesystem access. OK returns
     * one configured logical path and access bit; NOT_FOUND ends enumeration.
     * Includes configured files that do not exist yet. Native names/namespaces
     * and owner identities are never returned. CONTEXT/RETAINED fence use.
     * The index is stable only for this live export invocation. */
    int32_t (*entry)(void*,uint32_t,risc_app_data_export_entry_v1*);
} risc_app_data_export_v1;
#define RISC_APP_DATA_EXPORT_WRITE_PENDING 1
static inline const risc_app_data_export_v1 *risc_app_data_export(
    const risc_storage_volume_api_v1 *api) {
    if(!api || api->api_version!=1 || api->struct_size<offsetof(risc_app_data_export_v1,entry))return NULL;
    const risc_app_data_export_v1 *e=(const risc_app_data_export_v1 *)api;
    return e->export_tag==RISC_APP_DATA_EXPORT_TAG && e->export_version==1 ? e : NULL;
}
static inline const risc_app_data_export_v1 *risc_app_data_export_catalog(
    const risc_storage_volume_api_v1 *api) {
    const risc_app_data_export_v1 *e=risc_app_data_export(api);
    return e && api->struct_size>=sizeof(*e) && e->entry ? e : NULL;
}
