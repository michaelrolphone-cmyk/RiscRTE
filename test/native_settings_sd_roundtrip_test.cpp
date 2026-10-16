// Source-bound controller -> Runtime -> checked native worker -> actual
// Runtime/CpuPort/X4/FatFs checkpoint -> full host-native erasure -> typed restore.
// Keep the two System closures in separate executables; no mixed System revision.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main lower_controller_main
#if SETTINGS_NAMESPACE == 1
#include "runtime_native_settings_preferences_test.cpp"
#else
#include "runtime_native_settings_test.cpp"
#endif
#undef main
#define NATIVE_SD_PORT_OWNER sdPortOwner
#define NATIVE_SD_PORT_OWNER_PREDICATE (task==Task::Owner&&owner())
#define NATIVE_SD_PORT_BIND sdPortBind
#define main lower_port_main
#include "native_sd_port_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "experimental/native_replica/NativeSdSynchronization.h"
#include "experimental/native_replica/CheckedSdNativeRestore.h"
#include <openssl/evp.h>
#include <dlfcn.h>
#include <set>
#include "native_settings_payload_tracker.h"
#include "RuntimeFileFixture.h"
using Sync=RiscExperimental::NativeSdSynchronization;
using Restore=RiscExperimental::CheckedSdNativeRestore;
using Reader=RiscExperimental::CheckedSdReplicaReader;
using Catalog=RiscExperimental::CheckedSdSettingsCatalog;
using Sources=RiscExperimental::CheckedNvsFreshSources;
static constexpr uint32_t ns=SETTINGS_NAMESPACE;
struct Iterator {std::string space;std::vector<std::string>keys;size_t at=0;};
static std::set<Iterator*>iterators;
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){++NF::nvsTotal;NF::safe();assert(NF::handles.count(h));*n=NF::nvs[NF::handles.at(h).name].size();return ESP_OK;}
extern "C" nvs_iterator_t nvs_entry_find(const char*partition,const char*space,nvs_type_t type){++NF::nvsTotal;NF::safe();assert(!strcmp(partition,"nvs")&&type==NVS_TYPE_ANY);auto found=NF::nvs.find(space);if(found==NF::nvs.end()||found->second.empty())return nullptr;auto*i=new Iterator;iterators.insert(i);i->space=space;for(auto&e:found->second)i->keys.push_back(e.first);return i;}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t it){++NF::nvsTotal;NF::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));if(++i->at<i->keys.size())return i;iterators.erase(i);delete i;return nullptr;}
extern "C" void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){++NF::nvsTotal;NF::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));*out={};strcpy(out->namespace_name,i->space.c_str());strcpy(out->key,i->keys.at(i->at).c_str());out->type=NVS_TYPE_BLOB;}
extern "C" void nvs_release_iterator(nvs_iterator_t it){++NF::nvsTotal;NF::safe();auto*i=static_cast<Iterator*>(it);if(i){assert(iterators.erase(i));delete i;}}
struct Hash {
 EVP_MD_CTX*ctx=EVP_MD_CTX_new();~Hash(){EVP_MD_CTX_free(ctx);}
 Reader::Digest reader(){return {this,[](void*p){return EVP_DigestInit_ex(static_cast<Hash*>(p)->ctx,EVP_sha256(),nullptr)==1;},[](void*p,const void*b,uint32_t n){return EVP_DigestUpdate(static_cast<Hash*>(p)->ctx,b,n)==1;},[](void*p,uint8_t*out){unsigned n=0;return EVP_DigestFinal_ex(static_cast<Hash*>(p)->ctx,out,&n)==1&&n==32;}};}
 Restore::Digest native(){auto d=reader();return {d.context,d.begin,d.update,d.finish};}
};
struct Client {
 Coordinator&service;Coordinator::Lease hold;uint32_t slot=0;Catalog reader;Restore restore;
 Client(Coordinator&s,NF::Fixture&f,Sources&sources,Hash&readHash,Hash&nativeHash):service(s),reader(f.core,readHash.reader(),{this,[](void*p){auto&s=*static_cast<Client*>(p);return s.service.nativeOwner(s.hold);},[](void*p){auto&s=*static_cast<Client*>(p);return s.service.nativeWorker(s.slot);}},payloadTracker.api()),restore(f.core,f.store,sources,nativeHash.native(),{this,[](void*){return owner();},[](void*){return NF::onWorker;},[](void*p){auto&s=*static_cast<Client*>(p);return s.service.nativeWorker(s.slot);},[](void*p){return static_cast<Client*>(p)->hold.serial;}}){}
 Coordinator::NativeClient registration(){return {this,sizeof(*this),[](void*p){auto&s=*static_cast<Client*>(p);return s.reader.preparationWorkerStep()||s.restore.workerStep();},[](void*p){auto&s=*static_cast<Client*>(p);return s.reader.state()==Catalog::State::Running||s.restore.pending();},[](void*p){auto&s=*static_cast<Client*>(p);return s.reader.exitSafe()&&s.restore.exitSafe();},[](void*p){auto&s=*static_cast<Client*>(p);return s.reader.state()==Catalog::State::Retained||s.restore.retained();}};}
};
static C::Identity emptyIdentity(){
 uint8_t prefix[12]{};prefix[0]=ns;C::Identity id{};id.known=true;id.generation=19;id.lineage[0]=42;id.lineage[1]=ns;
 unsigned n=0;assert(EVP_Digest(prefix,sizeof prefix,id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;
}
static void sdFile(const char*path,const void*bytes,size_t n){
 const auto*v=f->volume;const auto*ext=risc_storage_volume_extension(v);const uint32_t h=v->file_open_write(v->context,path);
 assert(h&&v->file_write(v->context,h,bytes,n)==n&&ext->file_sync(v->context,h)&&v->file_close(v->context,h,true));
}
static void seedSd(const C::Identity&id){
 const auto*v=f->volume;const auto*ext=risc_storage_volume_extension(v);if(ns==1)assert(ext->mkdir(v->context,"/RTECFG"));
 char path[128];snprintf(path,sizeof path,"/RTECFG/%08x",ns);assert(ext->mkdir(v->context,path));
 snprintf(path,sizeof path,"/RTECFG/%08x/replica.index",ns);uint8_t index[Sync::Checkpoint::IndexBytes]{};
 Sync::Checkpoint::makeIndex(index,ns,0,0,id);sdFile(path,index,sizeof index);
 if(ns==1)sdFile("/ordinary.txt","ordinary-independent",20);
}
static void independentFileAndUsb(){
 assert(!port->nativeStorageAttached()&&port->appExitSafe());const auto*v=f->volume;uint64_t size=0;char bytes[32]{};
 const uint32_t h=v->file_open_read(v->context,"/ordinary.txt",&size);
 assert(h&&size==20&&v->file_read(v->context,h,bytes,20)==20&&!memcmp(bytes,"ordinary-independent",20)&&v->file_close(v->context,h,true));
 const auto*e=risc_storage_volume_export(v);uint64_t token=0,sectors=0;uint32_t sectorSize=0;
 assert(e&&!e->export_begin(v->context,&token,&sectors,&sectorSize)&&token&&sectors&&sectorSize==512);
 unsigned char sector[512]{};assert(!e->export_read(v->context,token,0,1,sector)&&sector[510]==0x55&&sector[511]==0xaa);
 assert(!e->export_end(v->context,token));
}
static void(*loadMedia)(const char*);static void(*saveMedia)(const char*);
extern "C" void native_settings_sd_media(void(*load)(const char*),void(*save)(const char*)){loadMedia=load;saveMedia=save;}
static void media(const std::string&,const char*which,const char*path){
 assert(f&&loadMedia&&saveMedia);if(!strcmp(which,"native_settings_sd_load"))loadMedia(path);else saveMedia(path);
}
static void seedNative(){
 NF::Fixture fixture(ns,payloadTracker.api());const auto id=emptyIdentity();
 std::thread worker([&]{NF::onWorker=true;NF::lease=true;assert(!fixture.core.initialize(nullptr,0,id));fixture.store.finishOperation();assert(fixture.store.exitSafe());NF::lease=false;});worker.join();
 puts("PRECONDITION: explicitly synthetic matching empty native/SD baseline, generation 19; no first-boot/product policy claim");
}
static void app(){
 RiscTest::FileSources currentFiles0({directory.c_str()});R current({owner,health,delay,logLine,bind,&forbiddenLegacy});assert(currentFiles0.bind(current));runtime=&current;
 if(!current.prepare(directory.c_str())){fprintf(stderr,"controller prepare: %s\n",current.error());assert(false);}
 assert(current.run()&&!current.retained()&&current.residentResetSafe());invalid(savedApi);
}
static Bytes snapshot(C&core){C::Snapshot snap;assert(!core.captureSnapshot(&snap));return {snap.map(),snap.map()+snap.mapSize()};}
static void recordSnapshot(C&core,const char*path){
 const auto bytes=snapshot(core);std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(out.good());
}
static void compareSnapshot(C&core,const char*path){
 std::ifstream in(path,std::ios::binary);assert(in);Bytes expected{std::istreambuf_iterator<char>(in),{}};assert(snapshot(core)==expected);
}
static void pollSdWorker(){
 assert(owner());task=Task::ProviderWorker;(void)mailbox.workerStep();task=Task::Owner;
}
static void synchronize(Sync&sync,R&sdRuntime,bool interruptBeforeAck=false){
 // Explicit test entropy only. Firmware entropy and first-export policy are
 // outside this fixture; subsequent/recovered exports use persisted identity.
 const uint8_t incarnation[16]={'F','U','L','L','-','S','D',uint8_t(ns),1};
 const auto*initial=native->f.core.exportIncarnationRequired()?incarnation:nullptr;
 P custody(sdRuntime,*port,ns,55,A::Profile::CheckedWarm);assert(!custody.acquire()&&!sync.begin(custody,initial));
 // Real custody blocks Files-style open and USB until terminal release.
 uint64_t size=0,token=0,sectors=0;uint32_t sectorSize=0;const auto*v=f->volume;
 assert(!v->file_open_read(v->context,"/ordinary.txt",&size));const auto*e=risc_storage_volume_export(v);
 assert(e->export_begin(v->context,&token,&sectors,&sectorSize)!=0&&!token);
 bool paused=false,cancelled=false;
 for(unsigned i=0;sync.phase()!=Sync::Phase::Terminal;++i){
  assert(i<2000000&&sync.state()!=Sync::State::Retained);
  if(interruptBeforeAck&&!paused&&sync.phase()==Sync::Phase::CheckpointWrite){poller->pause();paused=true;}
  if(interruptBeforeAck&&!cancelled&&sync.phase()==Sync::Phase::Acknowledge){assert(paused);sync.cancel();sync.poll();cancelled=true;poller->resume();}
  sync.poll();if(!mailbox.idle())pollSdWorker();std::this_thread::yield();
 }
 const auto expected=interruptBeforeAck?Sync::State::Cancelled:Sync::State::Synchronized;
 if(sync.state()!=expected)fprintf(stderr,"sync namespace %u state=%d error=%d\n",ns,int(sync.state()),sync.error());
 assert(sync.state()==expected&&sync.exitSafe()&&custody.exitSafe()&&service->exitSafe());
 if(interruptBeforeAck)assert(paused&&cancelled&&native->f.core.exportPrepared()&&native->f.core.pendingCount()&&native->f.core.identity().generation==19);
 else assert(!native->f.core.exportPrepared()&&!native->f.core.exportIncarnationRequired());
 independentFileAndUsb();
}
static void restore(Client&client,R&sdRuntime){
 P custody(sdRuntime,*port,ns,55,A::Profile::CheckedWarm);bootstrap(custody);
 assert(!service->acquireNative(client.slot,&client.hold));auto&a=*custody.attempt();
 assert(!client.reader.prepareRestoreNative(a));int rc=C::Busy;
 while(rc==C::Busy){rc=client.reader.collectPreparation();std::this_thread::yield();}assert(!rc);
 for(unsigned i=0;client.reader.state()==Catalog::State::Running;++i){assert(i<2000000);client.reader.step();if(!mailbox.idle())pollSdWorker();std::this_thread::yield();}
 if(client.reader.state()!=Catalog::State::Verified)fprintf(stderr,"catalog namespace %u state=%d error=%d\n",ns,int(client.reader.state()),client.reader.error());
 Reader::Candidate candidate;assert(client.reader.state()==Catalog::State::Verified&&!client.reader.takeRestoreCandidate(&candidate));
 const auto reads=f->counter(5),writes=f->counter(6);assert(candidate.identity().generation==20&&!client.restore.begin(a,candidate));
 rc=C::Busy;while(rc==C::Busy){RiscExperimental::NativeSdCompletion::Result result{};const int got=client.restore.collect(&result);rc=got?got:result.status;std::this_thread::yield();}
 assert(!rc&&reads==f->counter(5)&&writes==f->counter(6));
 assert(!service->releaseNative(client.hold)&&service->exitSafe()&&!custody.release()&&custody.exitSafe());independentFileAndUsb();
}
int main(int argc,char**argv){
 assert(argc==7);directory=argv[1];const std::string sdDir=argv[2];NF::rootPath=argv[3];const char*card=argv[4];const char*expected=argv[5];const bool restoring=!strcmp(argv[6],"restore"),interrupted=!strcmp(argv[6],"edit-interrupted");scenario="roundtrip";
 NF::fileAllocation=payloadTracker.api();payloadTracker.reset();NF::empty();NF::onWorker=false;NF::lease=false;NF::powerCheckpoint=[](const char*at){writePause.checkpoint(at);};bootFiles();
 if(!restoring)seedNative();
 Port cpu(hardware());port=&cpu;RiscBoot::Port boot{sdPortOwner,health,[](uint32_t ms){nowMs+=ms;},logLine,sdPortBind};
 boot.appExitSafe=[](){return port->appExitSafe();};boot.providerStorageSafe=[](){return port->providerStorageSafe();};
 RiscTest::FileSources sdRuntimeFiles1({sdDir.c_str()});R sdRuntime(boot);assert(sdRuntimeFiles1.bind(sdRuntime));if(!sdRuntime.prepare(sdDir.c_str())){fprintf(stderr,"SD prepare: %s\n",sdRuntime.error());assert(false);}
 Pin anchor(sdRuntime,cpu,55);assert(!anchor.acquire());if(std::filesystem::exists(card))media(sdDir,"native_settings_sd_load",card);
 {P ready(sdRuntime,cpu,ns,55);bootstrap(ready);assert(!ready.release());}
 if(!restoring)seedSd(emptyIdentity());independentFileAndUsb();
 for(unsigned bootPass=0;bootPass<(interrupted?2u:1u);++bootPass){
  NF::Fixture fixture(ns,payloadTracker.api());Background bg;Coordinator coordinator(fixture.core,fixture.store,bg.hooks());Reload reload{fixture,coordinator};
  service=&coordinator;background=&bg;native=&reload;Hash hash,readHash,nativeHash;Sync sync(fixture.core,fixture.store,coordinator,hash.reader(),payloadTracker.api());
  const uint8_t ownerId[16]={'R','i','s','c','R','T','E','/','N','a','t','i','v','e','1',0};
  Sources sources(ns,ownerId,fixture.inspector,{&fixture,[](void*){return NF::onWorker&&NF::lease;},[](void*){return ESP_OK;},[](void*p){return static_cast<NF::Fixture*>(p)->files.retained()?int(C::Retained):int(C::Ok);}});
  Client client(coordinator,fixture,sources,readHash,nativeHash);
  assert(!coordinator.registerNative(reload.client(),&reload.slot)&&!sync.registerBeforeActivate()&&!coordinator.registerNative(client.registration(),&client.slot)&&!coordinator.activate());
  Poller worker(coordinator);poller=&worker;
  if(!restoring){
   loadNative(reload);
   if(!bootPass){payloadTracker.reset();app();payloadTracker.report(ns,"controller-edits");verifyIntents(fixture.core);recordSnapshot(fixture.core,expected);}
   else assert(fixture.core.exportPrepared()&&fixture.core.pendingCount()&&fixture.core.identity().generation==19&&!fixture.core.exportIncarnationRequired());
   payloadTracker.reset();synchronize(sync,sdRuntime,interrupted&&!bootPass);payloadTracker.report(ns,bootPass?"sync-recovery":"sync");
   if(interrupted&&!bootPass){worker.join();assert(coordinator.exitSafe()&&!NF::lease&&bg.acquires==bg.ends&&NF::handles.empty());continue;}
   if(bootPass)assert(sync.recoveryDecision()==Sync::Recovery::Decision::AcknowledgePrefix);
   assert(!fixture.core.pendingCount()&&fixture.core.identity().generation==20);compareSnapshot(fixture.core,expected);
   // Explicit durable artifact of the host NVS model, erased by the runner
   // together with ALL native AppData files before either namespace restore.
   std::ofstream nvs(std::filesystem::path(NF::rootPath)/"simulated-nvs-erasure-witness.txt");
   for(const auto&space:NF::nvs)for(const auto&row:space.second){nvs<<space.first<<'/'<<row.first<<':'<<row.second.size()<<':';for(uint8_t byte:row.second){const char hex[]="0123456789abcdef";nvs<<hex[byte>>4]<<hex[byte&15];}nvs<<'\n';}assert(nvs.good());
  }else{assert(NF::nvs.empty()&&!fixture.core.ready());payloadTracker.reset();restore(client,sdRuntime);payloadTracker.report(ns,"restore");compareSnapshot(fixture.core,expected);}
  worker.join();assert(coordinator.exitSafe()&&!NF::lease&&bg.acquires==bg.ends&&NF::handles.empty());
 }
 if(restoring){
  // Discard every native reader/cache/coordinator, then exercise actual app
  // readers and controller display code through a new Runtime invocation.
  payloadTracker.reset();phase=1;NF::Fixture fixture(ns,payloadTracker.api());Background bg;Coordinator coordinator(fixture.core,fixture.store,bg.hooks());Reload reload{fixture,coordinator};
  service=&coordinator;background=&bg;native=&reload;assert(!coordinator.registerNative(reload.client(),&reload.slot)&&!coordinator.activate());
  Poller worker(coordinator);poller=&worker;loadNative(reload);compareSnapshot(fixture.core,expected);assert(!fixture.core.pendingCount());app();worker.join();payloadTracker.report(ns,"restored-readers");
  assert(coordinator.exitSafe()&&!NF::lease&&bg.acquires==bg.ends&&NF::handles.empty());
 }
 payloadTracker.report(ns,"after-all-scopes");NF::fileAllocation=C::heap();assert(payloadTracker.blocks.empty());independentFileAndUsb();media(sdDir,"native_settings_sd_save",card);assert(!anchor.release()&&!f&&unloads==1&&!sdRuntime.grantUsage().graphLive);
 printf("namespace %u: actual controller/Runtime/checked native -> Runtime/CpuPort/X4/FatFs -> %s PASS\n",ns,restoring?"typed SD restore + fresh actual readers":"checkpoint; Files-style file and USB preserved");
}
