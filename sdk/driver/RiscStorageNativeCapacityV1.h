#pragma once
#include <RiscStorageSessionAdmissionV1.h>
/* Explicit native-only opt-in suffix. This is not an app capability. All older
 * volume/files/export/async/package/download/admission layouts stay frozen.
 * The native caller holds the selected attempt, provider and all other-writer
 * exclusion continuously from scan through first export. A receipt is a
 * conservative reservation under that exclusion, never a reusable quota. */
#define RISC_STORAGE_NATIVE_CAPACITY_TAG UINT32_C(0x4e434131)
#define RISC_STORAGE_NATIVE_CAPACITY_VERSION 1u
#define RISC_STORAGE_NATIVE_CAPACITY_NO_SPACE RISC_STORAGE_ASYNC_END
typedef struct {
 uint32_t tag,version,map_bytes,pages;
} risc_storage_native_capacity_request_v1;
typedef struct {
 uint32_t tag,version,cluster_bytes,required_clusters,free_clusters,reserved;
 uint64_t mount_epoch,serial;
} risc_storage_native_capacity_result_v1;
/* Each page is an independently allocated file. Keep simultaneous index,
 * temporary and recovery copies, long-name directory entries (9/page), slack
 * for auxiliary entries, and root/parent/new-directory growth. Overestimation
 * is intentional; existing partial files never reduce this admission bound. */
static inline uint32_t risc_storage_native_capacity_required(uint32_t bytes,uint32_t pages,uint32_t cluster){
 if(bytes>65512u||pages!=(bytes+4095u)/4096u||!cluster||cluster<512u||cluster>65536u||(cluster&(cluster-1u)))return 0;
 uint32_t n=0,left=bytes;
 for(uint32_t i=0;i<pages;++i){uint32_t part=left>4096u?4096u:left;n+=(part+cluster-1u)/cluster;left-=part;}
 n+=3u*((128u+cluster-1u)/cluster);
 n+=((pages*9u+32u)*32u+cluster-1u)/cluster;
 return n+3u;
}
typedef struct {
 risc_storage_volume_api_v1_admission base;
 uint32_t capacity_tag,capacity_version;
 /* Copied, worker-only fresh FAT scan. collect uses the unchanged async result
  * and returns this exact result in data, or END for insufficient free space.
  * Pending/cancel/retention use the existing operation-custody protocol. */
 int32_t(*submit_capacity)(void*,uint64_t,const risc_storage_native_capacity_request_v1*,uint64_t*);
 /* Owner-only metadata check after checked end. No I/O, remount or lock: exact
  * latest receipt, same mounted media epoch, no live operation/handles, healthy
  * custody. A later scan invalidates the earlier serial. */
 int32_t(*check_capacity)(void*,const risc_storage_native_capacity_result_v1*);
} risc_storage_volume_api_v1_native_capacity;
static inline const risc_storage_volume_api_v1_native_capacity *risc_storage_volume_native_capacity(const risc_storage_volume_api_v1*v){
 if(!risc_storage_volume_session_admission(v)||v->struct_size<sizeof(risc_storage_volume_api_v1_native_capacity))return NULL;
 const risc_storage_volume_api_v1_native_capacity*a=(const risc_storage_volume_api_v1_native_capacity*)v;
 return a->capacity_tag==RISC_STORAGE_NATIVE_CAPACITY_TAG&&a->capacity_version==1u&&a->submit_capacity&&a->check_capacity?a:NULL;
}
