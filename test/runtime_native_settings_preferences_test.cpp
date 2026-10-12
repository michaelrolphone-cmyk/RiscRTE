// One source-bound host path: Runtime -> permanent coordinator polling task ->
// NativeAtomicKeyValue -> CheckedNativeReplicaStore -> AppDataFiles/NVS metadata.
#define NATIVE_FIXTURE_THREADED 1
#include "native_replica_checked_fixture.h"
#include "experimental/native_replica/NativeSettingsCoordinator.h"
#include "bootstrap/Runtime.h"
#include "runtime_native_settings_preferences_fixture.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
using R=RiscBoot::Runtime;
namespace NF=NativeFixture;
static const auto ownerThread=std::this_thread::get_id();
static bool owner(){return !NF::onWorker&&std::this_thread::get_id()==ownerThread;}
static std::string directory,scenario;
static unsigned phase=0,entries=0,finishes=0,providerStops=0;
static Coordinator*service;
static R*runtime;
static risc_bound_key_value_v1 boundReaders[2]{};
static risc_key_value_atomic_v1 savedApi{};
static bool health(risc_runtime_health_v1*){return true;}
static bool logLine(const char*){return true;}
static void delay(uint32_t){std::this_thread::yield();}
static const RiscBoot::KeyValueBackend forbiddenLegacy{nullptr,
 [](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{assert(false&&"namespace 1 must use the native read-through");return -1;},
 [](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{assert(false&&"legacy writes are forbidden");return -1;},2048};

struct Background {
 unsigned acquires=0,ends=0;
 Coordinator::Hooks hooks(){return {this,
  [](void*){return owner();},[](void*){return NF::onWorker;},
  [](void*p){assert(owner());if(NF::lease.exchange(true))return false;++static_cast<Background*>(p)->acquires;return true;},
  [](void*p){assert(owner()&&NF::lease.exchange(false));++static_cast<Background*>(p)->ends;},
  [](void*,const void*p,size_t n){return p&&n;}};}
};
static Background*background;
// Immutable trusted registration, using the actual compact native mailbox.
// This reload is not an SD/source-admission adapter and grants no new authority.
struct Reload {
 NF::Fixture&f;Coordinator&service;uint32_t slot=0;uint64_t token=0;
 RiscExperimental::NativeReplicaControlJob job{run,this};
 Coordinator::NativeClient client(){return {this,sizeof(*this),
  [](void*p){auto&s=*static_cast<Reload*>(p);return s.service.nativeWorker(s.slot)&&s.job.workerStep();},
  [](void*p){return static_cast<Reload*>(p)->job.pending();},
  [](void*p){auto&s=*static_cast<Reload*>(p);return !s.token&&s.job.exitSafe();},
  [](void*){return false;}};}
 static J::Result run(void*p,uint32_t action,const void*,uint32_t){
  auto&s=*static_cast<Reload*>(p);assert(action==1&&s.service.nativeWorker(s.slot));
  int rc=s.f.core.load();s.f.store.finishOperation();if(!s.f.store.exitSafe())rc=C::Retained;
  return {rc,s.f.core.pendingCount(),s.f.core.revision()};
 }
};
static Reload*native;
struct Poller {
 Coordinator&service;std::mutex mutex;std::condition_variable cv;
 bool stopped=false,paused=false;std::atomic<unsigned>polls{0},completed{0};std::thread thread;
 explicit Poller(Coordinator&s):service(s),thread([this]{
  NF::onWorker=true;
  for(;;){
   std::unique_lock<std::mutex>lock(mutex);cv.wait(lock,[this]{return stopped||!paused;});
   if(stopped)break;
   if(service.workerStep())++completed;
   ++polls;lock.unlock();std::this_thread::yield();
  }
  NF::onWorker=false;
 }){}
 void pause(){std::lock_guard<std::mutex>lock(mutex);paused=true;}
 void resume(){{std::lock_guard<std::mutex>lock(mutex);paused=false;}cv.notify_all();}
 void join(){if(!thread.joinable())return;{std::lock_guard<std::mutex>lock(mutex);stopped=true;}cv.notify_all();thread.join();}
 void several(){const unsigned until=polls.load()+1000;while(polls.load()<until)std::this_thread::yield();}
 ~Poller(){join();}
};
static Poller*poller;
struct WritePause {
 std::mutex mutex;std::condition_variable cv;bool armed=false,arrived=false,resume=false;
 void checkpoint(const char*at){
  if(strcmp(at,"write-before"))return;
  std::unique_lock<std::mutex>lock(mutex);if(!armed||arrived)return;
  arrived=true;cv.notify_all();cv.wait(lock,[this]{return resume;});
 }
};
static WritePause writePause;
extern "C" void native_settings_pause_worker(){assert(owner());poller->pause();}
extern "C" void native_settings_resume_worker(){assert(owner());poller->resume();}
extern "C" void native_settings_yield(){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
static unsigned snapshotNvs=0,snapshotFs=0;
extern "C" void native_settings_snapshot_io(){snapshotNvs=NF::nvsTotal;snapshotFs=NF::fsTotal;}
extern "C" void native_settings_assert_no_io(){assert(snapshotNvs==NF::nvsTotal&&snapshotFs==NF::fsTotal);}
extern "C" void native_settings_arm_write_pause(){
 std::lock_guard<std::mutex>lock(writePause.mutex);assert(!writePause.armed);writePause.armed=true;
}
extern "C" void native_settings_wait_write_pause(){
 std::unique_lock<std::mutex>lock(writePause.mutex);
 assert(writePause.cv.wait_for(lock,std::chrono::seconds(10),[]{return writePause.arrived;}));
}
extern "C" void native_settings_release_write_pause(){
 {std::lock_guard<std::mutex>lock(writePause.mutex);assert(writePause.arrived);writePause.resume=true;}writePause.cv.notify_all();
}
extern "C" void native_settings_arm_close_failure(){NF::armed=true;NF::fault="close";NF::cut=1;}
static void invalid(const risc_key_value_atomic_v1&a){
 if(!a.context)return;
 risc_key_value_atomic_state_v1 s{sizeof(s)};risc_key_value_atomic_receipt_v1 r{sizeof(r)};
 char out='x';uint32_t size=99;uint64_t token=99;
 assert(a.state(a.context,&s)==RISC_ATOMIC_INVALID);
 assert(a.get(a.context,1,"time_format",&out,1,&size)==RISC_ATOMIC_INVALID&&!size&&out=='x');
 assert(a.submit(a.context,1,nullptr,0,"intent",6,&token)==RISC_ATOMIC_INVALID&&!token);
 assert(a.collect(a.context,1,&r)==RISC_ATOMIC_INVALID&&a.cancel(a.context,1)==RISC_ATOMIC_INVALID);
}
static risc_key_value_v1 view(const risc_bound_key_value_v1&b){return {b.api_version,b.struct_size,b.context,b.get,b.put};}
extern "C" void test_atomic_bound_started(unsigned version,risc_bound_key_value_v1 table){assert(version>=1&&version<=2);boundReaders[version-1]=table;}
extern "C" void test_atomic_bound_stopped(risc_bound_key_value_v1 table){
 ++providerStops;char out='x';uint32_t n=99;
 assert(table.get(table.context,"time_format",&out,1,&n)==RISC_BOUND_KEY_VALUE_CONTEXT&&!n&&out=='x');
 assert(table.put(table.context,"time_format","x",1)==RISC_BOUND_KEY_VALUE_CONTEXT);
}
static risc_runtime_capability_v1 acquireLegacy(const risc_runtime_api_v1*rt,unsigned version){
 risc_runtime_capability_v1 grant{sizeof(grant)};assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,version,1,&grant));return grant;
}
extern "C" void native_settings_verify_legacy(const risc_runtime_api_v1*rt,unsigned count){
 const unsigned beforeNvs=NF::nvsTotal,beforeFs=NF::fsTotal;
 for(unsigned v=1;v<=2;++v){
  auto grant=acquireLegacy(rt,v);auto kv=*static_cast<const risc_key_value_v1*>(grant.api);auto bound=view(boundReaders[v-1]);
  if(v==1){runtime_native_settings_preferences(&kv,count);runtime_native_settings_preferences(&bound,count);}
  // The production preference codecs explicitly accept KV1 only. KV2 is checked
  // byte-for-byte without relabeling its API or bypassing that version guard.
  for(const char*key:{"time_format","time_zone","desk_clock_face","desk_direction","reader_flip_ui","reader_language","sleep_idle","rtc_basis"}){
   char expected[64],actual[64];uint32_t expectedSize=0,size=0;
   const int rc=native->f.core.get(key,expected,sizeof(expected),&expectedSize);
   assert(rc==C::Ok||rc==C::Missing);
   for(auto*reader:{&kv,&bound}){
    const int result=reader->get(reader->context,key,actual,sizeof(actual),&size);
    assert(result==(rc==C::Ok?RISC_KEY_VALUE_OK:RISC_KEY_VALUE_NOT_FOUND));
    assert(size==expectedSize&&!memcmp(actual,expected,size));
   }
  }
  assert(kv.put(kv.context,"time_format","x",1)==RISC_KEY_VALUE_CONTEXT);
  assert(bound.put(bound.context,"time_format","x",1)==RISC_BOUND_KEY_VALUE_CONTEXT);
  char out='x';uint32_t n=99;
  assert(bound.get(bound.context,"unmapped",&out,1,&n)==RISC_BOUND_KEY_VALUE_CONTEXT&&!n&&out=='x');
  std::thread foreign([&]{n=99;assert(kv.get(kv.context,"time_format",&out,1,&n)==RISC_KEY_VALUE_CONTEXT&&!n);});foreign.join();
  assert(rt->release(&grant));n=99;
  assert(kv.get(kv.context,"time_format",&out,1,&n)==RISC_KEY_VALUE_CONTEXT&&!n);
 }
 assert(NF::nvsTotal==beforeNvs&&NF::fsTotal==beforeFs);
 puts("integrated legacy readers: actual app/provider KV1 decode and KV2 byte-exact native preferences; no old-backend reads/writes PASS");
}
extern "C" void native_settings_pending_checks(const risc_key_value_atomic_v1*a,uint64_t token){
 assert(a&&token&&NF::lease&&!service->exitSafe()&&!runtime->residentResetSafe());savedApi=*a;
 const unsigned beforeNvs=NF::nvsTotal,beforeFs=NF::fsTotal,ends=background->ends;
 Coordinator::Lease request;assert(service->acquireNative(native->slot,&request)==C::Busy&&!request.slot);
 risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};
 for(unsigned i=0;i<1000;++i)assert(a->collect(a->context,token,&receipt)==RISC_ATOMIC_PENDING);
 assert(!service->workerStep());
 risc_key_value_atomic_state_v1 state{sizeof(state)};assert(a->state(a->context,&state)==RISC_ATOMIC_BUSY);
 const auto*rt=risc_runtime_get_api(1);
 for(unsigned v=1;v<=2;++v){
  auto grant=acquireLegacy(rt,v);auto kv=*static_cast<const risc_key_value_v1*>(grant.api);
  char out='x';uint32_t n=99;
  assert(kv.get(kv.context,"time_format",&out,1,&n)==RISC_KEY_VALUE_BUSY&&!n&&out=='x');
  n=99;auto&b=boundReaders[v-1];assert(b.get(b.context,"time_format",&out,1,&n)==RISC_BOUND_KEY_VALUE_BUSY&&!n&&out=='x');
  assert(rt->release(&grant));
 }
 assert(NF::nvsTotal==beforeNvs&&NF::fsTotal==beforeFs&&background->ends==ends&&NF::lease);
}
extern "C" void native_settings_idle_checks(const risc_key_value_atomic_v1*a){
 savedApi=*a;
 assert(service->exitSafe()&&service->endpoint().scratchCleared()&&runtime->residentResetSafe()&&!NF::lease);
 const unsigned beforeNvs=NF::nvsTotal,beforeFs=NF::fsTotal;
 // Idle worker probes continually race short owner lock/read/release cycles.
 for(unsigned i=0;i<20000;++i){
  risc_key_value_atomic_state_v1 state{sizeof(state)};assert(!a->state(a->context,&state)&&state.namespace_id==1);
  char bytes[64];uint32_t n=0;assert(!a->get(a->context,state.revision,"time_format",bytes,sizeof(bytes),&n)&&n);
 }
 Coordinator::Lease hold;assert(!service->acquireNative(native->slot,&hold));
 risc_key_value_atomic_state_v1 state{sizeof(state)};assert(a->state(a->context,&state)==RISC_ATOMIC_BUSY);
 assert(!service->releaseNative(hold));assert(service->releaseNative(hold)==C::Invalid);
 assert(service->exitSafe()&&!service->retained()&&background->acquires==background->ends);
 assert(NF::nvsTotal==beforeNvs&&NF::fsTotal==beforeFs);
 puts("integrated owner polling: 1000 pending polls and 40000 idle state/get calls do no filesystem/NVS I/O; native/background exclusion PASS");
}
extern "C" void test_atomic_app(unsigned hook,unsigned child){
 assert(!child);const auto*rt=risc_runtime_get_api(1);assert(rt);
 if(hook!=1){risc_runtime_capability_v1 g{sizeof(g)};assert(!rt->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&g));if(hook==2){++finishes;invalid(savedApi);}return;}
 ++entries;if(phase)invalid(savedApi);runtime_native_settings_controller(rt,phase,scenario.c_str());
}
static bool bind(R&r){
 assert(r.registerAtomicKeyValue("native-settings-fixture",1,service->endpoint()));
 assert(r.registerAtomicKeyValueReadThrough(1,service->endpoint()));return true;
}
static void file(const char*name,const std::string&data){std::ofstream(directory+"/"+name)<<data;}
static void bootFiles(){
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 JsonDocument app,boot;app["type"]="application";app["id"]="native-settings-fixture";app["version"]="1.0.0";app["architecture"]="xtensa-esp32s3";app["file_name"]="default.elf";app["entry"]="app_main";
 boot["board"]="board.json";boot["default_app"]="default.elf";
 auto scope=boot["app_capabilities"].to<JsonArray>().add<JsonObject>();scope["manifest"]="app.json";
 for(unsigned v=0;v<3;++v){const char*cap=v?RISC_KEY_VALUE_CAPABILITY:RISC_KEY_VALUE_ATOMIC_CAPABILITY;unsigned version=v==2?2:1;
  auto g=scope["grants"].as<JsonArray>();if(!v)g=scope["grants"].to<JsonArray>();
  auto row=g.add<JsonObject>();row["capability"]=cap;row["api"]=version;row["instance_id"]=1;
 }
 auto req=app["requires"].to<JsonArray>();
 for(unsigned v=0;v<3;++v){auto q=req.add<JsonObject>();q["capability"]=v?RISC_KEY_VALUE_CAPABILITY:RISC_KEY_VALUE_ATOMIC_CAPABILITY;q["api"]=v==2?2:1;}
 auto drivers=boot["drivers"].to<JsonArray>();
 for(unsigned v=1;v<=2;++v){
  std::string id=std::to_string(v),name="reader"+id+".json";
  file(name.c_str(),"{\"type\":\"driver\",\"id\":\"atomic-reader-"+id+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"driver_abi\":2,\"file_name\":\"reader"+id+".elf\",\"requires\":[{\"capability\":\"storage.key-value.bound\",\"api\":"+id+"}],\"provides\":[{\"capability\":\"test.atomic.reader"+id+"\",\"api\":1}]}");
  auto d=drivers.add<JsonObject>();d["manifest"]=name;auto keys=d["key_value"].to<JsonArray>();
  for(const char*key:{"time_format","time_zone","desk_clock_face","desk_direction","reader_flip_ui","reader_language","sleep_idle","rtc_basis"}){
   auto k=keys.add<JsonObject>();k["key"]=key;k["namespace"]=1;k["access"]="read-write";
  }
 }
 std::string text;serializeJson(app,text);file("app.json",text);text.clear();serializeJson(boot,text);file("boot.json",text);
}
static void bootstrapEstablishedNativeState(){
 // Explicit test PRECONDITION: an already established, empty Unclassified
 // native domain, with no claimed SD provenance. Compatibility initialize is
 // used only to create test media, never to claim first-boot/source admission.
 NF::Fixture seed(1);
 std::thread task([&]{NF::onWorker=true;NF::lease=true;assert(!seed.core.initialize(nullptr,0,{}));seed.store.finishOperation();assert(seed.store.exitSafe());NF::lease=false;});task.join();
 assert(seed.core.baseline().origin()==C::Origin::Unclassified);
 puts("PRECONDITION: synthetic pre-established empty native domain; first-boot/SD source admission is not qualified here");
}
static void loadNative(Reload&reload){
 Coordinator::Lease hold;assert(!service->acquireNative(reload.slot,&hold)&&service->nativeOwner(hold));
 assert(!reload.job.begin(1,nullptr,0,&reload.token));poller->several();
 assert(service->releaseNative(hold)==C::Busy&&NF::lease);
 J::Result result{};int rc;do{rc=reload.job.collect(reload.token,&result);std::this_thread::yield();}while(rc==RISC_STORAGE_WORK_PENDING);
 assert(rc==RISC_STORAGE_WORK_OK&&result.status==C::Ok);reload.token=0;
 assert(!service->releaseNative(hold)&&service->exitSafe()&&!NF::lease);
}
static void verifyIntents(C&core){
 assert(core.pendingCount()==8&&core.sequence()==8);
 const char*keys[]={"time_format","time_zone","desk_clock_face","desk_direction","reader_flip_ui","reader_language","sleep_idle"};
 for(unsigned i=0;i<7;++i){uint64_t id=0;uint32_t n=0;uint8_t intent[176]{};
  assert(!core.pending(i,&id,intent,sizeof(intent),&n)&&id==i+1&&n==sizeof(intent));
  assert(!memcmp(intent,"PFI0001",8)&&intent[8]==1&&intent[9]==1&&!intent[10]);
  assert(!strcmp(reinterpret_cast<char*>(intent+12),keys[i]));
  uint32_t bytes=uint32_t(intent[32])|(uint32_t(intent[33])<<8);
  char actual[64];uint32_t size=0;assert(!core.get(keys[i],actual,sizeof actual,&size)&&size==bytes);
  if(i!=5)assert(!memcmp(actual,intent+108,size)); // language was independently changed after its UI Save.
 }
 uint64_t id=0;uint32_t n=0;char other[32]{};
 assert(!core.pending(7,&id,other,sizeof(other),&n)&&id==8&&n==12&&!memcmp(other,"other-writer",12));
}
extern "C" void native_settings_intervening_preference(){
 assert(owner());auto api=service->endpoint().api();risc_key_value_atomic_state_v1 state{sizeof(state)};
 assert(!api.state(api.context,&state));const uint8_t bytes[]={0x4c,1,2,0xa7};
 const risc_key_value_atomic_mutation_v1 mutation{"reader_language",1,bytes,sizeof(bytes)};uint64_t token=0;
 assert(!api.submit(api.context,state.revision,&mutation,1,"other-writer",12,&token)&&token);
 risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};int rc;
 do{rc=api.collect(api.context,token,&receipt);std::this_thread::yield();}while(rc==RISC_ATOMIC_PENDING);
 assert(!rc&&!receipt.status&&receipt.operation_id==8);
}
static void runBoot(){
 NF::Fixture fixture(1);Background bg;Coordinator coordinator(fixture.core,fixture.store,bg.hooks());
 Reload reload{fixture,coordinator};service=&coordinator;background=&bg;native=&reload;
 assert(!coordinator.registerNative(reload.client(),&reload.slot)&&!coordinator.activate());
 Poller worker(coordinator);poller=&worker;loadNative(reload);
 if(phase)verifyIntents(fixture.core);
 R current({owner,health,delay,logLine,bind,&forbiddenLegacy});runtime=&current;const bool prepared=current.prepare(directory.c_str());if(!prepared)fprintf(stderr,"Runtime prepare: %s\n",current.error());assert(prepared);
 const bool ok=current.run();
 if(scenario!="roundtrip"){
  assert(!ok&&current.retained()&&!current.residentResetSafe()&&!coordinator.exitSafe());
  assert(NF::lease&&bg.acquires==bg.ends+1&&finishes==0&&providerStops==0);invalid(savedApi);
  if(scenario=="retained"){
   assert(coordinator.retained()&&!fixture.store.exitSafe()&&NF::fsCalls==1);Coordinator::Lease rejected;
   assert(coordinator.acquireNative(reload.slot,&rejected)==C::Retained);
   risc_key_value_atomic_state_v1 s{sizeof(s)};assert(coordinator.endpoint().state(&s)==RISC_ATOMIC_RETAINED);
  }
  worker.join(); // Fence host scheduler before process exit; never destroy retained custody.
  printf("integrated %s: Runtime pins app/providers, native/background lease retained, stale grants rejected; joined host worker PASS\n",scenario.c_str());
  fflush(stdout);_exit(0);
 }
 assert(ok&&!current.retained()&&coordinator.exitSafe()&&!NF::lease&&bg.acquires==bg.ends);invalid(savedApi);
 verifyIntents(fixture.core);worker.join();assert(NF::handles.empty());
}
int main(int argc,char**argv){
 assert(argc==4);directory=argv[1];NF::rootPath=argv[2];scenario=argv[3];
 assert(scenario=="roundtrip"||scenario=="retained");
 NF::empty();NF::onWorker=false;NF::lease=false;
 NF::powerCheckpoint=[](const char*at){writePause.checkpoint(at);};bootFiles();bootstrapEstablishedNativeState();
 runBoot();phase=1;for(auto&b:boundReaders)b={};runBoot();
 assert(entries==2&&finishes==2&&providerStops==8&&NF::handles.empty());
 puts("Runtime + coordinator + checked filesystem/NVS + pinned System Settings: seven preferences, stale/cancel and fresh reload PASS");
}
