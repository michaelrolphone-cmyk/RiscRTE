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
static std::unique_ptr<Coordinator> services[2];static unsigned onNamespace=0;static M*maintenance;static Pin*anchorPin;static F::Fixture*stores[2];static std::string scenario;static bool cancelLatch=false,cancellationArmed=false;static unsigned checkpoints=0;
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
bool esp_ptr_internal(const void*p){return p!=nullptr;}
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
static void saveNvs(){std::ofstream out(F::rootPath+".receipt",std::ios::binary);auto write=[&](uint32_t n){out.write(reinterpret_cast<const char*>(&n),sizeof n);};write(F::nvs.size());for(const auto&space:F::nvs){write(space.first.size());out.write(space.first.data(),space.first.size());write(space.second.size());for(const auto&row:space.second){write(row.first.size());out.write(row.first.data(),row.first.size());write(row.second.size());out.write(reinterpret_cast<const char*>(row.second.data()),row.second.size());}}out.close();assert(out);}
static void loadNvs(){std::ifstream in(F::rootPath+".receipt",std::ios::binary);auto read=[&](){uint32_t n;in.read(reinterpret_cast<char*>(&n),sizeof n);assert(in);return n;};F::nvs.clear();const auto count=read();assert(count<8);for(unsigned i=0;i<count;++i){auto n=read();assert(n<32);std::string space(n,' ');in.read(space.data(),n);const auto rows=read();assert(rows<8);for(unsigned j=0;j<rows;++j){n=read();assert(n<32);std::string key(n,' ');in.read(key.data(),n);n=read();assert(n<1000);auto&bytes=F::nvs[space][key];bytes.resize(n);in.read(reinterpret_cast<char*>(bytes.data()),n);}}assert(in);}
static bool lastNativeWorked=false;
static void schedule(uint32_t ms){lastNativeWorked=false;assert(owner());nowMs+=ms;
 if(cancellationArmed&&port->nativeStorageAttached()){cancelLatch=true;cancellationArmed=false;}
 if(!mailbox.idle())step();
 for(unsigned i=0;i<2;++i)if(services[i]){task=Task::NativeSettings;F::onWorker=true;onNamespace=i?6:1;lastNativeWorked|=services[i]->workerStep();F::onWorker=false;onNamespace=0;task=Task::Owner;}
}
static void le(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
static Bytes map(const char*value){Bytes b(20+strlen(value));strcpy(reinterpret_cast<char*>(b.data()),"setting");le(b.data()+16,strlen(value));memcpy(b.data()+20,value,strlen(value));return b;}
static C::Identity identity(uint32_t ns,const Bytes&map){Bytes b(12);le(b.data(),ns);le(b.data()+4,1);le(b.data()+8,map.size());b.insert(b.end(),map.begin(),map.end());C::Identity id{};id.known=true;id.generation=19;id.lineage[0]=ns;unsigned n=0;assert(EVP_Digest(b.data(),b.size(),id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;}
static void file(const char*path,const void*bytes,uint32_t n){const auto*v=f->volume;const auto*e=risc_storage_volume_extension(v);const auto h=v->file_open_write(v->context,path);assert(h);assert(v->file_write(v->context,h,bytes,n)==n&&e->file_sync(v->context,h)&&v->file_close(v->context,h,true));}
static void seedMaintenance(uint32_t ns,const Bytes&bytes,C::Identity id){const auto*v=f->volume;const auto*e=risc_storage_volume_extension(v);char directory[64],page[160],digest[65];snprintf(directory,sizeof directory,"/RTECFG/%08x",ns);assert(e->mkdir(v->context,directory));uint8_t index[M::Sync::Checkpoint::IndexBytes];M::Sync::Checkpoint::makeIndex(index,ns,bytes.size(),1,id);std::string path=std::string(directory)+"/replica.index";file(path.c_str(),index,sizeof index);for(unsigned i=0;i<32;++i)snprintf(digest+2*i,3,"%02x",id.digest[i]);snprintf(page,sizeof page,"%s/nr-%08x%08x-%s-%08x",directory,unsigned(id.generation>>32),unsigned(id.generation),digest,0);file(page,bytes.data(),bytes.size());}
static void ordinaryMaintenance(){const auto*v=f->volume;uint64_t n=0;char out[8]{};const auto h=v->file_open_read(v->context,"/ordinary.txt",&n);assert(h&&n==4&&v->file_read(v->context,h,out,4)==4&&!memcmp(out,"keep",4)&&v->file_close(v->context,h,true));const auto*e=risc_storage_volume_export(v);uint64_t t=0,sectors=0;uint32_t size=0;assert(!e->export_begin(v->context,&t,&sectors,&size)&&t&&size==512);assert(!e->export_end(v->context,t));}

#include "runtime_native_extra_fixture.h"
static const char*mediaPath;static const char*expectedPath;static bool restoring=false;
static void(*loadMedia)(const char*);static void(*saveMedia)(const char*);
extern "C" void native_settings_sd_media(void(*load)(const char*),void(*save)(const char*)){loadMedia=load;saveMedia=save;}
static uint64_t usbToken=0,hciRevision=0;static unsigned hciReads=0,hciWrites=0,hciBoundaries=0,atomicReleases=0,admissionDeferred=0,saveChecks=0;
extern "C" void native_settings_yield(void){schedule(1);}
#if ATTACHED_WIFI
extern "C" void runtime_atomic_wifi_controller(const risc_runtime_api_v1*,unsigned);
extern "C" int runtime_atomic_worker_step(void){schedule(1);return lastNativeWorked?1:0;}
#endif
extern "C" void native_attached_usb(bool begin){
 const auto*e=risc_storage_volume_export(f->volume);assert(e);
 if(begin){uint64_t sectors=0;uint32_t size=0;assert(!usbToken&&!e->export_begin(f->volume->context,&usbToken,&sectors,&size)&&usbToken&&size==512);}
 else {assert(usbToken&&stores[0]->core.pendingCount()==1&&stores[0]->core.identity().generation==19);assert(!e->export_end(f->volume->context,usbToken));usbToken=0;assert(stores[0]->core.pendingCount()==1);puts("Actual alias edit deferred under real USB admission, pending before the next BLE capture PASS");}
}
extern "C" void native_attached_hci_boundary(uint64_t revision,bool after){
 assert(!usbToken&&revision==stores[0]->core.revision()&&!stores[0]->core.pendingCount());
 if(!after){hciRevision=revision;hciReads=f->counter(5);hciWrites=f->counter(6);assert(stores[0]->core.identity().generation==20+hciBoundaries);}
 else {assert(revision==hciRevision&&hciReads==f->counter(5)&&hciWrites==f->counter(6));++hciBoundaries;puts("Actual BLE captured revision survives real Runtime HCI-provider acquisition without SD I/O PASS");}
}
extern "C" void native_attached_atomic_release(bool before){
 if(before){assert(stores[0]->core.pendingCount()==(atomicReleases==1?2u:1u));}
 else {++atomicReleases;if(usbToken)assert(stores[0]->core.pendingCount()==1);else assert(!stores[0]->core.pendingCount()&&L::exitSafe()&&maintenance->exitSafe()&&!port->nativeStorageAttached());}
}
extern "C" void native_attached_saved(void){
 ++saveChecks;assert(!stores[0]->core.pendingCount()&&stores[0]->core.identity().generation==20+saveChecks&&maintenance->result(1)==M::Sync::State::Synchronized&&L::exitSafe()&&maintenance->exitSafe());ordinaryMaintenance();
 puts("Actual BLE terminal atomic release reaches settled Runtime SD acknowledgement PASS");
}
extern "C" void native_extra_verify_reads(const risc_runtime_api_v1*rt){
 for(unsigned version=1;version<=2;++version){risc_runtime_capability_v1 grant{sizeof grant};assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,version,1,&grant));const auto*api=static_cast<const risc_key_value_v1*>(grant.api);
  if(version==1)runtime_native_settings_preferences(api,0);
  for(unsigned i=0;const char*key=native_extra_key(i);++i){uint8_t expected[2048]{},actual[2048]{};uint32_t need=0,size=0;assert(!stores[0]->core.get(key,expected,sizeof expected,&need)&&!api->get(api->context,key,actual,sizeof actual,&size)&&size==need&&!memcmp(expected,actual,size));assert(api->put(api->context,key,actual,size)==RISC_KEY_VALUE_CONTEXT);}
  assert(rt->release(&grant));
 }
}
static Bytes snapshot(C&core){C::Snapshot s;assert(!core.captureSnapshot(&s));return {s.map(),s.map()+s.mapSize()};}
static void expected(bool compare){for(unsigned i=0;i<2;++i){const std::string path=std::string(expectedPath)+"-"+std::to_string(i?6:1)+".bin";const auto actual=snapshot(stores[i]->core);if(compare){std::ifstream in(path,std::ios::binary);assert(in);Bytes prior{std::istreambuf_iterator<char>(in),{}};assert(prior==actual);}else{std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(actual.data()),actual.size());assert(out);}}}
extern "C" void test_maintenance_app(){
 assert(L::ready()&&stores[0]->core.ready()&&stores[1]->core.ready());if(restoring)expected(true);
 runtime_native_settings_controller(risc_runtime_get_api(1),restoring?1:0,"attached-ble");
#if ATTACHED_WIFI
 runtime_atomic_wifi_controller(risc_runtime_get_api(1),restoring?1:0);
 assert(!stores[1]->core.pendingCount()&&stores[1]->core.identity().generation==20&&L::exitSafe()&&maintenance->exitSafe());
 puts(restoring?"Attached Wi-Fi: fresh actual controller reads both exact restored profiles PASS":"Attached Wi-Fi: actual Save/Remember and queued cancel; final real atomic release acknowledges both profiles to SD PASS");
#endif
 if(!restoring){assert(atomicReleases==3&&hciBoundaries==2&&saveChecks==2&&admissionDeferred>0);expected(false);std::ofstream witness(F::rootPath+"/simulated-nvs-erasure-witness.txt");for(auto&space:F::nvs)for(auto&row:space.second)witness<<space.first<<'/'<<row.first<<':'<<row.second.size()<<'\n';assert(witness);}
 assert(!stores[0]->core.pendingCount()&&!stores[1]->core.pendingCount()&&L::exitSafe()&&maintenance->exitSafe());ordinaryMaintenance();assert(saveMedia);saveMedia(mediaPath);assert(!anchorPin->release()&&!f);
}
int main(int argc,char**argv){
 assert(argc==6);F::rootPath=argv[2];mediaPath=argv[3];expectedPath=argv[4];restoring=!strcmp(argv[5],"restore");F::empty();
 const auto one=map("one"),six=map("six");const auto oneId=identity(1,one),sixId=identity(6,six);
 if(!restoring){F::Fixture a(1),b(6);C::Value v1{"setting","one",3},v6{"setting","six",3};assert(!a.core.initialize(&v1,1,oneId));a.store.finishOperation();assert(!b.core.initialize(&v6,1,sixId));b.store.finishOperation();puts("PRECONDITION: synthetic matching initial generation-19 native/SD maps and initial incarnation");}
 auto*a=new F::Fixture(1);auto*b=new F::Fixture(6);stores[0]=a;stores[1]=b;Hash h1,h6;
 auto*s1=new Sources(1,nativeOwner,a->inspector,sourceHooks(*a));auto*s6=new Sources(6,nativeOwner,b->inspector,sourceHooks(*b));
 auto*bootstrapClient=new B({&a->core,&a->store,h1.api(),C::heap(),s1,{}},{&b->core,&b->store,h6.api(),C::heap(),s6,{}},9);
 const uint8_t incarnation1[16]={0x71},incarnation6[16]={0x76};static const L::Writer writers[]={{"maintenance-test",1},{"maintenance-test",6}};
 maintenance=new M({&a->core,&a->store,h1.api(),C::heap(),incarnation1},{&b->core,&b->store,h6.api(),C::heap(),incarnation6},9,writers,2,bootstrapClient);
 F::onWorker=false;F::lease=false;assert(L::prepare(&maintenance->admission(),{owner,schedule,[](){return uint32_t(nowMs);},[](){return false;}}));
 Port cpu(hardware());port=&cpu;RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;},bind};
 boot.appExitSafe=[](){return port->appExitSafe()&&L::exitSafe();};boot.providerStorageSafe=[](){return port->providerStorageSafe()&&!L::retained();};boot.retainedDelay=schedule;
 boot.nativeOwnerCheckpoint=[](bool admitNew){++checkpoints;if(!admitNew)++admissionDeferred;return L::checkpoint(admitNew);};
 static const RiscBoot::KeyValueBackend forbiddenLegacy{nullptr,[](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{assert(false);return RISC_KEY_VALUE_IO;},[](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{assert(false);return RISC_KEY_VALUE_IO;},2048};boot.keyValue=&forbiddenLegacy;
 RiscBoot::Runtime runtime(boot);assert(L::bind(runtime));if(!runtime.prepare(argv[1])){fprintf(stderr,"prepare: %s\n",runtime.error());assert(false);}Pin anchor(runtime,cpu,9);anchorPin=&anchor;assert(!anchor.acquire());
 if(restoring){assert(loadMedia);loadMedia(mediaPath);}
 {P fresh(runtime,cpu,1,9);bootstrap(fresh);assert(!fresh.release());}
 if(!restoring){const auto*e=risc_storage_volume_extension(f->volume);assert(e->mkdir(f->volume->context,"/RTECFG"));seedMaintenance(1,one,oneId);seedMaintenance(6,six,sixId);file("/ordinary.txt","keep",4);}
 const auto writes=f->counter(6);assert(L::hydrate(runtime,cpu)&&L::attachOwner(runtime,cpu));if(restoring)assert(f->counter(6)==writes);
 if(!runtime.run()){fprintf(stderr,"run: %s\n",runtime.error());assert(false);}assert(L::exitSafe()&&anchor.exitSafe()&&!f);
 printf("ATTACHED BLE actual Runtime lifecycle / X4 FatFs / %s PASS; checkpoint admissions=%u deferred=%u\n",restoring?"typed pre-app restore after complete native erasure":"controller capture, compensation and settled SD acknowledgement",checkpoints,admissionDeferred);std::fflush(stdout);std::_Exit(0);
}
