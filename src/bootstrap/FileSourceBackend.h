#pragma once
#include <RiscFileOpenSourceV1.h>
namespace RiscBoot {
/* Trusted native temporary-storage contract. One staged source per Runtime.
 * No path, namespace or executable authority is accepted from an application.
 * Each dispatched operation must finish with checked native custody. BUSY means
 * no operation began. RETAINED forbids all later I/O or cleanup until reset. */
struct FileSourceBackend {
 void* context=nullptr;
 int32_t (*begin)(void*,uint64_t size)=nullptr;
 int32_t (*write)(void*,const void*,uint32_t,uint32_t*)=nullptr;
 int32_t (*finish)(void*)=nullptr;
 int32_t (*readAt)(void*,uint64_t,void*,uint32_t,uint32_t*)=nullptr;
 int32_t (*discard)(void*)=nullptr;
 bool (*exitSafe)(void*)=nullptr;
};
}
