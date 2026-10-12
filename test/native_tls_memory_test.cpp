#include "ports/esp32s3/NativeTlsMemory.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <iostream>
using namespace RiscCpu;
static bool psram=true,hookFailure=false,allocationFailure=false;
static unsigned hookCalls=0,allocations=0,externalCalls=0,dmaCalls=0;
static std::map<void*,size_t> live;
static void*(*hookCalloc)(size_t,size_t)=nullptr;
static void(*hookFree)(void*)=nullptr;
size_t heap_caps_get_total_size(unsigned caps){assert(caps==NativeTlsMemory::ExternalCaps);return psram?8*1024*1024:0;}
void* heap_caps_calloc(size_t n,size_t bytes,unsigned caps){
  assert(caps==NativeTlsMemory::ExternalCaps);++externalCalls;
  if(allocationFailure)return nullptr;
  void* p=std::calloc(n,bytes);if(p){live[p]=n*bytes;++allocations;}return p;
}
void* heap_caps_malloc(size_t n,unsigned caps){
  assert(caps==(NativeTlsMemory::InternalCaps|MALLOC_CAP_DMA));++dmaCalls;
  if(allocationFailure)return nullptr;
  void* p=std::malloc(n);if(p){live[p]=n;++allocations;}return p;
}
void heap_caps_free(void* p){if(p){assert(live.erase(p)==1);--allocations;}std::free(p);}
int mbedtls_platform_set_calloc_free(void*(*c)(size_t,size_t),void(*f)(void*)){
  ++hookCalls;if(hookFailure)return -1;hookCalloc=c;hookFree=f;return 0;
}
extern bool otherTranslationUnit();
int main(int argc,char** argv){
  assert(argc==2);
  psram=std::strcmp(argv[1],"no-psram")!=0;hookFailure=std::strcmp(argv[1],"hook-failure")==0;
  assert(!NativeTlsMemory::ready());
  const bool expected=psram&&!hookFailure;
  assert(NativeTlsMemory::initialize()==expected);
  assert(otherTranslationUnit()==expected);
  assert(NativeTlsMemory::initialize()==expected&&hookCalls==unsigned(psram));
  if(!expected){assert(!hookCalloc&&!hookFree);std::cout<<argv[1]<<": fail closed, no repeated hook installation\n";return 0;}
  for(size_t n:{size_t(1),size_t(128),size_t(2280),size_t(4095),size_t(4096),size_t(16717)}){
    auto* p=static_cast<unsigned char*>(hookCalloc(2,n));assert(p&&live[p]==2*n);
    for(size_t i=0;i<2*n;++i)assert(p[i]==0);
    std::memset(p,0xa5,2*n);hookFree(p);
  }
  const unsigned before=externalCalls;
  assert(!hookCalloc(SIZE_MAX,2)&&externalCalls==before);
  allocationFailure=true;assert(!hookCalloc(1,16717)&&externalCalls==before+1&&dmaCalls==0);
  assert(!hookCalloc(1,1)&&externalCalls==before+2&&dmaCalls==0);
  allocationFailure=false;
  {NativeTlsMemory::DmaHeadroom probes;assert(probes&&allocations==3);}
  allocationFailure=true;{NativeTlsMemory::DmaHeadroom probes;assert(!probes&&allocations==0);}
  hookFree(nullptr);assert(live.empty()&&allocations==0);
  std::cout<<"Native TLS memory: all sizes PSRAM-only, overflow, zeroing, no fallback, probes and cross-TU lifetime passed\n";
}
