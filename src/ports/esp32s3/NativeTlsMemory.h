#pragma once
// Process-wide mbedTLS policy, installed once after PSRAM startup and before
// bootstrap/apps can open HTTPS. Never swap allocators around a live session.
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <cstddef>
#include <cstdint>

namespace RiscCpu { namespace NativeTlsMemory {
constexpr unsigned InternalCaps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
constexpr unsigned ExternalCaps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
enum class State { Uninitialized,Ready,Unavailable };
inline State state=State::Uninitialized;

inline void* calloc(size_t count,size_t bytes){
  if(bytes && count>SIZE_MAX/bytes)return nullptr;
  // Match IDF's supported CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC policy, including
  // small certificate/crypto controls. Failure must never spill into internal
  // RAM. SDK AES/SHA DMA allocations keep their separate capability allocator.
  return heap_caps_calloc(count,bytes,ExternalCaps);
}
inline void free(void* memory){heap_caps_free(memory);}
inline bool ready(){return state==State::Ready;}
inline bool initialize(){
  if(state!=State::Uninitialized)return ready();
  state=State::Unavailable;
  if(!heap_caps_get_total_size(ExternalCaps))return false;
  if(mbedtls_platform_set_calloc_free(calloc,free)!=0)return false;
  state=State::Ready;
  return true;
}

// Probe the pinned S3 hardware crypto's real internal/DMA requirements instead
// of requiring a contiguous TLS-record-sized internal block. Keep both AES
// bounce buffers (1600 each) and descriptor/SHA workspace (256) allocated while
// ESP-TLS creates its real native handle. These are admission probes, not a
// promise that concurrent Wi-Fi allocations cannot exhaust memory later.
class DmaHeadroom {
  void* blocks[3]{};
public:
  DmaHeadroom(){
    constexpr size_t sizes[]={1600,1600,256};
    for(unsigned i=0;i<3;++i){
      blocks[i]=heap_caps_malloc(sizes[i],InternalCaps|MALLOC_CAP_DMA);
      if(!blocks[i])break;
    }
  }
  ~DmaHeadroom(){for(void* p:blocks)if(p)heap_caps_free(p);}
  DmaHeadroom(const DmaHeadroom&)=delete;
  DmaHeadroom& operator=(const DmaHeadroom&)=delete;
  explicit operator bool()const{return blocks[2]!=nullptr;}
};
} }
