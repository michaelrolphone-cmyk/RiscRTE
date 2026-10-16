#pragma once
#include "RiscStorageFilesV1.h"
/* Read-only discovery of file namespaces authorized to this exact invocation.
 * Enumeration never opens/activates a backend or probes its availability.
 * namespace_id is an opaque positive service ID, unrelated to physical device
 * IDs; acquire storage.files@1 with that ID. Instance 0 selects only a unique
 * authorized namespace. label is provisioned presentation text, never a type,
 * path, mount, transport, driver identity or authorization discriminator.
 * Snapshot revision and indexes belong to this grant. A changed snapshot or
 * foreign revision returns STALE, out-of-range index NOT_FOUND. No live backend
 * objects are borrowed. Unavailable namespaces remain discoverable so clients
 * can present a clean operation error instead of silently changing destinations.
 */
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STORAGE_FILES_CATALOG_CAPABILITY "storage.files.catalog"
#define RISC_STORAGE_FILES_CATALOG_API_V1 1u
#define RISC_STORAGE_FILES_LABEL_MAX 64u
#define RISC_FILES_STALE (-8)
typedef struct {uint32_t struct_size,count;uint64_t revision;} risc_storage_files_catalog_snapshot_v1;
typedef struct {uint32_t struct_size,reserved;uint64_t namespace_id;char label[RISC_STORAGE_FILES_LABEL_MAX];} risc_storage_files_namespace_v1;
typedef struct {
 uint32_t api_version,struct_size;void *context;
 int32_t (*snapshot)(void*,risc_storage_files_catalog_snapshot_v1*);
 int32_t (*get)(void*,uint64_t,uint32_t,risc_storage_files_namespace_v1*);
} risc_storage_files_catalog_v1;
#ifdef __cplusplus
}
#endif
