// Compose actual X4/FatFs admission, both actual native SD mailbox types, and
// CheckedNativeReplicaStore/AppDataFiles/NvsReplicaMetadata under one worker.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main native_sd_attempt_fixture_main
#include "native_sd_attempt_test.cpp"
#undef main
#pragma GCC diagnostic pop
#define NATIVE_FIXTURE_THREADED 1
#define N CheckedFixtureMetadata
#include "native_replica_checked_fixture.h"
#undef N
#include "experimental/native_replica/NativeSettingsCoordinator.h"
#include "experimental/native_replica/NativeSdSession.h"
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
namespace NF=NativeFixture;
struct SdNative {
 NF::Fixture&files;Coordinator&coordinator;uint32_t slot=0;
 RiscExperimental::NativeSdSession session;
 RiscExperimental::NativeSdPreparation preparation{this,allowed,prepare};
 N completion;
 SdNative(NF::Fixture&f,Coordinator&c,A&a):files(f),coordinator(c),completion(a,this,allowed,complete){}
 static bool allowed(void*p){auto&s=*static_cast<SdNative*>(p);return s.coordinator.nativeWorker(s.slot);}
 static int load(SdNative&s){assert(allowed(&s)&&task==Task::NativeSettings);fixture_rejected_calls();int rc=s.files.core.load();s.files.store.finishOperation();if(!s.files.store.exitSafe())rc=C::Retained;return rc;}
 static int prepare(void*p){return load(*static_cast<SdNative*>(p));}
 static N::Result complete(void*p,const N::Evidence&){auto&s=*static_cast<SdNative*>(p);return {load(s),0,s.files.core.revision()};}
 Coordinator::NativeClient client(){return {this,sizeof(*this),
  [](void*p){auto&s=*static_cast<SdNative*>(p);return s.preparation.workerStep()||s.completion.workerStep();},
  [](void*p){auto&s=*static_cast<SdNative*>(p);return s.preparation.pending()||s.completion.pending();},
  [](void*p){auto&s=*static_cast<SdNative*>(p);return !s.preparation.pending()&&s.completion.exitSafe()&&!s.session.pinned();},
  [](void*p){auto&s=*static_cast<SdNative*>(p);return s.preparation.retained()||s.completion.state()==N::State::Retained;}};}
};
int main(int argc,char**argv){
 assert(argc==3);const char*mode=argv[1];NF::rootPath=argv[2];std::filesystem::create_directories(NF::rootPath);NF::empty();NF::Fixture files(3);
 task=Task::NativeSettings;NF::onWorker=true;NF::lease=true;assert(!files.core.initialize(nullptr,0,{}));files.store.finishOperation();NF::onWorker=false;NF::lease=false;task=Task::Owner;
 unsigned backgroundEnds=0;
 Coordinator service(files.core,files.store,{&backgroundEnds,
  [](void*){return task==Task::Owner;},[](void*){return task==Task::NativeSettings;},
  [](void*){assert(task==Task::Owner);return !NF::lease.exchange(true);},
  [](void*p){assert(task==Task::Owner&&NF::lease.exchange(false));++*static_cast<unsigned*>(p);}});
 const auto*v=fixture_start();A a({3,55,7,71,v},hooks());attempt=&a;SdNative native(files,service,a);
 assert(!service.registerNative(native.client(),&native.slot));assert(!service.activate());
 bootstrap(a,v); // exact real X4/FatFs refresh/close fence, before native lease
 Coordinator::Lease hold;assert(!service.acquireNative(native.slot,&hold));
 const unsigned reads=fixture_reads(),writes=fixture_writes();
 risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof state;assert(service.endpoint().state(&state)==RISC_ATOMIC_BUSY);
 bool prep=!std::strncmp(mode,"prep",4);
 if(prep){assert(!native.session.bind(a,RiscExperimental::NativeSdSession::Role::ReplicaReader,3));assert(!native.preparation.begin(native.session));if(strstr(mode,"cancel"))native.preparation.cancel();}
 else {assert(!native.completion.issue(evidence()));if(strstr(mode,"cancel"))native.completion.cancel();}
 assert(service.releaseNative(hold)==C::Busy&&!service.exitSafe()&&NF::lease&&a.receiptPinned());
 const unsigned nvsBefore=NF::nvsTotal,fsBefore=NF::fsTotal;
 assert(!service.workerStep());task=Task::ProviderIo;assert(!service.workerStep());task=Task::Owner;
 for(unsigned i=0;i<1000;++i){if(prep)assert(native.preparation.collect()==C::Busy);else{N::Result result{};assert(native.completion.collect(&result)==C::Busy);}}
 assert(NF::nvsTotal==nvsBefore&&NF::fsTotal==fsBefore);
 if(strstr(mode,"native-retained")){NF::armed=true;NF::fault="close";NF::cut=1;}
 std::atomic<bool>stop{false};std::atomic<unsigned>polls{0};
 std::thread settings([&]{task=Task::NativeSettings;NF::onWorker=true;while(!stop.load()){service.workerStep();++polls;std::this_thread::yield();}});
 while(polls.load()<1000)std::this_thread::yield();
 assert(service.releaseNative(hold)==C::Busy&&backgroundEnds==0&&a.receiptPinned());
 if(strstr(mode,"late-retained"))fixture_retain();
 int rc=C::Busy;N::Result result{};
 while(rc==C::Busy){rc=prep?native.preparation.collect():native.completion.collect(&result);std::this_thread::yield();}
 const bool retained=strstr(mode,"retained");
 if(retained){assert(rc==C::Retained);assert(service.releaseNative(hold)==C::Retained&&service.retained()&&!service.exitSafe()&&NF::lease&&backgroundEnds==0&&!a.exitSafe());assert(service.endpoint().state(&state)==RISC_ATOMIC_RETAINED&&service.endpoint().nativeRetained());}
 else {
  assert(rc==(prep&&strstr(mode,"cancel")?C::Stale:C::Ok));
  if(!prep)assert(result.status==(strstr(mode,"cancel")?C::Stale:C::Ok));
  if(prep){assert(service.releaseNative(hold)==C::Busy);assert(!native.session.release());}
  assert(!service.releaseNative(hold)&&backgroundEnds==1&&service.exitSafe()&&!NF::lease);
  assert(!a.release()&&a.exitSafe());
 }
 stop.store(true);settings.join();
 assert(reads==fixture_reads()&&writes==fixture_writes());
 if(strstr(mode,"cancel"))assert(NF::nvsTotal==nvsBefore&&NF::fsTotal==fsBefore);
 if(!retained){attempt=nullptr;fixture_stop();}
 printf("actual X4/FatFs + checked native store + settings coordinator %s PASS\n",mode);
}
