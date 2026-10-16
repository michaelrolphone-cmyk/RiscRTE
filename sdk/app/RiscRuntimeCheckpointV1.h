#pragma once
#include "RiscRuntimeV1.h"
/* Optional service support: absence on an older Runtime is a safe no-op. */
static inline int32_t risc_runtime_service_checkpoint_v1(const risc_runtime_api_v1* api) {
  if(!api || api->api_version!=RISC_RUNTIME_API_V1)return RISC_RUNTIME_CHECKPOINT_CONTEXT;
  if(api->struct_size<RISC_RUNTIME_SERVICE_CHECKPOINT_V1_SIZE || !api->service_checkpoint)
    return RISC_RUNTIME_CHECKPOINT_OK;
  return api->service_checkpoint();
}
