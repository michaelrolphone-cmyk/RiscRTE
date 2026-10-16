#pragma once
#include "RiscStorageFilesV1.h"
/* Optional storage.files.directory@1 supplement. Context and directory tokens
 * belong to the matching storage.files@1 provider/grant. This avoids imposing
 * its fixed entry-name buffer on providers with longer valid namespace names.
 * next returns the same metadata and END/custody semantics as dir_next.
 * required includes the terminating NUL on OK or CAPACITY. CAPACITY does not
 * consume the entry; retry with a sufficient caller-owned buffer. A zero
 * capacity/null buffer is a size probe. Other failures set required to zero.
 * Names are valid relative namespace names; flat providers may return slashes.
 * No truncation, backend identifier or physical path is exposed. */
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STORAGE_FILES_DIRECTORY_CAPABILITY "storage.files.directory"
#define RISC_STORAGE_FILES_DIRECTORY_API_V1 1u
enum {RISC_FILES_DIRECTORY_CAPACITY=-8};
typedef struct {
 uint32_t api_version,struct_size;void* context;
 int32_t (*next)(void*,risc_storage_files_handle_v1,risc_storage_files_stat_v1*,char*,uint32_t,uint32_t*);
} risc_storage_files_directory_v1;
#ifdef __cplusplus
}
#endif
