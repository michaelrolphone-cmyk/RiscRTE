// Real lifecycle callbacks -> Runtime graph/CpuPort -> X4/FatFs and checked
// native store. Only RTOS scheduling, hardware sectors and vendor NVS are models.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main lower_port_main
#include "native_sd_port_test.cpp"
#undef main
#pragma GCC diagnostic pop
#define NATIVE_FIXTURE_THREADED 1
#include "native_replica_checked_fixture.h"
#include "ports/esp32s3/NativeSettingsBootstrap.h"
#include <set>
#include "ports/esp32s3/NativeSettingsWorker.h"
#include <openssl/evp.h>
#include <memory>
namespace F=NativeFixture;namespace L=RiscCpu::NativeSettingsLifecycle;namespace W=RiscCpu::NativeSettingsWorker;
using B=RiscCpu::NativeSettingsBootstrap;using Sources=B::Sources;using M=RiscCpu::NativeSettingsMaintenance;using Coordinator=RiscExperimental::NativeSettingsCoordinator;
static std::unique_ptr<Coordinator> services[2];static thread_local unsigned onNamespace=0;static bool concurrentNative=false;static M*maintenance;static Pin*anchorPin;static bool releaseAnchorAtFinal=false;static C*cores[2];static std::string scenario;static bool cancelLatch=false,cancellationArmed=false;static unsigned checkpoints=0;
struct Iterator {std::string space;std::vector<std::string>keys;size_t at=0;};
static std::set<Iterator*>iterators;
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){++F::nvsTotal;F::safe();assert(F::handles.count(h));*n=F::nvs[F::handles.at(h).name].size();return ESP_OK;}
extern "C" nvs_iterator_t nvs_entry_find(const char*partition,const char*space,nvs_type_t type){++F::nvsTotal;F::safe();assert(!strcmp(partition,"nvs")&&type==NVS_TYPE_ANY);auto found=F::nvs.find(space);if(found==F::nvs.end()||found->second.empty())return nullptr;auto*i=new Iterator;iterators.insert(i);i->space=space;for(auto&e:found->second)i->keys.push_back(e.first);return i;}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t it){++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));if(++i->at<i->keys.size())return i;iterators.erase(i);delete i;return nullptr;}
extern "C" void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));*out={};strcpy(out->namespace_name,i->space.c_str());strcpy(out->key,i->keys.at(i->at).c_str());out->type=NVS_TYPE_BLOB;}
extern "C" void nvs_release_iterator(nvs_iterator_t it){++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);if(i){assert(iterators.erase(i));delete i;}}
static const uint8_t nativeOwner[16]={'R','i','s','c','R','T','E','/','N','a','t','i','v','e','1',0};
static Sources::Hooks sourceHooks(F::Fixture&f){return {&f,[](void*){return F::onWorker&&F::lease;},[](void*){return ESP_OK;},[](void*p){return static_cast<F::Fixture*>(p)->files.retained()?int(C::Retained):int(C::Ok);}};}
static void freshBaseline(F::Fixture&f,const char*value){Sources sources(f.core.namespaceId(),nativeOwner,f.inspector,sourceHooks(f));using Factory=RiscExperimental::NativeBaselineAdmission;Factory factory(&sources,f.core,{nullptr,[](void*){return F::onWorker&&F::lease;},[](void*){return uint64_t(1);}},{},sources.sources(),{});C::Initialization admission;assert(!factory.admitFreshEmpty(&admission)&&!f.core.initializeFreshEmpty(admission));f.store.finishOperation();assert(!F::put(f.core,"setting",value));f.store.finishOperation();}
#if !RISC_NATIVE_SETTINGS_ASSEMBLY
bool esp_ptr_internal(const void*p){return p!=nullptr;}
#endif
namespace RiscCpu {namespace NativeSettingsWorker {
bool prepare(uint32_t ns,C&core,S&store,bool(*check)()){
 assert(check()&&!core.ready());const unsigned i=ns==6?1:0;
 services[i]=std::make_unique<Coordinator>(core,store,Coordinator::Hooks{reinterpret_cast<void*>(uintptr_t(ns)),
 [](void*){return owner();},[](void*p){return task==Task::NativeSettings&&onNamespace==uintptr_t(p);},
 [](void*){return !F::lease.exchange(true);},[](void*){assert(F::lease.exchange(false));},[](void*,const void*p,size_t n){return p&&n;}});return true;
}
Coordinator*coordinator(uint32_t ns){return services[ns==6?1:0].get();}
bool exitSafe(){for(auto&s:services)if(s&&!s->exitSafe())return false;return true;}
}}
struct Hash {EVP_MD_CTX*ctx=EVP_MD_CTX_new();~Hash(){EVP_MD_CTX_free(ctx);}
 M::Sync::Checkpoint::Digest api(){return {this,[](void*p){return EVP_DigestInit_ex(static_cast<Hash*>(p)->ctx,EVP_sha256(),nullptr)==1;},[](void*p,const void*b,uint32_t n){return EVP_DigestUpdate(static_cast<Hash*>(p)->ctx,b,n)==1;},[](void*p,uint8_t*out){unsigned n=0;return EVP_DigestFinal_ex(static_cast<Hash*>(p)->ctx,out,&n)==1&&n==32;}};}};
