#pragma once
#include <RiscBoundAppDataV1.h>
#define RISC_BOUND_APP_DATA_ASYNC_TAG UINT32_C(0x42444131)
/* Optional suffix on the exact bound file capability. Submission copies the
 * entire bounded payload into native-owned memory. No provider callback or
 * borrowed buffer runs on the worker. Namespace/name/access checks happen on
 * the owner task before submission. Keep the provider grant alive and collect
 * to completion before teardown, even when navigation cancels the request.
 * A submitted atomic write cannot be rolled back by abandoning its token.
 * collect returns BUSY while pending, OK with the final replace status, or
 * CONTEXT for an invalid token. No implicit retry on ambiguous commit. */
typedef struct {
 risc_bound_app_data_v1 base;
 uint32_t async_tag,async_version;
 int32_t (*begin_replace)(void*,const char*,uint64_t,const void*,uint32_t,uint64_t*);
 int32_t (*collect_replace)(void*,uint64_t,int32_t*);
} risc_bound_app_data_async_v1;
static inline const risc_bound_app_data_async_v1*risc_bound_app_data_async(const risc_bound_app_data_v1*base){
 if(!base||base->struct_size<sizeof(risc_bound_app_data_async_v1))return NULL;
 const risc_bound_app_data_async_v1*a=(const risc_bound_app_data_async_v1*)base;
 return a->async_tag==RISC_BOUND_APP_DATA_ASYNC_TAG&&a->async_version==1&&a->begin_replace&&a->collect_replace?a:NULL;
}
