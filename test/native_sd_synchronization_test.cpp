// Actual validated Runtime graph/CpuPort + X4/FatFs + checked native NVS/files.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main lower_port_main
#include "native_sd_port_test.cpp"
#undef main
#pragma GCC diagnostic pop
#define NATIVE_FIXTURE_THREADED 1
#include "native_replica_checked_fixture.h"
#include "experimental/native_replica/NativeSdSynchronization.h"
#include <openssl/evp.h>
namespace F=NativeFixture;
using Sync=RiscExperimental::NativeSdSynchronization;
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
struct Hash {
 EVP_MD_CTX*ctx=EVP_MD_CTX_new();~Hash(){EVP_MD_CTX_free(ctx);}
 Sync::Checkpoint::Digest api(){return {this,[](void*p){return EVP_DigestInit_ex(static_cast<Hash*>(p)->ctx,EVP_sha256(),nullptr)==1;},[](void*p,const void*b,uint32_t n){return EVP_DigestUpdate(static_cast<Hash*>(p)->ctx,b,n)==1;},[](void*p,uint8_t*out){unsigned n=0;return EVP_DigestFinal_ex(static_cast<Hash*>(p)->ctx,out,&n)==1&&n==32;}};}
};
static void le(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
static Bytes baseMap(){Bytes b;for(unsigned i=0;i<24;++i){const auto at=b.size();b.resize(at+276);snprintf(reinterpret_cast<char*>(b.data()+at),16,"w%08x.a0",i);le(b.data()+at+16,256);for(unsigned j=0;j<256;++j)b[at+20+j]=uint8_t(i+j);}return b;}
static C::Identity baseIdentity(const Bytes&map){Bytes b(12);le(b.data(),3);le(b.data()+4,24);le(b.data()+8,map.size());b.insert(b.end(),map.begin(),map.end());C::Identity id{};id.known=true;id.generation=19;id.lineage[0]=42;unsigned n=0;assert(EVP_Digest(b.data(),b.size(),id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;}
static void file(const char*name,const void*bytes,size_t n){const auto*v=f->volume;const auto*ext=risc_storage_volume_extension(v);const uint32_t h=v->file_open_write(v->context,name);assert(h);for(size_t at=0;at<n;){const auto chunk=std::min<size_t>(4096,n-at);assert(v->file_write(v->context,h,static_cast<const uint8_t*>(bytes)+at,chunk)==chunk);at+=chunk;}assert(ext->file_sync(v->context,h)&&v->file_close(v->context,h,true));}
static void seedSync(const Bytes&map,const C::Identity&id){
 const auto*v=f->volume;const auto*ext=risc_storage_volume_extension(v);assert(ext->mkdir(v->context,"/RTECFG")&&ext->mkdir(v->context,"/RTECFG/00000003"));
 uint8_t index[Sync::Checkpoint::IndexBytes];Sync::Checkpoint::makeIndex(index,3,map.size(),24,id);file("/RTECFG/00000003/replica.index",index,sizeof index);
 char digest[65];for(unsigned i=0;i<32;++i)snprintf(digest+2*i,3,"%02x",id.digest[i]);
 for(unsigned at=0;at<map.size();at+=4096){char path[160];snprintf(path,sizeof path,"/RTECFG/00000003/nr-%08x%08x-%s-%08x",unsigned(id.generation>>32),unsigned(id.generation),digest,at/4096);file(path,map.data()+at,std::min<size_t>(4096,map.size()-at));}
}
static void ordinaryFile(){
 const auto*v=f->volume;uint64_t size=0;const uint32_t h=v->file_open_read(v->context,"/ordinary.txt",&size);char bytes[32]{};
 assert(h&&size==20&&v->file_read(v->context,h,bytes,20)==20&&!memcmp(bytes,"ordinary-independent",20)&&v->file_close(v->context,h,true));
}
using CardFiles=std::map<std::string,Bytes>;
static CardFiles cardFiles(){
 const auto*v=f->volume;const auto*ext=risc_storage_volume_extension(v);CardFiles out;const auto d=v->dir_open(v->context,"/RTECFG/00000003");assert(d);risc_storage_dirent_v1 e{};
 while(v->dir_next(v->context,d,&e)){if(!strcmp(e.name,".")||!strcmp(e.name,".."))continue;assert(!e.is_directory);const std::string path=std::string("/RTECFG/00000003/")+e.name;uint64_t n=0;const auto h=v->file_open_read(v->context,path.c_str(),&n);assert(h&&n==e.size);Bytes b(n);for(size_t at=0;at<n;){const auto chunk=std::min<size_t>(4096,n-at);assert(v->file_read(v->context,h,b.data()+at,chunk)==chunk);at+=chunk;}assert(v->file_close(v->context,h,true));out[path]=b;}
 assert(!ext->handle_error(v->context,d,true)&&ext->dir_close_checked(v->context,d));return out;
}
static std::string candidatePage(const C::Identity&id,unsigned page=0){char digest[65],path[160];for(unsigned i=0;i<32;++i)snprintf(digest+2*i,3,"%02x",id.digest[i]);snprintf(path,sizeof path,"/RTECFG/00000003/nr-%08x%08x-%s-%08x",unsigned(id.generation>>32),unsigned(id.generation),digest,page);return path;}
static C::Identity candidateIdentity(C::Snapshot&snapshot){Bytes b(12);le(b.data(),3);le(b.data()+4,snapshot.count());le(b.data()+8,snapshot.mapSize());b.insert(b.end(),snapshot.map(),snapshot.map()+snapshot.mapSize());auto id=snapshot.base();++id.generation;unsigned n=0;assert(EVP_Digest(b.data(),b.size(),id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;}
static bool concurrentNative=false;
static void nativeStep(Coordinator&c){if(concurrentNative){std::this_thread::yield();return;}task=Task::NativeSettings;F::onWorker=true;c.workerStep();F::onWorker=false;task=Task::Owner;}
static void edit(Coordinator&c,const char*value){
 risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof state;assert(!c.endpoint().state(&state));
 risc_key_value_atomic_mutation_v1 mutation{"wifi.commit",RISC_ATOMIC_PUT,value,uint32_t(strlen(value))};uint64_t token=0;
 assert(!c.endpoint().submit(state.revision,&mutation,1,"explicit-test-edit",18,&token));nativeStep(c);
 risc_key_value_atomic_receipt_v1 receipt{};receipt.struct_size=sizeof receipt;int rc;do{rc=c.endpoint().collect(token,&receipt);if(rc==RISC_ATOMIC_PENDING)std::this_thread::yield();}while(rc==RISC_ATOMIC_PENDING);assert(!rc&&!receipt.status&&receipt.operation_id);
}
static void poll(Sync&s){if(concurrentNative){s.poll();return;}const unsigned nvs=F::nvsTotal,fs=F::fsTotal,reads=f->counter(5),writes=f->counter(6);s.poll();assert(F::nvsTotal==nvs&&F::fsTotal==fs&&f->counter(5)==reads&&f->counter(6)==writes);}
static void drive(Sync&s,Coordinator&c,Sync::Phase stop=Sync::Phase::Terminal){for(unsigned n=0;n<200000;++n){if(s.phase()==stop||s.state()==Sync::State::Retained)return;poll(s);if(s.phase()==stop)return;if(!mailbox.idle()){if(concurrentNative){task=Task::ProviderWorker;mailbox.workerStep();task=Task::Owner;}else step();}nativeStep(c);}fprintf(stderr,"stuck phase %d state %d error %d\n",int(s.phase()),int(s.state()),s.error());assert(false);}
static void usb(){const auto*v=f->volume;const auto*e=risc_storage_volume_export(v);uint64_t token=0,sectors=0;uint32_t size=0;assert(e&&e->export_begin(v->context,&token,&sectors,&size)==0&&token&&size==512);assert(e->export_end(v->context,token)==0);}
static void restartRecovery(RiscBoot::Runtime&runtime,Port&cpu,const char*mode){
 // A new checked store/core loads the selected durable NVS root and AppData
 // objects. The former owner has drained and is never scheduled again.
 F::Fixture reboot(3);task=Task::NativeSettings;F::onWorker=true;F::lease=true;
 assert(!reboot.core.load());reboot.store.finishOperation();F::onWorker=false;F::lease=false;task=Task::Owner;
 assert(reboot.core.exportPrepared()&&!reboot.core.exportIncarnationRequired()&&reboot.core.pendingCount()==1);
 C::ExportMarker marker;assert(!reboot.core.preparedExport(&marker));
 if(!strcmp(mode,"recovery-mismatch")){auto other=marker.target();other.lineage[0]^=1;uint8_t index[Sync::Checkpoint::IndexBytes];Sync::Checkpoint::makeIndex(index,3,marker.mapSize(),marker.count(),other);assert(f->volume->remove(f->volume->context,"/RTECFG/00000003/replica.index"));file("/RTECFG/00000003/replica.index",index,sizeof index);}
 if(!strcmp(mode,"recovery-legacy"))file("/RTECFG/00000003/settings.kv","unexplained legacy",18);
 Coordinator service(reboot.core,reboot.store,{nullptr,[](void*){return owner();},[](void*){return task==Task::NativeSettings;},[](void*){return !F::lease.exchange(true);},[](void*){assert(F::lease.exchange(false));}});
 Hash hash;Sync recovered(reboot.core,reboot.store,service,hash.api());assert(!recovered.registerBeforeActivate()&&!service.activate());
 if(!strcmp(mode,"recovery-newer"))edit(service,"newer-after-restart");
 const auto originalRoot=F::nvs;const unsigned pending=reboot.core.pendingCount();
 P custody(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(!custody.acquire()&&!recovered.begin(custody));
 const bool cancelled=!strcmp(mode,"recovery-cancel-ack"),runningCancel=!strcmp(mode,"recovery-running-cancel");
 if(cancelled||runningCancel){
  drive(recovered,service,Sync::Phase::RecoveryAcknowledge);
  if(cancelled){recovered.cancel();poll(recovered);nativeStep(service);}
  else {
   F::powerCheckpoint=[](const char*at){if(strcmp(at,"write-before"))return;std::unique_lock<std::mutex>lock(gate);readEntered=true;cv.notify_all();cv.wait(lock,[]{return allowRead;});};
   std::thread native([&]{task=Task::NativeSettings;F::onWorker=true;assert(service.workerStep());});
   {std::unique_lock<std::mutex>lock(gate);assert(cv.wait_for(lock,std::chrono::seconds(5),[]{return readEntered;}));recovered.cancel();assert(recovered.poll()==C::Busy&&!recovered.exitSafe());allowRead=true;cv.notify_all();}native.join();F::powerCheckpoint=nullptr;
  }
 }
 drive(recovered,service);
 const bool mismatch=!strcmp(mode,"recovery-mismatch")||!strcmp(mode,"recovery-legacy");
 assert(recovered.state()==(mismatch?Sync::State::Conflict:cancelled?Sync::State::Cancelled:Sync::State::Synchronized)&&recovered.exitSafe()&&custody.exitSafe()&&service.exitSafe());
 if(mismatch||cancelled)assert(reboot.core.exportPrepared()&&reboot.core.pendingCount()==pending&&F::nvs==originalRoot);
 else {assert(!reboot.core.exportPrepared()&&!reboot.core.pendingCount()&&reboot.core.identity().generation==(!strcmp(mode,"recovery-newer")?21:20));assert(F::value(reboot.core,"wifi.commit")==(!strcmp(mode,"recovery-newer")?"newer-after-restart":"first"));}
 if(runningCancel)assert(recovered.cancellationRequested()&&recovered.recoveryDecision()==Sync::Recovery::Decision::AcknowledgePrefix);
 ordinaryFile();usb();
}
static void abandonedThenNewer(RiscBoot::Runtime&runtime,Port&cpu,C&original){
 C::ExportMarker a;assert(!original.preparedExport(&a));const auto first=cardFiles();char stage[RiscExperimental::SdReplicaStage::PathBytes];assert(RiscExperimental::SdReplicaStage::makePath(stage,sizeof stage,3,a.target()));assert(first.count(stage)&&first.count(candidatePage(a.target()))&&first.at(stage).size()==Sync::Checkpoint::IndexBytes);
 C::Identity bTarget{};CardFiles second;
 {
  F::Fixture reboot(3);task=Task::NativeSettings;F::onWorker=true;F::lease=true;assert(!reboot.core.load());reboot.store.finishOperation();F::onWorker=false;F::lease=false;task=Task::Owner;
  Coordinator service(reboot.core,reboot.store,{nullptr,[](void*){return owner();},[](void*){return task==Task::NativeSettings;},[](void*){return !F::lease.exchange(true);},[](void*){assert(F::lease.exchange(false));}});Hash hash;Sync sync(reboot.core,reboot.store,service,hash.api());assert(!sync.registerBeforeActivate()&&!service.activate());edit(service,"second-after-abandoned-A");assert(reboot.core.pendingCount()==2);
  P custody(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(!custody.acquire()&&!sync.begin(custody));drive(sync,service,Sync::Phase::Acknowledge);C::ExportMarker b;assert(!reboot.core.preparedExport(&b));bTarget=b.target();assert(memcmp(a.target().digest,bTarget.digest,32)&&a.target().generation==bTarget.generation);sync.cancel();drive(sync,service);assert(sync.state()==Sync::State::Cancelled&&sync.exitSafe()&&service.exitSafe()&&reboot.core.pendingCount()==2&&reboot.core.exportPrepared());
  second=cardFiles();for(const auto&entry:first)if(entry.first!="/RTECFG/00000003/replica.index")assert(second.at(entry.first)==entry.second);assert(second.count(candidatePage(bTarget))&&second.count(candidatePage(a.target()))&&candidatePage(bTarget)!=candidatePage(a.target()));ordinaryFile();usb();
 }
 {
  F::Fixture reboot(3);task=Task::NativeSettings;F::onWorker=true;F::lease=true;assert(!reboot.core.load());reboot.store.finishOperation();F::onWorker=false;F::lease=false;task=Task::Owner;
  Coordinator service(reboot.core,reboot.store,{nullptr,[](void*){return owner();},[](void*){return task==Task::NativeSettings;},[](void*){return !F::lease.exchange(true);},[](void*){assert(F::lease.exchange(false));}});Hash hash;Sync sync(reboot.core,reboot.store,service,hash.api());assert(!sync.registerBeforeActivate()&&!service.activate());P custody(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(!custody.acquire()&&!sync.begin(custody));drive(sync,service);assert(sync.state()==Sync::State::Synchronized&&sync.exitSafe()&&service.exitSafe()&&!reboot.core.pendingCount()&&!reboot.core.exportPrepared()&&reboot.core.identity().generation==20&&F::value(reboot.core,"wifi.commit")=="second-after-abandoned-A");assert(cardFiles()==second);ordinaryFile();usb();
 }
}
int main(int argc,char**argv){
 assert(argc==4);const char*mode=argv[2];Port cpu(hardware());port=&cpu;
 RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t ms){nowMs+=ms;},[](const char*){return true;},bind};
 boot.appExitSafe=[](){return port->appExitSafe();};boot.providerStorageSafe=[](){return port->providerStorageSafe();};
 RiscBoot::Runtime runtime(boot);assert(runtime.prepare(argv[1]));Pin anchor(runtime,cpu,55);assert(!anchor.acquire());
 if(strcmp(mode,"unavailable")){P fresh(runtime,cpu,3,55);bootstrap(fresh);assert(!fresh.release());}
 const auto original=baseMap();const auto id=baseIdentity(original);auto remote=original;auto remoteId=id;
 if(!strcmp(mode,"foreign"))remoteId.lineage[0]=43;
 if(!strcmp(mode,"changed"))++remoteId.generation;
 if(!strcmp(mode,"same-generation")){remote[20]^=1;remoteId=baseIdentity(remote);}
 if(strcmp(mode,"unavailable")){seedSync(remote,remoteId);file("/ordinary.txt","ordinary-independent",20);ordinaryFile();}
 if(!strcmp(mode,"unknown"))file("/RTECFG/00000003/unexplained.bin","x",1);
 if(!strcmp(mode,"legacy"))file("/RTECFG/00000003/settings.kv","unknown legacy content",22);
 F::rootPath=argv[3];F::empty();F::Fixture files(3);
 std::vector<C::Value> values;for(unsigned at=0;at<original.size();at+=276)values.push_back({reinterpret_cast<const char*>(original.data()+at),original.data()+at+20,256});
 task=Task::NativeSettings;F::onWorker=true;F::lease=true;assert(!files.core.initialize(values.data(),values.size(),id));files.store.finishOperation();F::onWorker=false;F::lease=false;task=Task::Owner;
 unsigned ended=0;Coordinator service(files.core,files.store,{&ended,[](void*){return owner();},[](void*){return task==Task::NativeSettings;},[](void*){return !F::lease.exchange(true);},[](void*p){assert(F::lease.exchange(false));++*static_cast<unsigned*>(p);}});
 Hash hash;C other(3,files.store.backend(),S::limits(),C::heap());Sync wrong(other,files.store,service,hash.api());assert(wrong.registerBeforeActivate()==C::Invalid);
 Sync sync(files.core,files.store,service,hash.api());assert(!sync.registerBeforeActivate()&&!service.activate());edit(service,"first");
 if(!strcmp(mode,"usb-before")){
  const auto*v=f->volume;const auto*e=risc_storage_volume_export(v);uint64_t token=0,sectors=0;uint32_t size=0;assert(!e->export_begin(v->context,&token,&sectors,&size));
  {P denied(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(denied.acquire()==RISC_STORAGE_ASYNC_BUSY&&denied.exitSafe()&&sync.exitSafe()&&service.exitSafe());}
  assert(!e->export_end(v->context,token));
 }
 const bool pageCase=!strncmp(mode,"page-",5);CardFiles seededPage;
 if(pageCase){C::Snapshot snapshot;assert(!files.core.captureSnapshot(&snapshot));auto target=candidateIdentity(snapshot);Bytes bytes(snapshot.map(),snapshot.map()+4096);if(!strcmp(mode,"page-empty"))bytes.clear();else if(!strcmp(mode,"page-prefix"))bytes.resize(57);else if(!strcmp(mode,"page-mismatch")){bytes.resize(57);bytes.back()^=1;}else if(!strcmp(mode,"page-oversize"))bytes.resize(4097);file(candidatePage(target).c_str(),bytes.data(),bytes.size());seededPage=cardFiles();}
 std::atomic<bool>stopNative{false};std::thread nativeTask;
 if(!strcmp(mode,"concurrent")){concurrentNative=true;nativeTask=std::thread([&]{task=Task::NativeSettings;F::onWorker=true;while(!stopNative.load(std::memory_order_acquire)){service.workerStep();std::this_thread::yield();}});}
 P custody(runtime,cpu,3,55,!strcmp(mode,"unavailable")?A::Profile::Fresh:A::Profile::CheckedWarm);const uint8_t incarnation[16]={0x71};assert(files.core.exportIncarnationRequired());assert(!custody.acquire()&&!sync.begin(custody,!strcmp(mode,"no-incarnation")?nullptr:incarnation));
 if(!strcmp(mode,"unavailable"))f->fault(8);
 if(!strcmp(mode,"cancel-admission"))sync.cancel();
 bool expectedConflict=!strcmp(mode,"foreign")||!strcmp(mode,"changed")||!strcmp(mode,"same-generation")||!strcmp(mode,"legacy")||!strcmp(mode,"unknown")||!strcmp(mode,"page-oversize");
 if(!expectedConflict&&strcmp(mode,"unavailable")&&strcmp(mode,"cancel-admission")){
  drive(sync,service,Sync::Phase::CheckpointPrepare);assert(sync.state()==Sync::State::Running);
  // No owner polling, idle native scheduler probe, ordinary Files or USB path
  // is allowed to perform native work or steal the live SD attempt.
  if(concurrentNative){for(unsigned i=0;i<1000;++i)poll(sync);assert(!sync.exitSafe());}
  else {const unsigned beforeNvs=F::nvsTotal,beforeFs=F::fsTotal;for(unsigned i=0;i<1000;++i)poll(sync);assert(beforeNvs==F::nvsTotal&&beforeFs==F::fsTotal&&!sync.exitSafe());}
  risc_key_value_atomic_mutation_v1 later{"wifi.commit",RISC_ATOMIC_PUT,"second",6};uint64_t token=99;
  assert(service.endpoint().submit(files.core.revision(),&later,1,"later",5,&token)==RISC_ATOMIC_BUSY&&!token);
  assert(cpu.nativeStorageAttached()&&!cpu.appExitSafe()&&!cpu.restartResourcesSafe());f->rejected();
  const auto*v=f->volume;uint64_t size=99;const unsigned beforeFiles=f->counter(5);assert(!v->file_open_read(v->context,"/ordinary.txt",&size)&&f->counter(5)==beforeFiles);
  const auto*exportApi=risc_storage_volume_export(v);uint64_t exportToken=0,sectors=0;uint32_t sectorSize=0;assert(exportApi->export_begin(v->context,&exportToken,&sectors,&sectorSize)!=0&&!exportToken);
  if(!strcmp(mode,"cancel-preparation"))sync.cancel();
  nativeStep(service);
  if(!strcmp(mode,"abandoned-A-B")){
   const unsigned replaced=f->counter(7);for(unsigned i=0;i<20000&&f->counter(7)==replaced;++i){poll(sync);if(f->counter(7)!=replaced)break;if(!mailbox.idle())step();}assert(f->counter(7)==replaced+1);sync.cancel();
  }
  if(!strcmp(mode,"remove"))f->fault(1);
  if(!strcmp(mode,"sd-close"))f->fault(5);
  if(!strcmp(mode,"cancel-write")||!strcmp(mode,"recovery-unpublished")){drive(sync,service,Sync::Phase::CheckpointWrite);sync.cancel();}
  if(!strcmp(mode,"cancel-ack")||!strcmp(mode,"running-cancel")||!strcmp(mode,"native-close")||!strcmp(mode,"late-retained")||(!strncmp(mode,"recovery-",9)&&strcmp(mode,"recovery-unpublished"))){
   // Stop before owner issues its completion, then issue without worker run.
   for(unsigned i=0;i<20000&&sync.phase()!=Sync::Phase::Acknowledge;++i){poll(sync);if(!mailbox.idle()){if(concurrentNative){task=Task::ProviderWorker;mailbox.workerStep();task=Task::Owner;}else step();}}
   assert(sync.phase()==Sync::Phase::Acknowledge);
   if(!strcmp(mode,"cancel-ack")||!strncmp(mode,"recovery-",9)){sync.cancel();poll(sync);}
   if(!strcmp(mode,"native-close")){F::armed=true;F::fault="close";F::cut=1;F::fsCalls=0;}
   if(!strcmp(mode,"running-cancel")){
    F::powerCheckpoint=[](const char*at){if(strcmp(at,"write-before"))return;std::unique_lock<std::mutex>lock(gate);readEntered=true;cv.notify_all();cv.wait(lock,[]{return allowRead;});};
    std::thread native([&]{task=Task::NativeSettings;F::onWorker=true;assert(service.workerStep());});
    {std::unique_lock<std::mutex>lock(gate);assert(cv.wait_for(lock,std::chrono::seconds(5),[]{return readEntered;}));sync.cancel();assert(sync.poll()==C::Busy&&!sync.exitSafe());allowRead=true;cv.notify_all();}native.join();F::powerCheckpoint=nullptr;
   }else nativeStep(service);
   if(!strcmp(mode,"late-retained"))f->fault(3);
  }
 }
 drive(sync,service);F::armed=false;
 if(!strcmp(mode,"abandoned-A-B")){assert(sync.state()==Sync::State::Cancelled&&sync.exitSafe()&&files.core.exportPrepared());abandonedThenNewer(runtime,cpu,files.core);assert(!anchor.release()&&!f);puts("actual X4 durable A interruption, native reload, app edit B, publication/reload acknowledgement preserves both page sets and old stage PASS");return 0;}
 if(!strcmp(mode,"page-mismatch"))expectedConflict=true;
 if(expectedConflict&&pageCase)assert(cardFiles()==seededPage);
 if(!strncmp(mode,"recovery-",9)){assert(sync.state()==Sync::State::Cancelled&&sync.exitSafe()&&files.core.exportPrepared());restartRecovery(runtime,cpu,mode);assert(!anchor.release()&&!f);printf("normal durable SD owner %s exact selected-marker restart PASS\n",mode);return 0;}
 if(sync.state()==Sync::State::Retained){
  assert(!strcmp(mode,"remove")||!strcmp(mode,"sd-close")||!strcmp(mode,"native-close")||!strcmp(mode,"late-retained"));
  assert(!sync.exitSafe()&&!custody.exitSafe()&&service.retained()&&!service.exitSafe()&&cpu.nativeStorageAttached()&&!cpu.appExitSafe());
  const auto nvs=F::nvsTotal,fs=F::fsTotal,reads=f->counter(5),writes=f->counter(6);for(unsigned i=0;i<100;++i){poll(sync);nativeStep(service);}assert(nvs==F::nvsTotal&&fs==F::fsTotal&&reads==f->counter(5)&&writes==f->counter(6));
  printf("normal SD owner actual Runtime/Port/X4/FatFs/coordinator %s Retained PASS\n",mode);fflush(stdout);std::_Exit(0);
 }
 assert(sync.exitSafe()&&custody.exitSafe()&&service.exitSafe()&&!cpu.nativeStorageAttached()&&cpu.appExitSafe());
 if(strcmp(mode,"unavailable"))ordinaryFile();
 const bool cancelled=!strncmp(mode,"cancel-",7);
 assert(sync.state()==(expectedConflict?Sync::State::Conflict:!strcmp(mode,"unavailable")?Sync::State::Deferred:cancelled?Sync::State::Cancelled:!strcmp(mode,"no-incarnation")?Sync::State::Failed:Sync::State::Synchronized));
 if(expectedConflict||cancelled||!strcmp(mode,"unavailable")||!strcmp(mode,"no-incarnation"))assert(files.core.pendingCount()==1&&files.core.identity().generation==19);
 else {
  assert(!files.core.pendingCount()&&files.core.identity().generation==20);assert(F::value(files.core,"wifi.commit")=="first");
  usb();edit(service,"second");assert(files.core.pendingCount()==1); // BUSY edit retried after acknowledged release.
  P second(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(!second.acquire()&&!sync.begin(second));drive(sync,service);
  assert(sync.state()==Sync::State::Synchronized&&sync.exitSafe()&&second.exitSafe()&&!files.core.pendingCount()&&files.core.identity().generation==21&&F::value(files.core,"wifi.commit")=="second");ordinaryFile();usb();
 }
 if(nativeTask.joinable()){stopNative.store(true,std::memory_order_release);nativeTask.join();concurrentNative=false;}
 assert(anchor.release()==0&&!f&&unloads==1&&runtime.grantUsage().graphLive==0);
 printf("normal SD owner actual Runtime/Port/X4/FatFs/coordinator %s PASS\n",mode);
}