#if RISC_NATIVE_SETTINGS_ASSEMBLY
#include "native_settings_assembly_fixture.h"
#endif
static void saveNvs(){std::ofstream out(F::rootPath+".receipt",std::ios::binary);auto write=[&](uint32_t n){out.write(reinterpret_cast<const char*>(&n),sizeof n);};write(F::nvs.size());for(const auto&space:F::nvs){write(space.first.size());out.write(space.first.data(),space.first.size());write(space.second.size());for(const auto&row:space.second){write(row.first.size());out.write(row.first.data(),row.first.size());write(row.second.size());out.write(reinterpret_cast<const char*>(row.second.data()),row.second.size());}}out.close();assert(out);}
static void loadNvs(){std::ifstream in(F::rootPath+".receipt",std::ios::binary);auto read=[&](){uint32_t n;in.read(reinterpret_cast<char*>(&n),sizeof n);assert(in);return n;};F::nvs.clear();const auto count=read();assert(count<8);for(unsigned i=0;i<count;++i){auto n=read();assert(n<32);std::string space(n,' ');in.read(space.data(),n);const auto rows=read();assert(rows<8);for(unsigned j=0;j<rows;++j){n=read();assert(n<32);std::string key(n,' ');in.read(key.data(),n);n=read();assert(n<1000);auto&bytes=F::nvs[space][key];bytes.resize(n);in.read(reinterpret_cast<char*>(bytes.data()),n);}}assert(in);}
static uint64_t ownerScheduleCalls=0,ownerScheduleMs=0,providerWorkerDelayMs=0,hardwareSleepMs=0;
static void schedule(uint32_t ms){assert(owner());++ownerScheduleCalls;ownerScheduleMs+=ms;nowMs+=ms;
 if(cancellationArmed&&port->nativeStorageAttached()){cancelLatch=true;cancellationArmed=false;}
 if(!mailbox.idle()){task=Task::ProviderWorker;(void)mailbox.workerStep();task=Task::Owner;}
 if(concurrentNative){std::this_thread::yield();return;}
 for(unsigned i=0;i<2;++i)if(services[i]){task=Task::NativeSettings;F::onWorker=true;onNamespace=i?6:1;services[i]->workerStep();F::onWorker=false;onNamespace=0;task=Task::Owner;}
}
static void le(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
static Bytes map(const char*value){Bytes b(20+strlen(value));strcpy(reinterpret_cast<char*>(b.data()),"setting");le(b.data()+16,strlen(value));memcpy(b.data()+20,value,strlen(value));return b;}
static C::Identity identity(uint32_t ns,const Bytes&map){Bytes b(12);le(b.data(),ns);le(b.data()+4,1);le(b.data()+8,map.size());b.insert(b.end(),map.begin(),map.end());C::Identity id{};id.known=true;id.generation=19;id.lineage[0]=ns;unsigned n=0;assert(EVP_Digest(b.data(),b.size(),id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;}
static void file(const char*path,const void*bytes,uint32_t n){const auto*v=f->volume;const auto*e=risc_storage_volume_extension(v);const auto h=v->file_open_write(v->context,path);assert(h);assert(v->file_write(v->context,h,bytes,n)==n&&e->file_sync(v->context,h)&&v->file_close(v->context,h,true));}
static void seedMaintenance(uint32_t ns,const Bytes&bytes,C::Identity id){const auto*v=f->volume;const auto*e=risc_storage_volume_extension(v);char directory[64],page[160],digest[65];snprintf(directory,sizeof directory,"/RTECFG/%08x",ns);assert(e->mkdir(v->context,directory));uint8_t index[M::Sync::Checkpoint::IndexBytes];M::Sync::Checkpoint::makeIndex(index,ns,bytes.size(),1,id);std::string path=std::string(directory)+"/replica.index";file(path.c_str(),index,sizeof index);for(unsigned i=0;i<32;++i)snprintf(digest+2*i,3,"%02x",id.digest[i]);snprintf(page,sizeof page,"%s/nr-%08x%08x-%s-%08x",directory,unsigned(id.generation>>32),unsigned(id.generation),digest,0);file(page,bytes.data(),bytes.size());}
static void ordinaryMaintenance(){const auto*v=f->volume;uint64_t n=0;char out[8]{};const auto h=v->file_open_read(v->context,"/ordinary.txt",&n);assert(h&&n==4&&v->file_read(v->context,h,out,4)==4&&!memcmp(out,"keep",4)&&v->file_close(v->context,h,true));const auto*e=risc_storage_volume_export(v);uint64_t t=0,sectors=0;uint32_t size=0;assert(!e->export_begin(v->context,&t,&sectors,&size)&&t&&size==512);assert(!e->export_end(v->context,t));}
static void edit(const risc_key_value_atomic_v1&endpoint,const char*value){risc_key_value_atomic_state_v1 state{sizeof state};assert(!endpoint.state(endpoint.context,&state));risc_key_value_atomic_mutation_v1 mutation{"setting",RISC_ATOMIC_PUT,value,uint32_t(strlen(value))};uint64_t token=0;assert(!endpoint.submit(endpoint.context,state.revision,&mutation,1,"explicit-test-edit",18,&token));risc_key_value_atomic_receipt_v1 receipt{sizeof receipt};int rc;do{schedule(1);rc=endpoint.collect(endpoint.context,token,&receipt);}while(rc==RISC_ATOMIC_PENDING);assert(!rc&&!receipt.status);}
static void editPair(const char*one,const char*six){const auto*api=risc_runtime_get_api(1);risc_runtime_capability_v1 grants[2]={{sizeof(risc_runtime_capability_v1)},{sizeof(risc_runtime_capability_v1)}};
 assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&grants[0]));assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,6,&grants[1]));
 edit(*static_cast<const risc_key_value_atomic_v1*>(grants[0].api),one);edit(*static_cast<const risc_key_value_atomic_v1*>(grants[1].api),six);
 assert(api->release(&grants[0]));const bool released=api->release(&grants[1]);
 if(scenario=="retained"){assert(!released&&L::retained()&&maintenance->retained()&&port->nativeStorageAttached());puts("last atomic release drains to terminal SD retention PASS");std::fflush(stdout);std::_Exit(0);}assert(released);
}

