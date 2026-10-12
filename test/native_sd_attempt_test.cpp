#include "experimental/native_replica/NativeSdCompletion.h"
#include "native_sd_attempt_fixture.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
using A=RiscExperimental::NativeSdAttempt;
using N=RiscExperimental::NativeSdCompletion;
using C=RiscExperimental::NativeReplicaCore;
using W=RiscStorage::Worker;
enum class Task {Other,Owner,ProviderIo,NativeSettings};
static thread_local Task task=Task::Other;
static W worker;static A*attempt;static bool fenceHeld,fresh=true,lease=true,scopeFault;
static uint64_t epoch=1;static unsigned credits,denials,nativeCalls,reentries;static bool injectReentry,cleanupPadding,malformedSize,malformedStatus;static unsigned paddedEnds;static int nativeStatus=C::Ok;
static std::mutex gate;static std::condition_variable cv;static bool blockRead,enteredRead,allowRead;
static bool blockNative,enteredNative,allowNative,lateRetain,retainOnSnapshot;
extern "C" {
bool fixture_sync_admission(){if(task!=Task::Owner)return false;if(!attempt)return true;assert(!attempt->syncAdmission(99));const bool yes=attempt->syncAdmission(55);if(yes&&fenceHeld){++credits;if(injectReentry){
  const auto old=attempt->state();assert(!attempt->syncAdmission(55));attempt->cancel();assert(attempt->close()==A::Context);assert(attempt->release()==A::Context);assert(attempt->state()==old);fixture_rejected_calls();++reentries;
 }}else if(!yes)++denials;return yes;}
bool fixture_provider_worker(){return task==Task::ProviderIo;}
bool fixture_raw_io_allowed(){return task==Task::ProviderIo||(!fenceHeld&&task==Task::Owner);}
int fixture_worker_bind(risc_storage_work_execute_v1 fn,void*c){assert(task==Task::Owner&&!fenceHeld);return worker.bind(fn,c);}
int fixture_worker_submit(const void*p,uint32_t n,uint64_t*out){
 assert(!attempt->rawSubmit(99,p,n));assert(!fixture_sync_admission());
 risc_storage_async_request_v1 padded;
 if(cleanupPadding&&attempt->dispatch()==A::Call::End){std::memset(&padded,0xff,sizeof padded);padded.operation=0;std::memset(padded.path,0,sizeof padded.path);std::memset(padded.destination,0,sizeof padded.destination);p=&padded;++paddedEnds;}
 if(task!=Task::Owner||!attempt)return RISC_STORAGE_WORK_CONTEXT;
 assert(!attempt->rawSubmit(71,p,n-1));
 if(attempt->dispatch()!=A::Call::End){unsigned char other[RISC_STORAGE_WORKER_BYTES]{};assert(!attempt->rawSubmit(71,other,n));}
 if(!attempt->rawSubmit(71,p,n))return RISC_STORAGE_WORK_CONTEXT;
 assert(!attempt->rawSubmit(71,p,n));
 const int rc=worker.submit(p,n,out);attempt->submitted(71,rc,rc==0?*out:0);return rc;
}
int fixture_worker_collect(uint64_t t,void*p,uint32_t n,uint32_t*size,int32_t*status){assert(!attempt->rawCollect(99,t));assert(!attempt->rawCollect(71,t+1));assert(!fixture_sync_admission());if(task!=Task::Owner||!attempt||!attempt->rawCollect(71,t))return RISC_STORAGE_WORK_CONTEXT;const int rc=worker.collect(t,p,n,size,status);if(rc==0&&malformedSize)--*size;if(rc==0&&malformedStatus)static_cast<risc_storage_async_result_v1*>(p)->status=27;if(rc==0&&lateRetain&&attempt->dispatch()==A::Call::End){fixture_retain();lease=false;}return rc;}
int fixture_worker_cancel(uint64_t t){assert(!attempt->rawCancel(99,t));assert(!attempt->rawCancel(71,t+1));assert(!fixture_sync_admission());if(task!=Task::Owner||!attempt||!attempt->rawCancel(71,t))return RISC_STORAGE_WORK_CONTEXT;return worker.cancel(t);}
int fixture_worker_unbind(){assert(task==Task::Owner&&!fenceHeld);return worker.unbind();}
void fixture_before_read(){std::unique_lock<std::mutex> lock(gate);if(!blockRead)return;enteredRead=true;cv.notify_all();cv.wait(lock,[]{return allowRead;});}
}
static A::Hooks hooks(){return {nullptr,
 [](void*){return task==Task::Owner;},[](void*,const A::Binding&b){return fresh&&b.device==55&&b.grant==7&&b.namespaceId==3;},
 [](void*){return worker.idle();},[](void*,const A::Binding&){return lease;},
 [](void*,bool b){assert(task==Task::Owner);fenceHeld=b;if(b)fresh=false;},
 [](void*,uint64_t*out){assert(task==Task::Owner&&worker.idle());*out=epoch;if(retainOnSnapshot){fixture_retain();lease=false;}return scopeFault?A::Scope::Retained:A::Scope::Healthy;}};}
