#pragma once
#include <RiscKeyValueV1.h>
#include "runtime/storage/ScalableSettings.h"
namespace RiscBoot {
// Optional compiled-in backend. Namespace comes only from validated boot policy.
// Backend must obey the public status/size contract; broker copies reads only
// after success. No app value encoding or fallback policy belongs here.
struct KeyValueBackend {
  void* context;
  int32_t (*get)(void*,uint32_t,const char*,void*,uint32_t,uint32_t*);
  int32_t (*put)(void*,uint32_t,const char*,const void*,uint32_t);
  uint32_t maxBlobSize=RISC_KEY_VALUE_BLOB_MAX;
  int32_t (*journalRead)(void*,uint32_t,void*,uint32_t,uint32_t*)=nullptr;
  int32_t (*journalWrite)(void*,uint32_t,const void*,uint32_t)=nullptr;
  int32_t (*legacyNamespace)(void*,uint32_t,bool*)=nullptr;
  const RiscStorage::ScalableSettings::Backend* scalableSettings=nullptr;
};
}