// Bounded everyday history, accumulated only by real successful checkpoints.
// One 1024-byte synthetic value per namespace serializes to 1044 bytes (one
// 4096-byte replica page). The first generation uses the same framing.
static std::string historyValue(unsigned ns,unsigned generation){
 char prefix[80];snprintf(prefix,sizeof prefix,"synthetic-ns-%u-generation-%u-",ns,generation);
 std::string value=prefix;value.resize(1024,char('a'+ns));return value;
}
static void historyEdit(unsigned retained,bool report,bool single=false){
 const auto*api=risc_runtime_get_api(1);risc_runtime_capability_v1 grants[2]={{sizeof(risc_runtime_capability_v1)},{sizeof(risc_runtime_capability_v1)}};
 assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&grants[0]));if(!single)assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,6,&grants[1]));
 const auto one=historyValue(1,retained+1),six=historyValue(6,retained+1);
 edit(*static_cast<const risc_key_value_atomic_v1*>(grants[0].api),one.c_str());
 if(!single){edit(*static_cast<const risc_key_value_atomic_v1*>(grants[1].api),six.c_str());assert(api->release(&grants[0]));}
 assert(cores[0]->identity().generation==retained&&cores[1]->identity().generation==retained);
 assert(cores[0]->pendingCount()==1&&cores[1]->pendingCount()==(single?0u:1u));
 unsigned before[11]{};for(unsigned i=1;i<=10;++i)before[i]=f->counter(i);
 const unsigned checkpointBefore=checkpoints,nvsBefore=F::nvsTotal,fsBefore=F::fsTotal;
 const uint64_t syntheticBefore=nowMs,schedulerCallsBefore=ownerScheduleCalls,schedulerMsBefore=ownerScheduleMs,workerDelayBefore=providerWorkerDelayMs,hardwareSleepBefore=hardwareSleepMs;const auto wallBefore=std::chrono::steady_clock::now();
 assert(api->release(&grants[single?0:1]));
 const auto wallNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-wallBefore).count();
 const auto timing=L::timing();
 assert(nowMs-syntheticBefore==ownerScheduleMs-schedulerMsBefore+providerWorkerDelayMs-workerDelayBefore+hardwareSleepMs-hardwareSleepBefore);
 assert(checkpoints==checkpointBefore+1&&L::exitSafe()&&maintenance->exitSafe()&&!port->nativeStorageAttached());
 assert(!cores[0]->pendingCount()&&!cores[1]->pendingCount());
 assert(cores[0]->identity().generation==retained+1&&cores[1]->identity().generation==retained+(single?0:1));
 assert(maintenance->result(1)==M::Sync::State::Synchronized&&maintenance->result(6)==M::Sync::State::Synchronized);
 if(report){
  printf("HISTORY_METRIC {\"checkpoint_kind\":\"%s\",\"retained_generations_per_namespace\":%u,\"namespace_generations_after\":[%u,%u],\"namespaces\":[1,6],\"keys_per_namespace\":1,\"value_bytes_per_namespace\":1024,\"map_bytes_per_namespace\":1044,\"replica_page_capacity_bytes\":4096,\"replica_file_bytes_per_generation_per_namespace\":1044,\"pages_per_generation_per_namespace\":1,\"owner_polls\":%u,\"raw_async_submit_attempts\":%u,\"raw_async_queued\":%u,\"package_submit_attempts\":%u,\"package_queued\":%u,\"total_queued\":%u,\"owner_scheduler_delay_calls\":%llu,\"owner_scheduler_delay_ms\":%llu,\"modeled_provider_worker_delay_ms\":%llu,\"modeled_hardware_sleep_ms\":%llu,\"synthetic_blocked_ms\":%llu,\"synthetic_elapsed_ms\":%llu,\"host_checkpoint_wall_ns\":%lld,\"card_read_calls\":%u,\"card_write_calls\":%u,\"native_nvs_calls\":%u,\"native_fs_calls\":%u,\"successful_namespaces\":%s,\"unchanged_namespaces\":%s}\n",
   single?"single":"paired",retained,retained+1,retained+(single?0:1),timing.lastPolls,f->counter(2)-before[2],f->counter(9)-before[9],f->counter(8)-before[8],f->counter(10)-before[10],f->counter(9)-before[9]+f->counter(10)-before[10],
   static_cast<unsigned long long>(ownerScheduleCalls-schedulerCallsBefore),static_cast<unsigned long long>(ownerScheduleMs-schedulerMsBefore),static_cast<unsigned long long>(providerWorkerDelayMs-workerDelayBefore),static_cast<unsigned long long>(hardwareSleepMs-hardwareSleepBefore),static_cast<unsigned long long>(timing.lastBlockedMs),static_cast<unsigned long long>(nowMs-syntheticBefore),static_cast<long long>(wallNs),f->counter(5)-before[5],f->counter(6)-before[6],F::nvsTotal-nvsBefore,F::fsTotal-fsBefore,single?"[1]":"[1,6]",single?"[6]":"[]");std::fflush(stdout);
 }
}
static void historyInventory(unsigned expected,unsigned expectedSix=0){
 if(!expectedSix)expectedSix=expected;
 const auto*v=f->volume;const auto*ext=risc_storage_volume_extension(v);
 for(unsigned ns:{1u,6u}){char path[32];snprintf(path,sizeof path,"/RTECFG/%08x",ns);const auto d=v->dir_open(v->context,path);assert(d);unsigned pages=0,index=0,other=0;risc_storage_dirent_v1 entry{};
  while(v->dir_next(v->context,d,&entry)){if(!strncmp(entry.name,"nr-",3)){assert(!entry.is_directory&&entry.size==1044);++pages;}else if(!strcmp(entry.name,"replica.index")){assert(!entry.is_directory&&entry.size==M::Sync::Checkpoint::IndexBytes);++index;}else ++other;}
  assert(ext->dir_close_checked(v->context,d)&&pages==(ns==6?expectedSix:expected)&&index==1&&!other);
 }
}

