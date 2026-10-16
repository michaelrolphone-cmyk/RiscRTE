#pragma once
/* Optional provider-owned identity supplement for storage.files@1.
 * Values are opaque, canonical equality evidence scoped to this exact provider
 * binding. They are not portable paths, credentials, hashes or durability claims.
 * A provider must preserve its native change/replacement detection; it must
 * report UNAVAILABLE rather than substitute size alone for supported identity.
 * Identity does not freeze a file. Consumers compare path and open-handle
 * identities before/after bounded reads and publish data only after clean close.
 * The context and raw handle domain are the associated storage.files table's.
 * All storage.files status and terminal custody rules apply unchanged.
 */
#include <RiscStorageFilesV1.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STORAGE_FILES_IDENTITY_CAPABILITY "storage.files.identity"
#define RISC_STORAGE_FILES_IDENTITY_API_V1 1u
typedef struct {
 uint32_t struct_size,reserved;
 uint64_t size;
 uint64_t opaque[7];
} risc_storage_files_identity_value_v1;
typedef struct {
 uint32_t api_version,struct_size;
 void* context;
 int32_t (*path)(void*,const char*,risc_storage_files_identity_value_v1*);
 int32_t (*opened)(void*,risc_storage_files_handle_v1,risc_storage_files_identity_value_v1*);
} risc_storage_files_identity_v1;
#ifdef __cplusplus
}
#endif
