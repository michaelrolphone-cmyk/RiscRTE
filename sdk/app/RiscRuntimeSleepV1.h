#pragma once
#include "RiscRuntimeV1.h"
/* Historical Runtime tables have no native-file sleep transaction. Preserve
 * their original sleep path without reading beyond the declared prefix. */
static inline int32_t risc_runtime_sleep_prepare_v1(const risc_runtime_api_v1 *api,uint64_t *token) {
  if(token)*token=0;
  if(!api || api->api_version!=RISC_RUNTIME_API_V1 || !token)return RISC_RUNTIME_SLEEP_CONTEXT;
  if(api->struct_size<RISC_RUNTIME_SLEEP_V1_SIZE)return RISC_RUNTIME_SLEEP_OK;
  if(!api->sleep_prepare || !api->sleep_resume)return RISC_RUNTIME_SLEEP_UNSUPPORTED;
  return api->sleep_prepare(token);
}
static inline int32_t risc_runtime_sleep_resume_v1(const risc_runtime_api_v1 *api,uint64_t token) {
  if(!api || api->api_version!=RISC_RUNTIME_API_V1)return RISC_RUNTIME_SLEEP_CONTEXT;
  if(!token)return RISC_RUNTIME_SLEEP_OK;
  if(api->struct_size<RISC_RUNTIME_SLEEP_V1_SIZE || !api->sleep_resume)return RISC_RUNTIME_SLEEP_UNSUPPORTED;
  return api->sleep_resume(token);
}
