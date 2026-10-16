#include <RiscPreferenceStoreV1.h>
#include <RiscStorageFilesV1.h>
#include <RiscStorageFilesIdentityV1.h>
#include <RiscStorageFilesDirectoryV1.h>
#include <RiscStorageFilesCatalogV1.h>
#include "bootstrap/PreferenceStoreConfig.h"
#include "bootstrap/StorageFilesConfig.h"
#include <stddef.h>
#ifdef __cplusplus
#define STATIC_ASSERT static_assert
#else
#define STATIC_ASSERT _Static_assert
#endif
STATIC_ASSERT(sizeof(risc_preference_store_v1)==sizeof(risc_key_value_atomic_v1), "shared operation layout");
STATIC_ASSERT(RISC_PREFERENCE_STORE_COPIED_INPUT==4u, "copied input is independent of atomicity");
STATIC_ASSERT(RISC_PREFERENCE_STORE_ATOMIC_GROUP==8u, "atomic group is explicit");
STATIC_ASSERT(offsetof(risc_storage_files_v1,api_version)==0, "file ABI prefix");
STATIC_ASSERT(offsetof(risc_storage_files_catalog_v1,api_version)==0, "catalog ABI prefix");
STATIC_ASSERT(sizeof(risc_storage_files_identity_value_v1)==72, "opaque identity value layout");
STATIC_ASSERT(offsetof(risc_storage_files_identity_v1,api_version)==0, "identity ABI prefix");
STATIC_ASSERT(offsetof(risc_storage_files_directory_v1,api_version)==0, "directory supplement ABI prefix");
STATIC_ASSERT(sizeof(((risc_storage_files_entry_v1*)0)->name)==128, "published fixed entry ABI unchanged");
STATIC_ASSERT(sizeof(risc_storage_files_handle_v1)==4, "opaque token width");
STATIC_ASSERT(sizeof(((risc_storage_files_namespace_v1*)0)->namespace_id)==8, "opaque namespace width");
STATIC_ASSERT(sizeof(((risc_storage_files_namespace_v1*)0)->label)==RISC_STORAGE_FILES_LABEL_MAX, "catalog label layout");
STATIC_ASSERT(RISC_FILES_RETAINED!=RISC_FILES_IO && RISC_FILES_NOT_FOUND!=RISC_FILES_UNAVAILABLE, "error custody distinctions");
#ifndef STORAGE_CONTRACT_OPT_IN
STATIC_ASSERT(RISC_RUNTIME_PREFERENCE_STORE==0 && RISC_RUNTIME_STORAGE_FILES==0, "storage is optional");
#else
STATIC_ASSERT(RISC_RUNTIME_PREFERENCE_STORE==1 && RISC_RUNTIME_STORAGE_FILES==1, "explicit opt in");
#endif
int main(void) { return 0; }
