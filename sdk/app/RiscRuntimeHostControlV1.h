#pragma once
#include "RiscRuntimeV1.h"
#include <string.h>
/* Read-only append-only suffix: no write, acknowledgement or clear. Keep
 * historical SDK prefixes intact and avoid aliasing a different struct. */
static inline bool risc_runtime_exit_requested_v1(const risc_runtime_api_v1 *api){
#if defined(RISC_RUNTIME_EXIT_REQUESTED_V1_SIZE)
  return api && api->struct_size>=RISC_RUNTIME_EXIT_REQUESTED_V1_SIZE &&
         api->exit_requested && api->exit_requested();
#elif defined(RISC_RUNTIME_DIAGNOSTIC_CHECKPOINT_V1_SIZE)
  bool (*request)(void)=NULL;
  const size_t offset=RISC_RUNTIME_DIAGNOSTIC_CHECKPOINT_V1_SIZE+sizeof(bool(*)(uint32_t*));
  if(!api||api->struct_size<offset+sizeof(request))return false;
  memcpy(&request,(const unsigned char*)api+offset,sizeof(request));
  return request&&request();
#else
  (void)api;return false;
#endif
}