static void trigger(){const auto*api=risc_runtime_get_api(1);assert(api);risc_runtime_capability_v1 grant{sizeof grant};assert(api->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&grant));assert(api->release(&grant));assert(L::exitSafe()&&maintenance->exitSafe()&&!port->nativeStorageAttached());}
extern "C" void test_maintenance_app(){
 assert(L::ready()&&cores[0]->ready()&&cores[1]->ready());
 if(scenario.rfind("first",0)==0){
  if(scenario=="first-cancel"){assert(maintenance->result(1)==M::Sync::State::Cancelled&&cores[0]->pendingCount());cancelLatch=false;}
  for(unsigned i=0;i<3;++i)trigger();
  if(scenario=="first-conflict"){assert(!cores[0]->identity().known&&cores[0]->pendingCount()&&maintenance->result(1)==M::Sync::State::Conflict);assert(cores[1]->identity().generation==1&&!cores[1]->pendingCount());ordinaryMaintenance();assert(!anchorPin->release()&&!f);return;}
  assert(cores[0]->identity().generation==1&&cores[1]->identity().generation==1&&!cores[0]->pendingCount()&&!cores[1]->pendingCount());
 }
 const unsigned reads=f->counter(5),writes=f->counter(6),refresh=f->counter(1),nvs=F::nvsTotal,fs=F::fsTotal;
 for(unsigned i=0;i<8;++i)trigger();
 assert(reads==f->counter(5)&&writes==f->counter(6)&&refresh==f->counter(1)&&nvs==F::nvsTotal&&fs==F::fsTotal);
 if(scenario.rfind("first-history",0)==0){
  const bool single=scenario=="first-history-single";
  for(unsigned retained=1;retained<=100;++retained){const bool report=single?retained==100:retained==1||retained==10||retained==100;if(report)historyInventory(retained);historyEdit(retained,report,single&&retained==100);}
  historyInventory(101,single?100:101);ordinaryMaintenance();assert(!anchorPin->release()&&!f);return;
 }
 if(scenario=="save-exit"||scenario=="absent"){const auto writes=f->counter(6);editPair("final-prefs","final-wifi");assert(L::exitSafe()&&maintenance->exitSafe());
  if(scenario=="absent"){assert(cores[0]->pendingCount()==1&&cores[1]->pendingCount()==1&&f->counter(6)==writes&&maintenance->result(1)==M::Sync::State::Deferred&&maintenance->result(6)==M::Sync::State::Deferred);releaseAnchorAtFinal=true;}
  else {assert(!cores[0]->pendingCount()&&!cores[1]->pendingCount()&&f->counter(6)>writes);ordinaryMaintenance();assert(!anchorPin->release()&&!f);}return;
 }
 if(scenario=="implicit-exit"){const auto*api=risc_runtime_get_api(1);risc_runtime_capability_v1 grants[2]={{sizeof(risc_runtime_capability_v1)},{sizeof(risc_runtime_capability_v1)}};assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&grants[0])&&api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,6,&grants[1]));edit(*static_cast<const risc_key_value_atomic_v1*>(grants[0].api),"implicit-prefs");edit(*static_cast<const risc_key_value_atomic_v1*>(grants[1].api),"implicit-wifi");assert(cores[0]->pendingCount()&&cores[1]->pendingCount());releaseAnchorAtFinal=true;return;}
 if(scenario=="draft-fence"){
  const auto*api=risc_runtime_get_api(1);risc_runtime_capability_v1 grant{sizeof grant};assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&grant));auto endpoint=*static_cast<const risc_key_value_atomic_v1*>(grant.api);
  edit(endpoint,"older-unsynced");risc_key_value_atomic_state_v1 captured{sizeof captured};assert(!endpoint.state(endpoint.context,&captured));
  trigger();assert(cores[0]->pendingCount()==1&&cores[0]->revision()==captured.revision);
  risc_key_value_atomic_mutation_v1 mutation{"setting",RISC_ATOMIC_PUT,"new-UI-edit",11};uint64_t token=0;
  assert(!endpoint.submit(endpoint.context,captured.revision,&mutation,1,"captured-intent",16,&token));risc_key_value_atomic_receipt_v1 receipt{sizeof receipt};int rc;do{schedule(1);rc=endpoint.collect(endpoint.context,token,&receipt);}while(rc==RISC_ATOMIC_PENDING);assert(!rc&&!receipt.status);
  token=99;assert(endpoint.submit(endpoint.context,captured.revision,&mutation,1,"stale-captured-intent",21,&token)==RISC_ATOMIC_STALE&&!token);
  assert(api->release(&grant)&&!cores[0]->pendingCount());assert(F::value(*cores[0],"setting")=="new-UI-edit");ordinaryMaintenance();assert(!anchorPin->release()&&!f);puts("live atomic draft survives ordinary acquisition; obsolete revision still refuses; final release SD-acks PASS");return;
 }
 if(scenario=="pre-capture"){const auto*e=risc_storage_volume_export(f->volume);uint64_t t=0,n=0;uint32_t size=0;assert(!e->export_begin(f->volume->context,&t,&n,&size));editPair("old-prefs","old-wifi");assert(cores[0]->pendingCount()&&cores[1]->pendingCount());assert(!e->export_end(f->volume->context,t));const auto*api=risc_runtime_get_api(1);risc_runtime_capability_v1 grant{sizeof grant};assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&grant));assert(!cores[0]->pendingCount()&&!cores[1]->pendingCount());assert(api->release(&grant));ordinaryMaintenance();assert(!anchorPin->release()&&!f);puts("first atomic grant is published after both older namespaces settle on SD PASS");return;}
 if(scenario=="idle"){ordinaryMaintenance();assert(!anchorPin->release()&&!f);return;}
 const unsigned base=scenario.rfind("first",0)==0?2:scenario=="persisted-incarnation"?21:20;
 for(unsigned generation=base;generation<base+3;++generation){
  uint64_t usbToken=0;const auto*exportApi=risc_storage_volume_export(f->volume);
  if(scenario=="usb-before"&&generation==20){uint64_t sectors=0;uint32_t size=0;assert(!exportApi->export_begin(f->volume->context,&usbToken,&sectors,&size));}
  if(scenario=="cancel"&&generation==20)cancellationArmed=true;
  if(scenario=="retained"&&generation==20)f->fault(5);
  editPair(generation==20?"prefs-a":generation==21?"prefs-b":"prefs-c",generation==20?"wifi-a":generation==21?"wifi-b":"wifi-c");
  if(usbToken){assert(cores[0]->pendingCount()&&cores[1]->pendingCount());assert(!exportApi->export_end(f->volume->context,usbToken));}
  if(scenario=="cancel"&&generation==20){assert(cores[0]->pendingCount()&&L::timing().cancellationRequested&&maintenance->result(1)==M::Sync::State::Cancelled);cancelLatch=false;}
  trigger();trigger();
  assert(cores[1]->identity().generation==generation&&!cores[1]->pendingCount());
  if(scenario=="foreign"||scenario=="no-incarnation"){assert(cores[0]->identity().generation==19&&cores[0]->pendingCount()&&maintenance->result(1)==(scenario=="foreign"?M::Sync::State::Conflict:M::Sync::State::Failed));}
  else assert(cores[0]->identity().generation==generation&&!cores[0]->pendingCount());
  ordinaryMaintenance();
 }
 assert(!anchorPin->release()&&!f);
}
int main(int argc,char**argv){assert(argc==4);scenario=argv[2];F::rootPath=argv[3];F::empty();const auto one=map("one"),six=map("six");auto oneId=identity(1,one),sixId=identity(6,six);
 {F::Fixture a(1),b(6);C::Value v1{"setting","one",3},v6{"setting","six",3};
  if(scenario.rfind("first",0)==0){freshBaseline(a,scenario.rfind("first-history",0)==0?historyValue(1,1).c_str():"one");freshBaseline(b,scenario.rfind("first-history",0)==0?historyValue(6,1).c_str():"six");}
  else {if(scenario.rfind("restore",0)!=0){assert(!a.core.initialize(&v1,1,oneId));a.store.finishOperation();}if(scenario.rfind("restore",0)!=0||scenario=="restore-single"){assert(!b.core.initialize(&v6,1,sixId));b.store.finishOperation();}
   if(scenario=="persisted-incarnation"){for(auto*f:{&a,&b}){C::Snapshot snapshot;assert(!f->core.captureSnapshot(&snapshot));auto target=f->core.identity();++target.generation;const uint8_t persisted[16]={0x55};C::ExportMarker marker;assert(!f->core.prepareExport(snapshot,target,persisted,&marker));f->store.finishOperation();assert(!f->core.acknowledgeExport(marker,target));f->store.finishOperation();assert(!f->core.exportIncarnationRequired());}++oneId.generation;++sixId.generation;}
  }
 }

#if RISC_NATIVE_SETTINGS_ASSEMBLY
 if(!assembleMaintenance())return 0;
#else
 auto*a=new F::Fixture(1);auto*b=new F::Fixture(6);cores[0]=&a->core;cores[1]=&b->core;Hash h1,h6;const uint8_t incarnation1[16]={0x71},incarnation6[16]={0x76};static const L::Writer writers[]={{"maintenance-test",1},{"maintenance-test",6}};
 B*bootstrapClient=nullptr;
 if(scenario.rfind("restore",0)==0||scenario.rfind("first",0)==0){auto*s1=new Sources(1,nativeOwner,a->inspector,sourceHooks(*a));auto*s6=new Sources(6,nativeOwner,b->inspector,sourceHooks(*b));
  B::First::Authority first{nullptr,[](void*){return F::lease.load();},[](void*,uint8_t out[16]){static uint8_t serial=33;memset(out,0,16);out[0]=serial++;return true;},[](void*,uint32_t,uint32_t){return F::lease.load();}};
  bootstrapClient=new B({&a->core,&a->store,h1.api(),C::heap(),scenario=="restore-no-source"?nullptr:s1,first},{&b->core,&b->store,h6.api(),C::heap(),s6,first},9);
 }
 maintenance=new M({&a->core,&a->store,h1.api(),C::heap(),scenario=="no-incarnation"?nullptr:incarnation1},{&b->core,&b->store,h6.api(),C::heap(),incarnation6},9,writers,2,bootstrapClient);
#endif
 C&oneCore=*cores[0];C&sixCore=*cores[1];
 F::onWorker=false;F::lease=false;
 assert(L::prepare(&maintenance->admission(),{owner,schedule,[](){return uint32_t(nowMs);},[](){return cancelLatch;}}));
 auto hw=hardware();hw.sleep=[](uint32_t ms){hardwareSleepMs+=ms;nowMs+=ms;};workBackend.delayMs=[](uint32_t ms){providerWorkerDelayMs+=ms;nowMs+=ms;};
 Port cpu(hw);port=&cpu;RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;},bind};
 boot.appExitSafe=[](){return port->appExitSafe()&&L::exitSafe();};boot.providerStorageSafe=[](){return port->providerStorageSafe()&&!L::retained();};boot.retainedDelay=schedule;boot.nativeOwnerCheckpoint=[](bool admitNew){++checkpoints;const bool safe=L::checkpoint(admitNew);if(safe&&releaseAnchorAtFinal){assert(admitNew&&!anchorPin->release()&&!f);releaseAnchorAtFinal=false;}return safe;};
 static const RiscBoot::KeyValueBackend forbiddenLegacy{nullptr,[](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{assert(false);return RISC_KEY_VALUE_IO;},[](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{assert(false);return RISC_KEY_VALUE_IO;},2048};boot.keyValue=&forbiddenLegacy;
 RiscBoot::Runtime runtime(boot);assert(L::bind(runtime));if(!runtime.prepare(argv[1])){fprintf(stderr,"prepare: %s\n",runtime.error());assert(false);}Pin anchor(runtime,cpu,9);anchorPin=&anchor;assert(!anchor.acquire());if(scenario!="absent"){P fresh(runtime,cpu,1,9);bootstrap(fresh);assert(!fresh.release());}
 if(scenario!="absent"){const auto*e=risc_storage_volume_extension(f->volume);assert(e->mkdir(f->volume->context,"/RTECFG"));auto remote=oneId;if(scenario=="foreign")remote.lineage[0]^=42;if(scenario!="restore-no-checkpoint"){if(scenario.rfind("first",0)!=0||scenario=="first-conflict")seedMaintenance(1,one,remote);if(scenario.rfind("first",0)!=0)seedMaintenance(6,six,sixId);}file("/ordinary.txt","keep",4);}else f->fault(8);
 if(scenario=="restore-conflict")F::nvs["rte00000001"]["valuable"]={1,2,3};
 if(scenario=="restore-resume"){
  const pid_t child=fork();assert(child>=0);if(!child){F::powerCheckpoint=[](const char*at){if(strcmp(at,"nvs-commit-after"))return;saveNvs();std::_Exit(73);};(void)L::hydrate(runtime,cpu);std::_Exit(95);}
  int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);loadNvs();assert(!oneCore.ready());
 }
 concurrentNative=scenario.find("concurrent")!=std::string::npos;std::atomic<bool>stopNative{false};std::thread nativeThreads[2];
 if(concurrentNative)for(unsigned i=0;i<2;++i)nativeThreads[i]=std::thread([&,i]{task=Task::NativeSettings;F::onWorker=true;onNamespace=i?6:1;while(!stopNative.load(std::memory_order_acquire)){services[i]->workerStep();std::this_thread::yield();}});
 if(scenario=="corrupt-native")F::nvs.at("rnr00000001").at("root")[0]^=1;
 const auto nativeBefore=F::nvs;const auto writesBefore=f->counter(6);
 if(scenario=="restore-cancel")cancellationArmed=true;
 if(scenario=="restore-retained")f->fault(3);
#if RISC_NATIVE_SETTINGS_ASSEMBLY
 if(scenario=="payload-failure")failPayload=true;
 const auto payloadBefore=payloadRequests;
#endif
 const bool hydrated=L::hydrate(runtime,cpu);
#if RISC_NATIVE_SETTINGS_ASSEMBLY
 if(scenario=="payload-failure"){assert(!hydrated&&!L::ready()&&L::exitSafe()&&!L::retained()&&F::nvs==nativeBefore&&payloadRequests>payloadBefore&&!oneCore.ready());ordinaryMaintenance();assert(!anchor.release()&&!f);puts("PSRAM allocation failure refuses hydration without fallback, mutation or retention PASS");std::fflush(stdout);std::_Exit(0);}
#endif
 if(scenario=="restore-retained"){assert(!hydrated&&L::retained()&&maintenance->retained()&&port->nativeStorageAttached());puts("actual pre-app restore retention PASS");std::fflush(stdout);std::_Exit(0);}
 if(scenario=="restore-no-source"||scenario=="restore-conflict"||scenario=="restore-cancel"||scenario=="restore-no-checkpoint"||scenario=="corrupt-native"){assert(!hydrated&&!L::ready()&&maintenance->exitSafe()&&F::nvs==nativeBefore&&f->counter(6)==writesBefore&&!oneCore.ready());ordinaryMaintenance();assert(!anchor.release()&&!f);printf("actual lifecycle pre-app %s refuses unchanged PASS\n",scenario.c_str());std::fflush(stdout);std::_Exit(0);}
 assert(hydrated&&L::attachOwner(runtime,cpu));
 if(scenario=="first-cancel")cancellationArmed=true;
if(!runtime.run()){fprintf(stderr,"run: %s\n",runtime.error());assert(false);}assert(checkpoints>((scenario=="first-conflict"||scenario=="draft-fence")?0u:8u)&&L::exitSafe());assert(anchor.exitSafe()&&!f);
 if(scenario=="save-exit"||scenario=="implicit-exit"){assert(!oneCore.pendingCount()&&!sixCore.pendingCount()&&oneCore.identity().generation==20&&sixCore.identity().generation==20);puts("last explicit atomic release / implicit final app teardown acknowledge BOTH namespaces on available SD PASS");}
 if(scenario=="absent")assert(oneCore.pendingCount()==1&&sixCore.pendingCount()==1&&L::exitSafe()&&!L::retained());
 if(concurrentNative){stopNative.store(true,std::memory_order_release);for(auto&t:nativeThreads)t.join();}
#if RISC_NATIVE_SETTINGS_ASSEMBLY
 assert(payloadRequests>0&&payloadPeak>0);
 printf("Factory PSRAM allocator coverage PASS; requests=%u; peak-vendor-bytes=%zu; live-vendor-bytes=%zu\n",payloadRequests,payloadPeak,payloadBytes);
#endif
 printf("actual lifecycle/Runtime/X4/FatFs maintenance %s PASS; control=%zu; polls=%u\n",scenario.c_str(),sizeof(M),L::timing().lastPolls);std::fflush(stdout);std::_Exit(0);
}
