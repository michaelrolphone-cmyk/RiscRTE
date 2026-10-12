#include "ports/esp32s3/NativeAppDataMount.h"
#include "ports/esp32s3/NativeSettingsBackend.h"
#include "ports/esp32s3/NativeSettingsStartupEntropy.h"
#include "ports/esp32s3/NativeSettingsAssembly.h"
#include "ports/esp32s3/NativeSettingsWorker.h"
#include "ports/esp32s3/NativeSettingsPayloadAllocator.h"
#include "native_littlefs_profile_fixture.h"
#include <esp_heap_caps.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <vector>
#include <string>
#include <set>
#include <thread>
#include <fcntl.h>
namespace M=RiscAppDataMount;
namespace B=RiscCpu::NativeSettingsBackend;
namespace F=RiscCpu::NativeSettingsAssembly;
namespace W=RiscCpu::NativeSettingsWorker;
using C=RiscExperimental::NativeReplicaCore;
using A=RiscExperimental::NativeBaselineAdmission;
using Bytes=std::vector<uint8_t>;
extern "C" void native_lfs_fixture_select_appdata(void);
extern "C" void* xTaskGetCurrentTaskHandle(){static thread_local int task;return &task;}
extern "C" int xPortInIsrContext(){return 0;}
static bool onOwner=true,liveWorker=false,nvsReady=true;
static uint32_t workerNs=0;
static uint64_t generation=0;
static F::Inputs inputs{};
static RiscCpu::NativeSettingsLifecycle::Admission selected{};
static unsigned selections=0,nvsCalls=0,allocations=0;
static std::map<void*,size_t> blocks;
void*heap_caps_malloc(size_t n,unsigned flags){assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));++allocations;void*p=malloc(n);assert(p);blocks[p]=n;return p;}
void heap_caps_free(void*p){assert(blocks.count(p));for(size_t i=0;i<blocks[p];++i)assert(!static_cast<unsigned char*>(p)[i]);blocks.erase(p);free(p);}
bool esp_ptr_external_ram(const void*p){for(auto&v:blocks)if(uintptr_t(p)>=uintptr_t(v.first)&&uintptr_t(p)-uintptr_t(v.first)<v.second)return true;return false;}
bool esp_ptr_internal(const void*p){return p&&!esp_ptr_external_ram(p);}
namespace RiscNvs {esp_err_t initializationStatus(){return nvsReady?ESP_OK:ESP_FAIL;}}
namespace RiscCpu {namespace NativeSettingsWorker {
bool custody(uint32_t ns,Custody*out){if(out)*out={};if(!out||!liveWorker||!ns)return false;*out={reinterpret_cast<const Coordinator*>(uintptr_t(0x1000+ns)),xTaskGetCurrentTaskHandle(),generation,workerNs,1};return true;}
}}
static void*alloc(size_t n){auto a=RiscCpu::NativeSettingsPayloadAllocator::allocator();return a.allocate(a.context,n);}
static void release(void*p){auto a=RiscCpu::NativeSettingsPayloadAllocator::allocator();a.release(a.context,p);}
namespace RiscCpu {namespace NativeSettingsAssembly {
Files::Hooks fileHooks(void*c,uint32_t(*now)(void*),bool(*yield)(void*)){return {c,now,yield,alloc,release};}
const NativeSettingsLifecycle::Admission*assemble(const Inputs&i){++selections;assert(i.owner&&i.owner()&&i.device==9&&i.writerCount==6);inputs=i;return &selected;}
}}
static std::map<std::string,std::map<std::string,Bytes>> nvs;
struct Handle {std::string space;nvs_open_mode_t mode;};
static std::map<nvs_handle_t,Handle>handles;
static nvs_handle_t nextHandle=1;
static void safe(){++nvsCalls;assert(liveWorker&&M::held());}
extern "C" esp_err_t nvs_open(const char*n,nvs_open_mode_t m,nvs_handle_t*out){safe();if(m==NVS_READONLY&&!nvs.count(n))return ESP_ERR_NVS_NOT_FOUND;*out=nextHandle++;handles[*out]={n,m};return ESP_OK;}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){safe();assert(handles.count(h));auto&space=nvs[handles[h].space];if(!space.count(k))return ESP_ERR_NVS_NOT_FOUND;auto&b=space[k];if(!out){*n=b.size();return ESP_OK;}if(*n<b.size())return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,b.data(),b.size());*n=b.size();return ESP_OK;}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*b,size_t n){safe();assert(handles.count(h)&&handles[h].mode==NVS_READWRITE);auto*p=static_cast<const uint8_t*>(b);nvs[handles[h].space][k]=Bytes(p,p+n);return ESP_OK;}
extern "C" esp_err_t nvs_commit(nvs_handle_t h){safe();assert(handles.count(h));return ESP_OK;}
extern "C" void nvs_close(nvs_handle_t h){safe();assert(handles.erase(h)==1);}
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){safe();assert(handles.count(h));*n=nvs[handles[h].space].size();return ESP_OK;}
struct Iterator {std::string space;std::vector<std::string>keys;size_t at=0;};
static std::set<Iterator*>iterators;
extern "C" nvs_iterator_t nvs_entry_find(const char*p,const char*n,nvs_type_t t){safe();assert(!strcmp(p,"nvs")&&t==NVS_TYPE_ANY);if(!nvs.count(n)||nvs[n].empty())return nullptr;auto*i=new Iterator;i->space=n;for(auto&v:nvs[n])i->keys.push_back(v.first);iterators.insert(i);return i;}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t v){safe();auto*i=static_cast<Iterator*>(v);assert(iterators.count(i));if(++i->at<i->keys.size())return i;iterators.erase(i);delete i;return nullptr;}
extern "C" void nvs_entry_info(nvs_iterator_t v,nvs_entry_info_t*out){safe();auto*i=static_cast<Iterator*>(v);assert(iterators.count(i));*out={};strcpy(out->namespace_name,i->space.c_str());strcpy(out->key,i->keys[i->at].c_str());out->type=NVS_TYPE_BLOB;}
extern "C" void nvs_release_iterator(nvs_iterator_t v){safe();auto*i=static_cast<Iterator*>(v);if(i){assert(iterators.erase(i));delete i;}}
static bool owner(){return onOwner;}
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
static bool entropyWindow=false;static unsigned entropyFills=0;
extern "C" void bootloader_random_enable(){assert(!entropyWindow);entropyWindow=true;}
extern "C" void bootloader_random_disable(){assert(entropyWindow);entropyWindow=false;}
extern "C" void esp_fill_random(void*p,size_t n){assert(entropyWindow&&n==32);++entropyFills;memset(p,0x31,n);}
#endif
static void bind(){native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();assert(M::prepare(static_cast<const esp_partition_t*>(native_lfs_fixture_partition())));assert(native_lfs_fixture_reboot()==0&&M::finish());}
static void ready(){bind();assert(B::prepare(owner)&&B::admission()==&selected&&selections==1);assert(!B::prepare(owner));assert(!nvsCalls&&!allocations);native_lfs_fixture_counters a{},b{};native_lfs_fixture_get_counters(&a);for(unsigned i=0;i<100;++i)assert(risc_native_settings_admission()==&selected);native_lfs_fixture_get_counters(&b);assert(!memcmp(&a,&b,sizeof a)&&!nvsCalls&&!allocations);}
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
static void entropyInputs(){
 const auto&a=inputs.preferences;const auto&b=inputs.wifi;
 assert(a.initialIncarnation&&b.initialIncarnation&&memcmp(a.initialIncarnation,b.initialIncarnation,16));
 assert(a.first.otherWritersStopped&&a.first.entropy&&!a.first.stagingCapacity&&a.firstContextBytes);
 uint8_t v[16];memset(v,0x51,16);assert(a.first.otherWritersStopped(a.first.context));
 assert(!a.first.entropy(a.first.context,v));for(auto n:v)assert(n==0x51);assert(entropyFills==1&&!entropyWindow);
}
#endif
static void active(uint32_t ns){liveWorker=true;onOwner=false;workerNs=ns;++generation;}
static void verifyValue(C&core,const char*k,const char*v){char out[128]{};uint32_t n=0;assert(core.get(k,out,sizeof out,&n)==C::Ok&&n==strlen(v)&&!memcmp(out,v,n));}
static void exercise(unsigned index,const char*mode){
 auto&input=index?inputs.wifi:inputs.preferences;const auto&b=input.backend;const uint32_t ns=index?6:1;
 active(!strcmp(mode,"wrong-namespace")?7:ns);if(!strcmp(mode,"wrong-worker"))liveWorker=false;
 M::Guard guard(M::Access::Settings);assert(guard.held);
 F::Store store(ns,*b.files,b.metadata,b.guard,RiscCpu::NativeSettingsPayloadAllocator::allocator(),b.artifacts);
 C core(ns,store.backend(),F::Store::limits(),RiscCpu::NativeSettingsPayloadAllocator::allocator());
 if(!strcmp(mode,"wrong-worker")||!strcmp(mode,"wrong-namespace")){auto calls=nvsCalls;assert(core.load()==C::Busy&&nvsCalls==calls);return;}
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
 uint8_t first[16]{},next[16]{};assert(input.first.otherWritersStopped(input.first.context));
 assert(input.first.entropy(input.first.context,first)&&input.first.entropy(input.first.context,next));
 assert(memcmp(first,next,16)&&memcmp(first,input.initialIncarnation,16)&&entropyFills==1&&!entropyWindow);
#endif
 assert(core.load()==C::Missing);
 const bool legacy=!strcmp(mode,"legacy");
 const std::string originalSpace=ns==1?"rte00000001":"rte00000006";
 if(legacy)nvs[originalSpace]["original"]={'k','e','p','t'};
 const auto original=nvs[originalSpace];
 A factory(&store,core,{const_cast<F::Backend*>(&b),[](void*p){auto&v=*static_cast<F::Backend*>(p);return v.guard.workerWithExclusiveLease(v.guard.context);},[](void*){return generation;}},{input.digest.context,input.digest.begin,input.digest.update,input.digest.finish},legacy?b.legacy->sources():b.sources->sources(),RiscCpu::NativeSettingsPayloadAllocator::allocator());
 C::Initialization init;
 if(legacy){A::Witness witness{};assert(b.legacy&&b.legacy->inventory(&witness)==C::Ok);A::Selection selection{witness.kind,witness.revision,{}};memcpy(selection.fingerprint,witness.fingerprint,32);assert(factory.admitImported(selection,&init)==C::Ok);}
 else assert(factory.admitFreshEmpty(&init)==C::Ok);
 if(!strcmp(mode,"retained-flash"))native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,2,NATIVE_LFS_FAIL_BEFORE);
 const int rc=legacy?core.initializeImportedBaseline(init):core.initializeFreshEmpty(init);
 if(!strcmp(mode,"retained-flash")){fprintf(stderr,"retained-flash: result=%d mount=%d files=%d sources=%d\n",rc,int(M::retained()),int(b.files->retained()),int(b.sources->retained()));assert(rc!=C::Ok&&M::retained());native_lfs_fixture_counters a{},z{};native_lfs_fixture_get_counters(&a);uint32_t t=1,u=1;assert(!b.guard.space(b.guard.context,&t,&u)&&!t&&!u);assert(core.load()!=C::Ok);native_lfs_fixture_get_counters(&z);assert(!memcmp(&a,&z,sizeof a));store.finishOperation();return;}
 assert(rc==C::Ok&&core.baseline().origin()==(legacy?C::Origin::ImportedLegacy:C::Origin::FreshEmpty));store.finishOperation();
 const C::Mutation values[]={{index?"wifi.home":"ui.theme",C::Put,index?"synthetic-home":"dark",index?14u:4u},{index?"wifi.work":"ui.font",C::Put,index?"synthetic-work":"large",index?14u:5u}};
 for(unsigned i=0;i<20;++i){assert(core.apply(core.revision(),values,2,"synthetic-settings",18)==C::Ok);store.finishOperation();}
 assert(core.load()==C::Ok);verifyValue(core,values[0].key,index?"synthetic-home":"dark");verifyValue(core,values[1].key,index?"synthetic-work":"large");store.finishOperation();
 uint32_t total=0,used=0;assert(b.guard.space(b.guard.context,&total,&used)&&total==0x80000&&used>0);
 uint8_t digest[32]{};assert(input.digest.begin(input.digest.context)&&input.digest.update(input.digest.context,"abc",3)&&input.digest.finish(input.digest.context,digest));assert(digest[0]==0xba&&digest[31]==0xad);
 if(legacy){verifyValue(core,"original","kept");assert(nvs[originalSpace]==original&&core.baseline().archiveObligation());}
 assert(!handles.size()&&!iterators.size()&&store.exitSafe()&&factory.exitSafe());
}
int main(int argc,char**argv){assert(argc==2&&native_lfs_fixture_reset()==0);const char*mode=argv[1];
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
 if(!strcmp(mode,"no-entropy")){ready();assert(!inputs.preferences.initialIncarnation&&!inputs.wifi.initialIncarnation&&inputs.preferences.first.entropy&&inputs.wifi.first.entropy&&!nvsCalls);return 0;}
 assert(RiscCpu::NativeSettingsStartupEntropy::capture(owner));
#endif
 if(!strcmp(mode,"no-mount")){assert(!B::prepare(owner)&&!B::admission()&&!selections);return 0;}
 if(!strcmp(mode,"wrong-owner")){onOwner=false;assert(!B::prepare(owner)&&!B::admission()&&!selections);return 0;}
 if(!strcmp(mode,"nvs-unavailable")){nvsReady=false;assert(!B::prepare(owner)&&!B::admission()&&!selections);return 0;}
 if(!strcmp(mode,"busy-mount")){bind();M::Guard g(M::Access::AppData);assert(g.held&&!B::prepare(owner)&&!B::admission()&&!M::retained());return 0;}
 ready();
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
 entropyInputs();
#endif
 {M::Guard g(M::Access::AppData);assert(g.held);assert(native_lfs_fixture_mkdir("/n00000001")==0);assert(native_lfs_fixture_seed("/n00000001/timecard.json","preserve",8)==0);}
 exercise(0,mode);if(!strcmp(mode,"normal")||!strcmp(mode,"legacy"))exercise(1,mode);
 if(!M::retained()){liveWorker=false;onOwner=true;M::Guard g(M::Access::AppData);assert(g.held);char bytes[8]{};int fd=native_lfs_fixture_open("/n00000001/timecard.json",O_RDONLY);assert(fd>=0&&native_lfs_fixture_read(fd,bytes,8)==8&&!memcmp(bytes,"preserve",8)&&native_lfs_fixture_close(fd)==0);}
 assert(blocks.empty());printf("PASS: concrete native settings backend %s; controls=%zu; NVS calls=%u\n",mode,B::controlBytes(),nvsCalls);
}
