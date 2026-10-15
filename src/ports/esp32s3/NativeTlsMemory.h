#pragma once
// Process-wide mbedTLS policy, checked before bootstrap/apps can open HTTPS.
// IDF4 installs its hook after PSRAM startup; IDF6 uses the SDK startup policy.
// Never swap allocators around live TLS or PSA objects.
#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif
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
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 6
  // IDF6 initializes PSA crypto before app_main. Its allocator must therefore
  // be PSRAM-only from system startup, not replaced while PSA objects exist.
  // The selected SDK's esp_mem.c implements this policy without any fallback.
#if !defined(CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC) || !CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC || \
    defined(CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC) || defined(CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC) || \
    defined(CONFIG_MBEDTLS_IRAM_8BIT_MEM_ALLOC)
  return false;
#endif
#if !defined(MBEDTLS_PLATFORM_MEMORY) || defined(MBEDTLS_PLATFORM_CALLOC_MACRO) || defined(MBEDTLS_PLATFORM_FREE_MACRO)
  return false; // An alternate compile-time allocator is not this policy.
#endif
#else
  if(mbedtls_platform_set_calloc_free(calloc,free)!=0)return false;
#endif
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
