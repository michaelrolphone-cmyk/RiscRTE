#include "NativeSettingsPayloadAllocator.h"
#if defined(ESP_PLATFORM) && RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD
#include <esp_heap_caps.h>
#include "NativeMemory.h"
#include <cstdint>
#include <new>
namespace RiscCpu { namespace NativeSettingsPayloadAllocator { namespace {
using Core=RiscExperimental::NativeReplicaCore;
struct alignas(Alignment) Header {void*allocation;size_t payloadBytes;size_t allocationBytes;};
static_assert((Alignment&(Alignment-1))==0,"Payload alignment must be a power of two");
static_assert(sizeof(Header)%Alignment==0,"Payload header must preserve alignment");
constexpr size_t Overhead=sizeof(Header)+Alignment-1;
bool external(const void*p,size_t n){return p&&n&&uintptr_t(p)<=UINTPTR_MAX-(n-1)&&esp_ptr_external_ram(p)&&esp_ptr_external_ram(static_cast<const uint8_t*>(p)+n-1);}
void*allocate(void*,size_t bytes){
 if(bytes>SIZE_MAX-Overhead)return nullptr;
 const size_t total=bytes+Overhead;
 void*raw=heap_caps_malloc(total,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!raw)return nullptr;
 // A capability request is necessary but a non-PSRAM result is still refused.
 // Nothing from the caller has been copied into a rejected allocation.
 if(uintptr_t(raw)>UINTPTR_MAX-Overhead||!external(raw,total)){heap_caps_free(raw);return nullptr;}
 const uintptr_t aligned=(uintptr_t(raw)+sizeof(Header)+Alignment-1)&~uintptr_t(Alignment-1);
 new(reinterpret_cast<void*>(aligned-sizeof(Header)))Header{raw,bytes,total};
 return reinterpret_cast<void*>(aligned);
}
void release(void*,void*payload){
 if(!payload)return;
 auto*header=reinterpret_cast<Header*>(static_cast<uint8_t*>(payload)-sizeof(Header));
 void*raw=header->allocation;const size_t bytes=header->allocationBytes;
 // Wipe payload, header and all alignment slack before releasing PSRAM. Saved
 // scalar locals are needed because wiping invalidates the allocation header.
 Core::wipe(raw,bytes);heap_caps_free(raw);
}
} // namespace
Core::Allocator allocator(){return {nullptr,allocate,release};}
} }
#endif
