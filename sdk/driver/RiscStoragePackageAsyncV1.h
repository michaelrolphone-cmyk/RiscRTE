#pragma once
#include <RiscStorageVolumeAsyncV1.h>
#define RISC_STORAGE_PACKAGE_TAG UINT32_C(0x53504b31)
#define RISC_STORAGE_PACKAGE_VALIDATE 100u
#define RISC_STORAGE_PACKAGE_WRITE_SMALL 101u
#define RISC_STORAGE_PACKAGE_REPLACE 102u
#define RISC_STORAGE_PACKAGE_RECOVER 103u
/* Separate copied request. The v2/v3 request layout and submit entry are frozen.
 * All operands are closed ordinary files. WRITE_SMALL creates exclusively and
 * syncs/closes <=4096 bytes. REPLACE/RECOVER use the existing filesystem journal.
 * VALIDATE reads exactly size bytes, checks SHA and native app ELF/imports once;
 * it never executes code. Caller still owns manifest/capability admission.
 * A failed mutation may have taken effect: reconcile from durable intent. */
typedef struct {
 uint32_t operation;char path[205],destination[205];
 uint32_t size;uint8_t sha256[32],data[4096];
} risc_storage_package_request_v1;
typedef struct {
 risc_storage_volume_api_v1_async base;
 uint32_t package_tag,package_version;
 int32_t (*submit_package)(void*,uint64_t,const risc_storage_package_request_v1*,uint64_t*);
} risc_storage_volume_api_v1_packages;
static inline const risc_storage_volume_api_v1_packages*risc_storage_volume_packages(const risc_storage_volume_api_v1*v){
 if(!risc_storage_volume_async(v)||v->struct_size<sizeof(risc_storage_volume_api_v1_packages))return NULL;
 const risc_storage_volume_api_v1_packages*p=(const risc_storage_volume_api_v1_packages*)v;
 return p->package_tag==RISC_STORAGE_PACKAGE_TAG&&p->package_version==1&&p->submit_package?p:NULL;
}

/* Additive download suffix; v1 callers retain their original exact prefix. */
#define RISC_STORAGE_DOWNLOAD_TAG UINT32_C(0x53444c31)
#define RISC_STORAGE_PACKAGE_OPEN_WRITE 104u
#define RISC_STORAGE_PACKAGE_WRITE_CHUNK 105u
#define RISC_STORAGE_PACKAGE_CLOSE_WRITE 106u
#define RISC_STORAGE_PACKAGE_VALIDATE_DRIVER 107u
#define RISC_STORAGE_PACKAGE_MKDIR 108u
typedef struct {
 risc_storage_volume_api_v1_packages base;
 uint32_t download_tag,download_version;
} risc_storage_volume_api_v1_download;
static inline const risc_storage_volume_api_v1_download*risc_storage_volume_download(const risc_storage_volume_api_v1*v){
 if(!risc_storage_volume_packages(v)||v->struct_size<sizeof(risc_storage_volume_api_v1_download))return NULL;
 const risc_storage_volume_api_v1_download*d=(const risc_storage_volume_api_v1_download*)v;
 return d->download_tag==RISC_STORAGE_DOWNLOAD_TAG&&d->download_version==1?d:NULL;
}
