#pragma once
#include <RiscStorageWorkerV1.h>
#include <RiscStorageVolumeV1.h>
#define RISC_STORAGE_ADMISSION_TAG UINT32_C(0x53414431)
/* Optional worker-only suffix. No owner-task privilege, pointer retention, bank
 * writes or relocation. Caller owns the volume and scope for the whole call.
 * Validate the actual selected image snapshot once during installation. A
 * failed close returns RETAINED with the live handle; never unload its provider.
 * Result OK means exact length/SHA plus ordinary application ELF/import audit.
 * This is integrity/admission, not publisher authentication. */
typedef struct {
 risc_storage_worker_v1 base;
 uint32_t admission_tag,admission_version;
 int32_t (*application)(void*,const risc_storage_volume_api_v1*,const char*path,
                      uint32_t size,const uint8_t sha256[32],uint32_t*retained_handle);
} risc_storage_worker_admission_v1;
static inline const risc_storage_worker_admission_v1*risc_storage_worker_admission(const risc_storage_worker_v1*v){
 if(!v||v->struct_size<sizeof(risc_storage_worker_admission_v1))return NULL;
 const risc_storage_worker_admission_v1*a=(const risc_storage_worker_admission_v1*)v;
 return a->admission_tag==RISC_STORAGE_ADMISSION_TAG&&a->admission_version==1&&a->application?a:NULL;
}
