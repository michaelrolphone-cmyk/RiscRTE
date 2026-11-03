#pragma once
#include <RiscStorageVolumeAsyncV1.h>
#include <string.h>
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
 return d->download_tag==RISC_STORAGE_DOWNLOAD_TAG&&(d->download_version>=1&&d->download_version<=3)?d:NULL;
}

/* Download v3: bounded copied random read. Only READ_RANGE interprets the
 * first eight sha256 bytes as a native uint64_t offset (memcpy, no alignment
 * assumption); remaining digest bytes are zero. Other opcodes are unchanged.
 * Exactly size bytes (1..4096) are returned or size zero on failure. Each
 * request opens/seeks/reads/closes on the worker; session custody prevents
 * concurrent volume mutation. Archive-size policy belongs to the caller. */
#define RISC_STORAGE_PACKAGE_READ_RANGE 110u
static inline const risc_storage_volume_api_v1_download*risc_storage_volume_range(const risc_storage_volume_api_v1*v){
 const risc_storage_volume_api_v1_download*d=risc_storage_volume_download(v);
 return d&&d->download_version==3?d:NULL;
}
static inline bool risc_storage_package_read_range(risc_storage_package_request_v1*q,const char*path,uint64_t offset,uint32_t size){
 if(!q||!path||!size||size>4096)return false;
 size_t n=0;while(n<sizeof(q->path)&&path[n])++n;
 if(!n||n==sizeof(q->path))return false;
 memset(q,0,sizeof(*q));q->operation=RISC_STORAGE_PACKAGE_READ_RANGE;
 memcpy(q->path,path,n+1);q->size=size;memcpy(q->sha256,&offset,sizeof(offset));return true;
}

/* Append-at opens an existing regular file, verifies its exact current length,
 * seeks to that offset, then writes, syncs and checked-closes <=4096 bytes.
 * Offset zero creates exclusively. Failure can leave a partial stage: reconcile
 * the durable transaction, never blindly replay an ambiguous append. */
#define RISC_STORAGE_PACKAGE_APPEND_AT 111u
#define RISC_STORAGE_PACKAGE_REMOVE_DIRECTORY 112u
static inline bool risc_storage_package_append_at(risc_storage_package_request_v1*q,const char*path,uint64_t offset,const void*bytes,uint32_t size){
 if(!bytes||!risc_storage_package_read_range(q,path,offset,size))return false;
 q->operation=RISC_STORAGE_PACKAGE_APPEND_AT;memcpy(q->data,bytes,size);return true;
}

/* Exact size/SHA only; never ELF/import admission or execution. */
#define RISC_STORAGE_PACKAGE_VALIDATE_DATA 113u
