#include "ports/esp32s3/NativeTlsMemory.h"
extern "C" {
#include <esp_mem.h>
}
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <iostream>
using namespace RiscCpu;
static bool psram=true,allocationFailure=false;
static unsigned externalCalls=0,dmaCalls=0;
static std::map<void*,size_t> live;
size_t heap_caps_get_total_size(unsigned caps){assert(caps==NativeTlsMemory::ExternalCaps);return psram?8*1024*1024:0;}
void* heap_caps_calloc(size_t n,size_t bytes,unsigned caps){
  assert(caps==NativeTlsMemory::ExternalCaps);++externalCalls;
  if(allocationFailure || (bytes&&n>SIZE_MAX/bytes))return nullptr;
  void* p=std::calloc(n,bytes);if(p)live[p]=n*bytes;return p;
}
void* heap_caps_malloc(size_t n,unsigned caps){
  assert(caps==(NativeTlsMemory::InternalCaps|MALLOC_CAP_DMA));++dmaCalls;
  if(allocationFailure)return nullptr;
  void* p=std::malloc(n);if(p)live[p]=n;return p;
}
void heap_caps_free(void* p){if(p)assert(live.erase(p)==1);std::free(p);}
int mbedtls_platform_set_calloc_free(void*(*)(size_t,size_t),void(*)(void*)){
  assert(false&&"IDF6 must not replace allocators after PSA startup");return -1;
}
extern bool otherTranslationUnit();
int main(int argc,char** argv){
  assert(argc==2);psram=std::strcmp(argv[1],"no-psram")!=0;
#ifdef RISC_TEST_POLICY_REJECTED
  const bool expected=false;
#else
  const bool expected=psram;
#endif
  assert(!NativeTlsMemory::ready());
  assert(NativeTlsMemory::initialize()==expected);
  assert(otherTranslationUnit()==expected);
  assert(NativeTlsMemory::initialize()==expected);
  // Unavailable is sticky even if PSRAM later appears. No allocator mutation.
  psram=true;assert(NativeTlsMemory::initialize()==expected);
  if(!expected){assert(live.empty()&&externalCalls==0&&dmaCalls==0);std::cout<<"Native TLS allocator policy: fail closed\n";return 0;}
  // Exercise both the runtime helper and the selected SDK implementation.
  for(auto allocate:{NativeTlsMemory::calloc,esp_mbedtls_mem_calloc}){
    for(size_t n:{size_t(1),size_t(128),size_t(2280),size_t(4095),size_t(4096),size_t(16717)}){
      auto* p=static_cast<unsigned char*>(allocate(2,n));assert(p&&live[p]==2*n);
      for(size_t i=0;i<2*n;++i)assert(p[i]==0);
      std::memset(p,0xa5,2*n);esp_mbedtls_mem_free(p);
    }
    allocationFailure=true;const unsigned before=externalCalls;
    assert(!allocate(1,16717)&&externalCalls==before+1&&dmaCalls==0);
    assert(!allocate(1,1)&&externalCalls==before+2&&dmaCalls==0);
    allocationFailure=false;
  }
  const unsigned before=externalCalls;
  assert(!NativeTlsMemory::calloc(SIZE_MAX,2)&&externalCalls==before);
  {NativeTlsMemory::DmaHeadroom probes;assert(probes&&live.size()==3);}
  allocationFailure=true;{NativeTlsMemory::DmaHeadroom probes;assert(!probes&&live.empty());}
  NativeTlsMemory::free(nullptr);esp_mbedtls_mem_free(nullptr);assert(live.empty());
  std::cout<<"Native TLS memory: SDK PSRAM-only allocations, zeroing, overflow, no fallback, DMA probes and cross-TU lifetime passed\n";
}
