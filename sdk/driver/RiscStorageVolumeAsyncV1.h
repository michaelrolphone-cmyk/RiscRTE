#pragma once
#include <RiscStorageVolumeFsV1.h>
#define RISC_STORAGE_ASYNC_VERSION 2u
#define RISC_STORAGE_ASYNC_TAG UINT32_C(0x41535631)
enum {RISC_STORAGE_ASYNC_OK=0,RISC_STORAGE_ASYNC_PENDING=1,RISC_STORAGE_ASYNC_END=2,
 RISC_STORAGE_ASYNC_NOT_FOUND=3,RISC_STORAGE_ASYNC_INVALID=-1,RISC_STORAGE_ASYNC_BUSY=-2,
 RISC_STORAGE_ASYNC_IO=-3,RISC_STORAGE_ASYNC_RETAINED=-4};
enum {RISC_STORAGE_ASYNC_REFRESH=1,RISC_STORAGE_ASYNC_OPEN_DIRECTORY=2,
 RISC_STORAGE_ASYNC_NEXT_ENTRY=3,RISC_STORAGE_ASYNC_READ_SMALL_FILE=4,
 RISC_STORAGE_ASYNC_CLOSE_DIRECTORY=5,RISC_STORAGE_ASYNC_RENAME=6};
typedef struct {uint32_t operation;char path[205],destination[205];} risc_storage_async_request_v1;
typedef struct {
 int32_t status;uint32_t size;risc_storage_dirent_v1 entry;
 uint8_t data[4096];
} risc_storage_async_result_v1;
typedef struct {
 risc_storage_volume_api_v1_fs fs;
 uint32_t async_tag,async_version;
 /* One exclusive copied-request session. begin never touches media. Existing
  * synchronous consumers return busy while an async operation is in flight.
  * Open directory belongs to this session; READ_SMALL closes its own file.
  * No app buffer/pointer is retained by submit or collect. */
 int32_t (*begin)(void*,uint64_t*session);
 int32_t (*submit)(void*,uint64_t session,const risc_storage_async_request_v1*,uint64_t*operation);
 int32_t (*collect)(void*,uint64_t session,uint64_t operation,risc_storage_async_result_v1*);
 /* Nonblocking cancel/drain and release session. PENDING retains all grants.
  * RETAINED means uncertain close: never release/unload. No app I/O after it. */
 int32_t (*end)(void*,uint64_t session);
} risc_storage_volume_api_v1_async;
static inline const risc_storage_volume_api_v1_async*risc_storage_volume_async(const risc_storage_volume_api_v1*base){
 if(!risc_storage_volume_fs(base)||base->struct_size<sizeof(risc_storage_volume_api_v1_async))return NULL;
 const risc_storage_volume_api_v1_async*api=(const risc_storage_volume_api_v1_async*)base;
 return api->async_tag==RISC_STORAGE_ASYNC_TAG&&api->async_version==RISC_STORAGE_ASYNC_VERSION&&api->begin&&api->submit&&api->collect&&api->end?api:NULL;
}
