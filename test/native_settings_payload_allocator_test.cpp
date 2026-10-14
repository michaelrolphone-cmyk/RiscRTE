#include "ports/esp32s3/NativeSettingsPayloadAllocator.h"
#include <esp_heap_caps.h>
#include <cassert>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
namespace P=RiscCpu::NativeSettingsPayloadAllocator;
namespace {
struct Block {void*base;size_t bytes;bool rejected;};
std::mutex mutex;std::map<void*,Block>blocks;
bool fail=false,internal=false,shortRange=false,wrapAddress=false,tailWrapAddress=false;void*invalidAddress=nullptr;unsigned requests=0,frees=0,predicates=0;size_t last=0;
}
void*heap_caps_malloc(size_t n,uint32_t caps){
 std::lock_guard<std::mutex>lock(mutex);++requests;last=n;
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)&&!(caps&MALLOC_CAP_INTERNAL));
 if(fail)return nullptr;
 if(wrapAddress){invalidAddress=reinterpret_cast<void*>(UINTPTR_MAX-P::Alignment);return invalidAddress;}
 if(tailWrapAddress){assert(n>2*P::Alignment);invalidAddress=reinterpret_cast<void*>(UINTPTR_MAX-n+2);return invalidAddress;}
 // Deliberately misalign the vendor result; the production wrapper must align
 // its payload while keeping the exact vendor allocation for secure release.
 void*base=std::malloc(n+1);assert(base);void*out=static_cast<unsigned char*>(base)+1;
 std::memset(out,0xa7,n);blocks[out]={base,n,internal||shortRange};return out;
}
bool esp_ptr_external_ram(const void*p){
 std::lock_guard<std::mutex>lock(mutex);++predicates;if(internal)return false;
 for(const auto&b:blocks){const auto at=uintptr_t(p),begin=uintptr_t(b.first);if(at>=begin&&at<begin+b.second.bytes)return !(shortRange&&at==begin+b.second.bytes-1);}
 return false;
}
void heap_caps_free(void*p){
 std::lock_guard<std::mutex>lock(mutex);if(p==invalidAddress){invalidAddress=nullptr;++frees;return;}const auto i=blocks.find(p);assert(i!=blocks.end());
 for(size_t at=0;at<i->second.bytes;++at)assert(static_cast<unsigned char*>(p)[at]==(i->second.rejected?0xa7:0));
 std::free(i->second.base);blocks.erase(i);++frees;
}
int main(){
 auto a=P::allocator();assert(!a.context&&a.allocate&&a.release);a.release(a.context,nullptr);
 for(size_t n:{size_t(0),size_t(1),size_t(15),size_t(16),size_t(17),size_t(4095),size_t(4096),size_t(65512),size_t(199180)}){
  auto*p=a.allocate(a.context,n);assert(p&&uintptr_t(p)%P::Alignment==0&&last>=n+P::Alignment);std::memset(p,0x59,n);a.release(a.context,p);assert(blocks.empty());
 }
 const unsigned before=requests;for(size_t n:{SIZE_MAX,SIZE_MAX-1,SIZE_MAX-P::Alignment})assert(!a.allocate(a.context,n));assert(requests==before);
 fail=true;assert(!a.allocate(a.context,4096)&&requests==before+1&&blocks.empty());fail=false;
 wrapAddress=true;assert(!a.allocate(a.context,32)&&!invalidAddress&&blocks.empty());wrapAddress=false;
 const unsigned beforePredicates=predicates;tailWrapAddress=true;
 assert(!a.allocate(a.context,32)&&!invalidAddress&&blocks.empty()&&predicates==beforePredicates);tailWrapAddress=false;
 internal=true;assert(!a.allocate(a.context,32)&&blocks.empty());internal=false;
 shortRange=true;assert(!a.allocate(a.context,32)&&blocks.empty());shortRange=false;
 std::vector<std::thread>threads;for(unsigned t=0;t<4;++t)threads.emplace_back([&]{for(unsigned i=0;i<500;++i){const size_t n=1+(i%128);void*p=a.allocate(a.context,n);assert(p&&uintptr_t(p)%P::Alignment==0);std::memset(p,0x3c,n);a.release(a.context,p);}});for(auto&t:threads)t.join();
 assert(blocks.empty()&&requests==frees+1);
 printf("PSRAM-only payload allocator: aligned zero/nonzero/large requests; size overflow before SDK; rejected alignment and range-tail address overflow before predicate; allocation failure without fallback; whole-range rejection; complete payload/header/slack wiping; 2000 concurrent cycles PASS (alignment=%zu)\n",P::Alignment);
}
