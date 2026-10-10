/* Production Runtime + Graph own every grant. Test peripherals never broker grants. */
#include "bootstrap/Runtime.h"
#include "RiscPlatformClockV1.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <fstream>
#include <iterator>
static RiscBoot::Runtime *running;
static std::string mode,client;
static bool allPins;
static unsigned launcherEntries;
static size_t hostPeak,foregroundPeak,hostProviderPeak,foregroundProviderPeak;
static std::unordered_map<void*,size_t> allocations;
static size_t bytes,peakBytes,peakBlocks,allocationCalls,freeCalls;
extern "C" {
bool capacity_health(risc_runtime_health_v1*);void capacity_delay(uint32_t);bool capacity_log(const char*);
int32_t capacity_kv_get(void*,uint32_t,const char*,void*,uint32_t,uint32_t*);
int32_t capacity_kv_put(void*,uint32_t,const char*,const void*,uint32_t);
extern const risc_platform_clock_api_v1 capacity_clock;
extern const risc_realtime_control_api_v1 capacity_realtime;
void capacity_verify(bool);unsigned capacity_provider_calls(void);
const char *capacity_mode(){return mode.c_str();}
const char *capacity_client(){return client=="points"?"points.elf":"ble.elf";}
void capacity_observe(){if(!running)return;auto u=running->grantUsage();if(getenv("CAPACITY_TRACE")){static size_t last=SIZE_MAX;if(last!=u.graphLive){fprintf(stderr,"usage graph=%zu pins=%zu host=%zu fg=%zu bytes=%zu\n",u.graphLive,u.bootPins,u.hostLive,u.foregroundLive,bytes);last=u.graphLive;}}hostPeak=std::max(hostPeak,u.hostLive);foregroundPeak=std::max(foregroundPeak,u.foregroundLive);hostProviderPeak=std::max(hostProviderPeak,u.hostProviders);foregroundProviderPeak=std::max(foregroundProviderPeak,u.foregroundProviders);}
void capacity_phase(const char *phase){capacity_observe();auto u=running->grantUsage();if(!strcmp(phase,"launcher-handoff"))++launcherEntries;if(allPins&&(!strcmp(phase,"home-started")||strstr(phase,"modal-open")))assert(u.bootPins==26);printf("PHASE %s graph=%zu peak=%zu boot=%zu host=%zu/%zu foreground=%zu/%zu bytes=%zu blocks=%zu peak_bytes=%zu allocations=%zu frees=%zu\n",phase,u.graphLive,u.graphPeak,u.bootPins,u.hostLive,u.hostProviders,u.foregroundLive,u.foregroundProviders,bytes,allocations.size(),peakBytes,allocationCalls,freeCalls);fflush(stdout);}
static void record(void*p,size_t n){if(!p)return;assert(!allocations.count(p));allocations[p]=n;bytes+=n;peakBytes=std::max(bytes,peakBytes);peakBlocks=std::max(allocations.size(),peakBlocks);++allocationCalls;capacity_observe();}
void *__wrap_malloc(size_t n){void*p=std::malloc(n);record(p,n);return p;}
void *__wrap_calloc(size_t n,size_t s){void*p=std::calloc(n,s);record(p,n*s);return p;}
void __wrap_free(void*p){if(!p)return;auto i=allocations.find(p);assert(i!=allocations.end());bytes-=i->second;allocations.erase(i);++freeCalls;std::free(p);capacity_observe();}
void *__wrap_realloc(void*p,size_t n){if(!p)return __wrap_malloc(n);auto i=allocations.find(p);assert(i!=allocations.end());size_t old=i->second;void*q=std::realloc(p,n);if(!q&&n)return nullptr;bytes-=old;allocations.erase(i);if(q)record(q,n);else ++freeCalls;return q;}
}
static int32_t dataStat(void*,uint32_t ns,const char*,uint32_t*size,uint64_t*revision){capacity_observe();assert(ns==5);*size=0;*revision=0;return RISC_APP_DATA_NOT_FOUND;}
static int32_t dataRead(void*,uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*){assert(false);return -1;}
static int32_t dataReplace(void*,uint32_t,const char*,uint64_t,const void*,uint32_t){assert(false);return -1;}
int main(int argc,char**argv){
 assert(argc==4);client=argv[2];mode=argv[3];std::ifstream boot(std::string(argv[1])+"/boot.json");std::string config((std::istreambuf_iterator<char>(boot)),{});allPins=config.find("demand-retained")==std::string::npos;bool retained=mode=="retained-close";
 RiscBoot::KeyValueBackend kv{nullptr,capacity_kv_get,capacity_kv_put};
 RiscBoot::AppDataBackend data{nullptr,dataStat,dataRead,dataReplace,[](void*){return true;}};
 RiscRetainedWake::Image image{};RiscRetainedWake::Store wake(image);wake.boot(RISC_BOOT_POWER_ON);
 RiscBoot::Port port{[](){return true;},capacity_health,capacity_delay,capacity_log};port.keyValue=&kv;port.appData=&data;port.retainedWake=&wake;port.retainedDelay=[](uint32_t){};
 port.bindPlatforms=[](RiscBoot::Runtime&r){return r.registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,&capacity_clock)&&r.registerRealtime(&capacity_realtime);};
 auto runtime=new RiscBoot::Runtime(port);running=runtime;
 if(!runtime->prepare(argv[1])){fprintf(stderr,"PREPARE: %s\n",runtime->error());return 2;}
 bool ran=runtime->run();if(ran==retained){fprintf(stderr,"RUN: ran=%u retained=%u expected=%u error=%s\n",ran,runtime->retained(),retained,runtime->error());return 3;}
 assert(runtime->retained()==retained);if(mode=="handoff")assert(launcherEntries==1);capacity_phase("runtime-stopped");auto u=runtime->grantUsage();
 assert(u.selectedProviders==26&&u.graphPeak<=42);
 capacity_verify(retained);
 printf("RESULT client=%s mode=%s graph_peak=%zu graph_capacity=%zu host_sampled_peak=%zu foreground_sampled_peak=%zu host_provider_sampled_peak=%zu foreground_provider_sampled_peak=%zu owned_peak_bytes=%zu owned_peak_blocks=%zu owned_allocations=%zu owned_frees=%zu retained_owned_bytes=%zu\n",client.c_str(),mode.c_str(),u.graphPeak,u.graphCapacity,hostPeak,foregroundPeak,hostProviderPeak,foregroundProviderPeak,peakBytes,peakBlocks,allocationCalls,freeCalls,bytes);fflush(stdout);
 if(retained){unsigned before=allocationCalls,io=capacity_provider_calls();size_t retainedBytes=bytes,freed=freeCalls;assert(!runtime->run());assert(allocationCalls==before&&capacity_provider_calls()==io&&bytes==retainedBytes&&freeCalls==freed);std::_Exit(0);}
 assert(!u.graphLive&&!u.bootPins&&!u.hostProviders&&!u.foregroundProviders);delete runtime;running=nullptr;assert(!bytes&&allocations.empty());return 0;
}