static void step(){task=Task::ProviderIo;assert(worker.workerStep());task=Task::Owner;}
static void close(A&a){for(unsigned n=0;n<10;++n){const int rc=a.close();if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY){if(!worker.idle())step();continue;}assert(!rc);return;}assert(false);}
static void bootstrap(A&a,const risc_storage_volume_api_v1*v){
 assert(fixture_observe()==RISC_STORAGE_STATE_UNAVAILABLE);assert(a.acquire(v)==0&&a.state()==A::State::Admission);
 assert(fixture_observe()==RISC_STORAGE_STATE_UNAVAILABLE);assert(a.refresh()==0);
 risc_storage_async_result_v1 result{};for(unsigned n=0;n<100;++n)assert(a.collect(&result)==RISC_STORAGE_ASYNC_PENDING&&!fixture_reads());
 step();assert(a.collect(&result)==0);close(a);assert(a.state()==A::State::ReadyClosed&&fixture_observe()==RISC_STORAGE_STATE_READY);
}
static N::Result execute(void*,const N::Evidence&e){
 assert(task==Task::NativeSettings&&!fixture_provider_worker()&&!fixture_raw_io_allowed());assert(e.action==5);
 fixture_rejected_calls();
 std::unique_lock<std::mutex> lock(gate);++nativeCalls;
 if(blockNative){enteredNative=true;cv.notify_all();cv.wait(lock,[]{return allowNative;});}
 return {nativeStatus,4,23};
}
static bool nativeWorker(void*){return task==Task::NativeSettings;}
static N::Evidence evidence(){N::Evidence e{};e.action=5;e.count=1;e.mapBytes=8;e.coreRevision=22;e.frontier=4;e.identity.known=true;e.identity.generation=7;e.identity.lineage[0]=2;return e;}
int main(int argc,char**argv){
 assert(argc==2);task=Task::Owner;const char*mode=argv[1];const auto*v=fixture_start();A a({3,55,7,71,v},hooks());attempt=&a;
 if(!std::strcmp(mode,"foreign-session")){
  attempt=nullptr;const uint64_t s=fixture_foreign_begin();attempt=&a;
  assert(a.acquire(v)==RISC_STORAGE_ASYNC_BUSY&&!a.fenced()&&worker.idle());attempt=nullptr;assert(fixture_foreign_end(s));fixture_stop();return 0;
 }
 if(!std::strcmp(mode,"wrong-binding")){auto copy=*v;assert(a.acquire(&copy)==A::Unsupported);attempt=nullptr;fixture_stop();return 0;}
 if(!std::strcmp(mode,"warm")){fresh=false;assert(a.acquire(v)==A::Unsupported&&!a.fenced()&&!fixture_reads());attempt=nullptr;fixture_stop();return 0;}
 if(!std::strcmp(mode,"wrong-task")){task=Task::Other;assert(a.acquire(v)==A::Context);task=Task::Owner;attempt=nullptr;fixture_stop();return 0;}
 if(!std::strcmp(mode,"clean-unavailable")){
  fixture_open_failure(true);assert(a.acquire(v)==0&&a.refresh()==0);step();risc_storage_async_result_v1 r{};assert(a.collect(&r)==RISC_STORAGE_ASYNC_IO);
  assert(a.close()==A::Lost&&a.state()==A::State::Unavailable);assert(a.release()==0&&a.exitSafe());attempt=nullptr;fixture_stop();return 0;
 }
 if(!std::strcmp(mode,"absent")){
  fixture_absent(true);assert(a.acquire(v)==0&&a.refresh()==0);step();risc_storage_async_result_v1 r{};assert(a.collect(&r)==RISC_STORAGE_ASYNC_IO);
  const int rc=a.close();assert(rc==RISC_STORAGE_ASYNC_RETAINED&&a.state()==A::State::Retained&&!a.exitSafe());return 0;
 }
 if(!std::strcmp(mode,"cancel-running")){
  assert(a.acquire(v)==0&&a.refresh()==0);blockRead=true;
  std::thread thread([]{task=Task::ProviderIo;assert(worker.workerStep());});
  {std::unique_lock<std::mutex> lock(gate);cv.wait(lock,[]{return enteredRead;});}
  assert(fixture_observe()==RISC_STORAGE_STATE_UNAVAILABLE);a.cancel();assert(a.close()==RISC_STORAGE_ASYNC_PENDING&&!a.exitSafe());
  {std::lock_guard<std::mutex> lock(gate);allowRead=true;}cv.notify_all();thread.join();
  close(a);assert(a.state()==A::State::Cancelled);assert(a.release()==0);attempt=nullptr;fixture_stop();return 0;
 }
 if(!std::strcmp(mode,"reentry"))injectReentry=true;
 if(!std::strcmp(mode,"cleanup-padding"))cleanupPadding=true;
 bootstrap(a,v);
 if(!std::strcmp(mode,"snapshot-retained")){retainOnSnapshot=true;assert(a.release()==RISC_STORAGE_ASYNC_RETAINED&&a.state()==A::State::Retained&&!a.exitSafe());return 0;}
 if(!std::strcmp(mode,"scope-retained")){scopeFault=true;lease=false;assert(a.release()==RISC_STORAGE_ASYNC_RETAINED&&!a.exitSafe());return 0;}
 if(!std::strcmp(mode,"closed-unavailable")){fixture_unavailable();assert(a.release()==0&&a.exitSafe());attempt=nullptr;fixture_stop();return 0;}
 assert(!a.exitSafe()&&a.fenced()&&!a.syncAdmission(99));fixture_rejected_calls();
 if(!std::strncmp(mode,"native-",7)){
  N n(a,nullptr,nativeWorker,execute);
  if(!std::strcmp(mode,"native-preloss")){n.unexpectedLoss();assert(n.issue(evidence())==C::Invalid&&nativeCalls==0&&!n.pending());assert(a.release()==0);attempt=nullptr;fixture_stop();return 0;}
  if(!std::strcmp(mode,"native-result-retained"))nativeStatus=C::Retained;
  if(!std::strcmp(mode,"native-result-uncertain"))nativeStatus=C::Uncertain;
  assert(n.issue(evidence())==C::Ok&&a.receiptPinned());assert(a.release()==A::Context);
  if(!std::strcmp(mode,"native-cancel"))n.cancel();
  if(!std::strcmp(mode,"native-running")){
   blockNative=true;std::thread t([&]{task=Task::NativeSettings;assert(n.workerStep());});
   {std::unique_lock<std::mutex> lock(gate);cv.wait(lock,[]{return enteredNative;});}
   n.cancel();assert(n.state()==N::State::Committing&&!n.exitSafe());
   {std::lock_guard<std::mutex> lock(gate);allowNative=true;}cv.notify_all();t.join();
  }else{task=Task::NativeSettings;assert(n.workerStep());task=Task::Owner;}
  if(!std::strcmp(mode,"native-retained")){fixture_retain();lease=false;}
  if(!std::strcmp(mode,"native-loss"))n.unexpectedLoss();
  N::Result result{};const int rc=n.collect(&result);
  if(!std::strcmp(mode,"native-retained")||!std::strcmp(mode,"native-loss")||nativeStatus!=C::Ok){
   assert(rc==C::Retained&&a.state()==A::State::Retained&&!n.exitSafe()&&!a.exitSafe()&&n.retainedNativeResult().status==nativeStatus);return 0;
  }
  assert(rc==C::Ok&&n.exitSafe()&&!a.receiptPinned());
  assert(result.status==(!std::strcmp(mode,"native-cancel")?C::Stale:C::Ok));
  assert(nativeCalls==(!std::strcmp(mode,"native-cancel")?0u:1u));assert(a.release()==0);
 }else{
  assert(a.openChild()==0&&a.state()==A::State::OwnedSession);assert(fixture_observe()==RISC_STORAGE_STATE_UNAVAILABLE);
  if(!std::strcmp(mode,"submit-retained")){fixture_adapter_retain();risc_storage_async_request_v1 q{};q.operation=RISC_STORAGE_ASYNC_OPEN_DIRECTORY;std::strcpy(q.path,"/");assert(a.submit(q)==RISC_STORAGE_ASYNC_RETAINED&&a.state()==A::State::Retained&&!a.exitSafe());return 0;}
  if(!std::strcmp(mode,"package-retained")){fixture_adapter_retain();risc_storage_package_request_v1 p{};p.operation=RISC_STORAGE_PACKAGE_RECOVER;std::strcpy(p.path,"/RTECFG/00000003/replica.index");assert(a.package(p)==RISC_STORAGE_ASYNC_RETAINED&&a.state()==A::State::Retained&&!a.exitSafe());return 0;}
  if(!std::strcmp(mode,"package")){
   risc_storage_package_request_v1 p{};p.operation=RISC_STORAGE_PACKAGE_RECOVER;std::strcpy(p.path,"/RTECFG/00000003/replica.index");assert(a.package(p)==0);step();risc_storage_async_result_v1 result{};const int rc=a.collect(&result);assert(rc==0||rc==RISC_STORAGE_ASYNC_IO);close(a);assert(a.release()==0);attempt=nullptr;fixture_stop();return 0;
  }
  risc_storage_async_request_v1 q{};q.operation=RISC_STORAGE_ASYNC_OPEN_DIRECTORY;
  std::strcpy(q.path,"/RTECFG/00000004");assert(a.submit(q)==A::Context);
  std::strcpy(q.path,"/RTECFG/00000003/../escape");assert(a.submit(q)==A::Context);
  std::memset(q.path,'x',sizeof q.path);assert(a.submit(q)==A::Context);
  std::memset(q.path,0,sizeof q.path);std::strcpy(q.path,"/");assert(a.submit(q)==0);
  if(!std::strcmp(mode,"uncopied-invalid")){fixture_corrupt_operation();risc_storage_async_result_v1 r{};assert(a.collect(&r)==RISC_STORAGE_ASYNC_RETAINED&&a.state()==A::State::Retained&&!a.exitSafe());return 0;}
  if(!std::strcmp(mode,"malformed-size")||!std::strcmp(mode,"malformed-status")){
   step();malformedSize=!std::strcmp(mode,"malformed-size");malformedStatus=!std::strcmp(mode,"malformed-status");risc_storage_async_result_v1 r{};assert(a.collect(&r)==RISC_STORAGE_ASYNC_RETAINED&&a.state()==A::State::Retained&&!a.exitSafe());return 0;
  }
  if(!std::strcmp(mode,"cancel-queued")){a.cancel();close(a);assert(a.state()==A::State::Cancelled);}
  else{
   step();risc_storage_async_result_v1 r{};assert(a.collect(&r)==0);
   if(!std::strcmp(mode,"late-retained"))lateRetain=true;
   if(!std::strcmp(mode,"late-retained")){
    int rc=a.close();assert(rc==RISC_STORAGE_ASYNC_PENDING);step();rc=a.close();assert(rc==RISC_STORAGE_ASYNC_RETAINED&&!a.exitSafe());return 0;
   }
   if(!std::strcmp(mode,"cancel-complete"))a.cancel();
   close(a);
  }
  assert(a.release()==0);
 }
 if(injectReentry)assert(reentries==credits);
 if(cleanupPadding)assert(paddedEnds);
 assert(!fenceHeld&&a.exitSafe()&&credits&&denials);attempt=nullptr;fixture_stop();
 std::printf("native SD attempt %s PASS (one-use credits=%u, rejected calls=%u)\n",mode,credits,denials);
}
