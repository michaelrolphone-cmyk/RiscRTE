/* Real Runtime, provider graph/registry, native app ledger, target allocator
 * adapter and import resolver. Host mapping, heap and RTOS are substituted. */
#include "image_pressure_stubs/alloc_redirect.h"
#include "bootstrap/Runtime.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#include "native/NativeAppMemory.h"
#include "support/native_registry/backend.h"
#include <esp_elf.h>
#include <private/elf_platform.h>
#include <freertos/task.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
extern "C" bool risc_runtime_reclaim_app_images();
extern "C" void native_app_memory_relocation(bool);
static thread_local bool isOwner=true;
static bool probeRecursion=false,providerRetained=false,failMapping=false,failLedger=false,probeDetached=false;
static unsigned failLedgerCount,ledgerAttempts;
static bool custodyPressure=false;
static void checkCustodyPressure();
static unsigned failures,recursiveCalls,providerStarts,providerStops,providerOutlivedApp,invocations,detachedChecks;
static size_t live,limit=SIZE_MAX,allocCount;
static struct {void* p;size_t n;} allocations[16384];
static std::string mode;
static void* providerMemory;
static RiscBoot::Runtime* runtime;
static constexpr size_t WorkBytes=256*1024,PressureLimit=384*1024;
static size_t indexOf(void*p){size_t i=0;while(i<allocCount && allocations[i].p!=p)++i;return i;}
extern "C" void* pressure_malloc(size_t n){
  if(!n)return nullptr;
  if(n>limit || live>limit-n){++failures;errno=ENOMEM;return nullptr;}
  void*p=std::malloc(n);assert(p && allocCount<16384);allocations[allocCount++]={p,n};live+=n;return p;
}
extern "C" void pressure_free(void*p){
  if(!p)return;
  if(custodyPressure)checkCustodyPressure();
  const size_t i=indexOf(p);
  if(i<allocCount){
    if(probeDetached){
      assert(risc_test_native_image_count()==1 && !risc_runtime_reclaim_app_images());
      ++detachedChecks;probeDetached=false;
    }
    if(probeRecursion && allocations[i].n>=512*1024){++recursiveCalls;assert(!risc_runtime_reclaim_app_images());}
    live-=allocations[i].n;allocations[i]=allocations[--allocCount];
  }
  std::free(p);
}
extern "C" void* pressure_calloc(size_t count,size_t size){
  if(size && count>SIZE_MAX/size){errno=ENOMEM;return nullptr;}
  void*p=pressure_malloc(count*size);if(p)std::memset(p,0,count*size);return p;
}
extern "C" void* pressure_realloc(void*p,size_t size){
  if(!p)return pressure_malloc(size);
  if(!size){pressure_free(p);return nullptr;}
  size_t i=indexOf(p);assert(i<allocCount);
  const size_t old=allocations[i].n;
  if(size>limit || live-old>limit-size){++failures;errno=ENOMEM;return nullptr;}
  void*q=std::realloc(p,size);assert(q);live=live-old+size;allocations[i]={q,size};return q;
}
extern "C" void* heap_caps_malloc(size_t size,uint32_t){
  if(failMapping && size==sizeof(esp_elf_t)){
    failMapping=false;probeDetached=true;++failures;errno=ENOMEM;return nullptr;
  }
  return pressure_malloc(size);
}
extern "C" void* heap_caps_calloc(size_t n,size_t size,uint32_t){
  ++ledgerAttempts;
  if(failLedgerCount){--failLedgerCount;++failures;errno=ENOMEM;return nullptr;}
  if(failLedger){failLedger=false;++failures;errno=ENOMEM;return nullptr;}
  return pressure_calloc(n,size);
}
extern "C" void* heap_caps_realloc(void*p,size_t size,uint32_t){return pressure_realloc(p,size);}
extern "C" void heap_caps_free(void*p){pressure_free(p);}
extern "C" size_t heap_caps_get_free_size(uint32_t){assert(false && "no heap polling");return 0;}
extern "C" size_t heap_caps_get_largest_free_block(uint32_t){assert(false && "no heap polling");return 0;}
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(isOwner?1:2);}
extern "C" void vTaskDelay(TickType_t){}
extern "C" void spi_flash_disable_interrupts_caches_and_other_cpu(){}
extern "C" void spi_flash_enable_interrupts_caches_and_other_cpu(){}
extern "C" void esp_spiram_writeback_cache(){}
extern "C" bool esp_elf_privileged_os_cpu_scope_owned_v1(){return false;}
extern "C" uintptr_t esp_elf_privileged_os_cpu_lookup_v1(const char*){return 0;}
extern "C" uintptr_t esp_elf_find_symbol(const char*){return 0;}
extern "C" int esp_elf_register_symbol(esp_elf_symbol_table_t*){return 0;}
extern "C" int* __errno(){return &errno;}
struct _reent;extern "C" _reent* __getreent(){return nullptr;}
extern "C" const char _ctype_[]={0};
extern "C" int __ltdf2(double a,double b){return a<b?-1:0;}
extern "C" int __gtdf2(double a,double b){return a>b?1:0;}
extern "C" unsigned __fixunsdfsi(double a){return unsigned(a);}
extern "C" double __floatunsidf(unsigned a){return a;}
extern "C" double __divdf3(double a,double b){return a/b;}
template<class Function>static Function imported(const char* name,bool app){
  native_app_memory_relocation(app);
  const uintptr_t pointer=elf_find_sym_default(name);
  native_app_memory_relocation(false);assert(pointer);
  return reinterpret_cast<Function>(pointer);
}
extern "C" void* image_pressure_provider_alloc(){
  ++providerStarts;
  if(mode=="provider-start")limit=PressureLimit;
  auto allocate=imported<void*(*)(size_t)>("malloc",false);
  providerMemory=allocate(WorkBytes);assert(providerMemory);
  std::memset(providerMemory,0x5a,WorkBytes);return providerMemory;
}
extern "C" bool image_pressure_provider_quiesce(){return !providerRetained;}
extern "C" void image_pressure_provider_free(void*p){
  assert(p==providerMemory && indexOf(p)<allocCount && static_cast<unsigned char*>(p)[0]==0x5a);
  auto release=imported<void(*)(void*)>("free",false);release(p);providerMemory=nullptr;++providerStops;
}
extern "C" void risc_test_native_unloading(const char*path,void*){
  if(providerMemory && std::strstr(path,"default.elf")){
    assert(indexOf(providerMemory)<allocCount && static_cast<unsigned char*>(providerMemory)[0]==0x5a);
    ++providerOutlivedApp;
  }
}
extern "C" void image_pressure_app(const risc_runtime_api_v1* api){
  ++invocations;
  if(mode=="cache-hit" || mode=="ledger-init"){
    if(invocations==1){
      assert(risc_test_native_image_count()==(RISC_APP_IMAGE_CACHE?1u:0u));
      failMapping=RISC_APP_IMAGE_CACHE && mode=="cache-hit";
      failLedger=RISC_APP_IMAGE_CACHE && mode=="ledger-init";
      probeRecursion=true;assert(api->request_launch("default.elf"));return;
    }
    assert(invocations==2 && !risc_test_native_image_count() && failures==unsigned(RISC_APP_IMAGE_CACHE));
    assert(risc_test_native_read_count()==((RISC_APP_IMAGE_CACHE && mode=="cache-hit")?1u:2u));
    assert(detachedChecks==((RISC_APP_IMAGE_CACHE && mode=="cache-hit")?1u:0u));
    assert(!risc_runtime_reclaim_app_images());probeRecursion=false;return;
  }
  assert(risc_test_native_image_count()==(RISC_APP_IMAGE_CACHE?1u:0u));
  // Rejected sizes do not reclaim useful inputs, even if libc returns NULL.
  auto providerAllocate=imported<void*(*)(size_t)>("malloc",false);
  auto providerZero=imported<void*(*)(size_t,size_t)>("calloc",false);
  auto providerResize=imported<void*(*)(void*,size_t)>("realloc",false);
  assert(!providerAllocate(0) && !providerZero(SIZE_MAX,2));
  void* zero=providerAllocate(16);assert(zero && !providerResize(zero,0));
  assert(risc_test_native_image_count()==(RISC_APP_IMAGE_CACHE?1u:0u));
  probeRecursion=true;
  const unsigned before=failures;
  if(mode.compare(0,8,"provider")==0){
    risc_runtime_capability_v1 promotion{};promotion.struct_size=sizeof(promotion);
    assert(api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&promotion));
    const auto* table=static_cast<const risc_provider_promotion_api_v1*>(promotion.api);
    assert(table->promote(table->context)==RISC_PROVIDER_PROMOTION_OK && api->release(&promotion));
    providerRetained=mode=="provider-retained";
    if(mode!="provider-start")limit=PressureLimit;
    risc_runtime_capability_v1 grant{};grant.struct_size=sizeof(grant);
    if(!api->acquire("test.pressure",1,0,&grant)){std::fprintf(stderr,"acquire failed: %s live=%zu limit=%zu failures=%u\n",runtime->error(),live,limit,failures);assert(false);}
    assert(providerStarts==1 && providerMemory && !providerStops);
    // Keep the boot-owned provider alive through the app ledger's cleanup.
    assert(api->release(&grant));
  }else{
    auto allocate=imported<void*(*)(size_t)>("malloc",true);
    auto zeroAllocate=imported<void*(*)(size_t,size_t)>("calloc",true);
    auto resize=imported<void*(*)(void*,size_t)>("realloc",true);
    auto capsAllocate=imported<void*(*)(size_t,uint32_t)>("heap_caps_malloc",true);
    void* p=nullptr;
    if(mode=="realloc" || mode=="failed-retry"){p=allocate(32768);assert(p);std::memset(p,0x3c,32768);}
    limit=PressureLimit;
    if(mode=="foreign"){
      std::thread foreign([&](){
        isOwner=false;assert(!risc_runtime_reclaim_app_images());
        void* denied=providerAllocate(WorkBytes);
        assert(bool(denied)==!RISC_APP_IMAGE_CACHE);
        if(denied)pressure_free(denied);
      });foreign.join();
      assert(risc_test_native_image_count()==(RISC_APP_IMAGE_CACHE?1u:0u));
    }
    if(mode=="calloc"){
      p=zeroAllocate(512,512);assert(p);
      for(size_t i=0;i<WorkBytes;++i)assert(!static_cast<unsigned char*>(p)[i]);
    }else if(mode=="realloc"){
      p=resize(p,WorkBytes);assert(p);
      for(size_t i=0;i<32768;++i)assert(static_cast<unsigned char*>(p)[i]==0x3c);
    }else if(mode=="failed-retry"){
      assert(!resize(p,2*1024*1024));
      assert(failures-before==(RISC_APP_IMAGE_CACHE?2u:1u));
      for(size_t i=0;i<32768;++i)assert(static_cast<unsigned char*>(p)[i]==0x3c);
      const unsigned failed=failures;assert(!resize(p,2*1024*1024));assert(failures==failed+1);
    }else if(mode=="caps")p=capsAllocate(WorkBytes,3);
    else p=allocate(WorkBytes);
    assert(p);
    if(mode=="app-retained")assert(api->retain_invocation());
  }
  assert(!risc_test_native_image_count());
  assert(recursiveCalls==(RISC_APP_IMAGE_CACHE?1u:0u));
  if(mode!="failed-retry")assert(failures-before==(RISC_APP_IMAGE_CACHE?(mode=="foreign"?2u:1u):0u));
  assert(!risc_runtime_reclaim_app_images()); // Binding is detached for this session.
  limit=SIZE_MAX;probeRecursion=false;
}
static void write(const std::string&path,const char*data){std::ofstream(path)<<data;}
#include "image_cache_custody.inc"
int main(int argc,char**argv){
  assert(argc==3);const std::string root=argv[1];mode=argv[2];
  if(mode=="ledger-final" || mode.rfind("custody-",0)==0)return runCustody(root);
  assert(!risc_runtime_reclaim_app_images());
  assert(!elf_find_sym_default("risc_runtime_reclaim_app_images"));
  assert(!elf_find_sym_default("esp_dlopen_cached_instance"));
  write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root+"/app.json",R"({"type":"application","id":"pressure-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.pressure","api":1},{"capability":"runtime.provider-promotion","api":1}]})");
  write(root+"/provider.json",R"({"type":"driver","id":"pressure","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","requires":[],"provides":[{"capability":"test.pressure","api":1}]})");
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand-retained","drivers":[{"manifest":"provider.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.pressure","api":1,"instance_id":0},{"capability":"runtime.provider-promotion","api":1,"instance_id":0}]}]})");
  runtime=new RiscBoot::Runtime({[](){return isOwner;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}});
  assert(runtime->prepare(root.c_str()));
  const bool retained=mode=="app-retained" || mode=="provider-retained";
  assert(runtime->run()!=retained && runtime->retained()==retained);
  assert(!risc_runtime_reclaim_app_images() && !risc_test_native_image_count());
  if(mode.compare(0,8,"provider")==0)assert(providerOutlivedApp==1);
  if(!retained){delete runtime;runtime=nullptr;assert(!risc_test_native_mapping_count());if(allocCount){std::fprintf(stderr,"remaining blocks=%zu bytes=%zu\n",allocCount,live);for(size_t i=0;i<allocCount;++i)std::fprintf(stderr," %zu",allocations[i].n);}assert(!allocCount && !live);}
  else assert(risc_test_native_mapping_count()==1 && allocCount);
  printf("Cache pressure flag=%u mode=%s: allocation_failures=%u recursion_rejected=%u provider_start/stop=%u/%u retained=%u PASS\n",RISC_APP_IMAGE_CACHE,mode.c_str(),failures,recursiveCalls,providerStarts,providerStops,retained);
}
