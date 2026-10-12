#ifndef NATIVE_SD_PORT_OWNER
#define NATIVE_SD_PORT_OWNER owner
#endif
#ifndef NATIVE_SD_PORT_OWNER_PREDICATE
#define NATIVE_SD_PORT_OWNER_PREDICATE (task==Task::Owner)
#endif
#ifndef NATIVE_SD_PORT_BIND
#define NATIVE_SD_PORT_BIND bind
#endif
#include "experimental/native_replica/NativeSdPortAttempt.h"
#include "native_sd_port_fixture.h"
#include <RiscDiagnosticSourceV1.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <csignal>
using A=RiscExperimental::NativeSdAttempt;
using P=RiscExperimental::NativeSdPortAttempt;
using Pin=RiscExperimental::NativeRuntimeStoragePin;
using RiscCpu::Port;
using RiscCpu::Hardware;
enum class Task {Owner,ProviderWorker,NativeSettings,Isr,Other};
static thread_local Task task=Task::Owner;
static RiscStorage::Worker mailbox;
static Port*port;
static const native_sd_port_fixture*f;
static unsigned loads,unloads,hardwareCalls;
static uint64_t nowMs;
static std::mutex gate;
static std::condition_variable cv;
static bool blockRead,readEntered,allowRead;
static bool NATIVE_SD_PORT_OWNER(){return NATIVE_SD_PORT_OWNER_PREDICATE;}
static bool worker(){return task==Task::ProviderWorker;}
extern "C" void native_sd_port_loaded(const native_sd_port_fixture*p){assert(NATIVE_SD_PORT_OWNER()&&p&&!f);f=p;++loads;}
extern "C" void native_sd_port_unloaded(){f=nullptr;++unloads;}
extern "C" bool native_sd_port_hardware_allowed(){return worker()||(NATIVE_SD_PORT_OWNER()&&!port->nativeStorageAttached()&&mailbox.idle());}
extern "C" void native_sd_port_before_read(){std::unique_lock<std::mutex> lock(gate);if(!blockRead)return;readEntered=true;cv.notify_all();cv.wait(lock,[]{return allowRead;});}
static RiscCpu::StorageWorkBackend workBackend{&mailbox,[](){return true;},worker,[](uint32_t ms){nowMs+=ms;}};
static Hardware hardware(){
 Hardware h{};h.owner=NATIVE_SD_PORT_OWNER;h.now=[](){return nowMs;};h.sleep=[](uint32_t ms){nowMs+=ms;};h.storageWork=&workBackend;
 h.gpioOpen=[](uint8_t pin,bool output,bool initial,bool pullup){assert(NATIVE_SD_PORT_OWNER()&&pin==5&&output&&initial&&!pullup);++hardwareCalls;return true;};
 h.gpioWrite=[](uint8_t pin,bool){assert(native_sd_port_hardware_allowed()&&pin==5);++hardwareCalls;return true;};
 h.gpioRead=[](uint8_t,bool*){assert(false);return false;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){assert(false);return false;};
 h.gpioClose=[](uint8_t pin){assert(NATIVE_SD_PORT_OWNER()&&pin==5);++hardwareCalls;return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){assert(false);return false;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){assert(false);return false;};h.i2cClose=[](uint8_t){assert(false);return false;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){assert(false);return false;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){assert(false);return false;};
 h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){assert(false);return false;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){assert(false);return false;};h.spiClose=[](uint8_t){assert(false);return false;};
 h.sdmmcOpen=[](uint8_t c,uint8_t m,uint8_t d,uint32_t hz,risc_sdmmc_card_info_v1*out){assert(c==41&&m==42&&d==40&&hz==20000000);++hardwareCalls;return f->card_open(out);};
 h.sdmmcRead=[](uint64_t l,uint32_t n,void*out){++hardwareCalls;return f->card_read(l,n,out);};
 h.sdmmcWrite=[](uint64_t l,uint32_t n,const void*in){++hardwareCalls;return f->card_write(l,n,in);};
 h.sdmmcSync=[](){++hardwareCalls;return f->card_sync();};h.sdmmcClose=[](){++hardwareCalls;return f->card_close();};
 return h;
}
static risc_diagnostic_source_api_v1 diagnostic{1,sizeof(diagnostic),nullptr,[](void*,uint32_t,char*t,uint32_t cap,uint32_t*n,uint64_t*s,uint32_t*r)->int32_t{if(cap)t[0]=0;*n=0;*s=0;*r=0;return 0;}};
static bool NATIVE_SD_PORT_BIND(RiscBoot::Runtime&r){return port->bind(r)&&r.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&diagnostic);}
static void step(){assert(NATIVE_SD_PORT_OWNER()&&!mailbox.idle());task=Task::ProviderWorker;assert(mailbox.workerStep());task=Task::Owner;}
static int close(A&a){for(unsigned i=0;i<20;++i){const int rc=a.close();if(rc!=RISC_STORAGE_ASYNC_PENDING&&rc!=RISC_STORAGE_ASYNC_BUSY)return rc;if(!mailbox.idle())step();}assert(false);return -99;}
static void bootstrap(P&p){
 const unsigned refreshes=f->counter(1),reads=f->counter(5);assert(p.acquire()==0&&p.attempt());auto&a=*p.attempt();
 assert(a.state()==A::State::Admission&&a.fenced()&&f->session());assert(f->counter(1)==refreshes&&f->counter(5)==reads);
 assert(a.refresh()==0&&f->counter(1)==refreshes+1);assert(a.refresh()==A::Context);
 risc_storage_async_result_v1 result{};
 for(unsigned i=0;i<20;++i)assert(a.collect(&result)==RISC_STORAGE_ASYNC_PENDING&&f->counter(5)==reads);
 step();assert(a.collect(&result)==0);assert(close(a)==0&&a.state()==A::State::ReadyClosed);
 assert(!f->session()&&mailbox.idle()&&a.epoch());
 const auto*s=risc_storage_volume_state(f->volume);assert(s&&s->observe(f->volume->context)==RISC_STORAGE_STATE_READY);
}
static void child(P&p){
 auto&a=*p.attempt();assert(a.openChild()==0);risc_storage_async_request_v1 q{};q.operation=RISC_STORAGE_ASYNC_READ_SMALL_FILE;std::strcpy(q.path,"/RTECFG/00000003/state.bin");
 assert(a.submit(q)==0);step();risc_storage_async_result_v1 r{};assert(a.collect(&r)==0&&r.size==5&&!std::memcmp(r.data,"hello",5));assert(close(a)==0);
}
static void seed(){
 const auto*v=f->volume;const auto*e=risc_storage_volume_extension(v);assert(e);
 assert(e->mkdir(v->context,"/RTECFG")&&e->mkdir(v->context,"/RTECFG/00000003"));
 const uint32_t h=v->file_open_write(v->context,"/RTECFG/00000003/state.bin");assert(h);
 assert(v->file_write(v->context,h,"hello",5)==5&&e->file_sync(v->context,h)&&v->file_close(v->context,h,true));
}
static void ordinary(){
 const auto*v=f->volume;uint64_t n=0;char out[6]{};const uint32_t h=v->file_open_read(v->context,"/RTECFG/00000003/state.bin",&n);assert(h&&n==5);
 assert(v->file_read(v->context,h,out,5)==5&&!std::strcmp(out,"hello")&&v->file_close(v->context,h,true));
 const uint32_t d=v->dir_open(v->context,"/RTECFG/00000003");assert(d);risc_storage_dirent_v1 entry{};
 assert(v->dir_next(v->context,d,&entry)&&!std::strcmp(entry.name,"state.bin"));assert(risc_storage_volume_extension(v)->dir_close_checked(v->context,d));
}
static void exportRoundTrip(){
 const auto*v=f->volume;const auto*e=risc_storage_volume_export(v);uint64_t token=0,sectors=0;uint32_t sectorSize=0;
 assert(!port->nativeStorageAttached()&&e&&e->export_begin(v->context,&token,&sectors,&sectorSize)==0&&token&&sectors&&sectorSize==512);
 unsigned char bytes[512]{};assert(e->export_read(v->context,token,0,1,bytes)==0&&bytes[510]==0x55&&bytes[511]==0xaa);
 assert(e->export_end(v->context,token)==0);ordinary();
}
static void endOrdinary(uint64_t s){const auto*a=risc_storage_volume_async(f->volume);for(unsigned i=0;i<10;++i){int rc=a->end(f->volume->context,s);if(rc==0)return;assert(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY);if(!mailbox.idle())step();}assert(false);}
static void gates(P&p,Pin&anchor){
 auto&a=*p.attempt();const auto*w=f->worker;const auto*s=f->sync;const auto*g=f->gpio;const auto before=hardwareCalls;const auto grantCount=loads;
 assert(port->nativeStorageAttached()&&!port->appExitSafe()&&!port->quiescent()&&!port->restartResourcesSafe());
 assert(anchor.release()==RISC_STORAGE_ASYNC_BUSY);assert(!f->quiesce());f->rejected();
 uint64_t token=99;unsigned char request=1,out[32]{};uint32_t n=99;int32_t status=99;
 assert(!s->is_owner(s->context));assert(!s->create(s->context,&token)&&!token);
 assert(!g->claim(g->context,5,true,true,false,&token));
 assert(w->submit(w->context,&request,1,&token)==RISC_STORAGE_WORK_CONTEXT&&!token);
 assert(w->collect(w->context,1,out,sizeof(out),&n,&status)==RISC_STORAGE_WORK_CONTEXT&&!n);
 assert(w->cancel(w->context,1)==RISC_STORAGE_WORK_CONTEXT&&w->unbind(w->context)==RISC_STORAGE_WORK_CONTEXT);
 assert(w->release(w->context)==RISC_STORAGE_WORK_BUSY);
 risc_sdmmc_card_info_v1 card{};assert(!w->io->open(w->io->context,&card));assert(!w->io->power(w->io->context,true));assert(!w->io->sync(w->io->context)&&!w->io->close(w->io->context));
 for(Task other:{Task::Other,Task::NativeSettings,Task::Isr,Task::ProviderWorker}){
  task=other;assert(a.openChild()==A::Context&&a.refresh()==A::Context&&a.close()==A::Context);
  assert(p.release()==RISC_STORAGE_ASYNC_INVALID);assert(!s->is_owner(s->context));assert(!g->claim(g->context,5,true,true,false,&token));
  assert(w->submit(w->context,&request,1,&token)==RISC_STORAGE_WORK_CONTEXT&&!token);
  if(other!=Task::ProviderWorker)assert(!w->io->power(w->io->context,true));
 }
 task=Task::Owner;assert(hardwareCalls==before&&loads==grantCount&&unloads==0);assert(a.state()==A::State::ReadyClosed);
}
static void refusedIsInert(P&p){
 assert(p.exitSafe()&&!port->nativeStorageAttached());const unsigned before=hardwareCalls;
 if(auto*a=p.attempt()){
  assert(a->state()==A::State::Idle&&a->acquire(a->binding().volume)==A::Context);
  assert(a->openChild()==A::Context&&a->refresh()==A::Context&&a->close()==A::Context&&a->release()==A::Context);
  a->cancel();assert(a->state()==A::State::Idle&&a->exitSafe());
 }
 assert(p.release()==RISC_STORAGE_ASYNC_INVALID&&hardwareCalls==before);
}
static void retainedExit(RiscBoot::Runtime&r,P&p,unsigned grants){
 assert(!p.exitSafe()&&port->nativeStorageAttached()&&!port->appExitSafe()&&!port->quiescent());
 assert(r.grantUsage().graphLive==grants&&loads==1&&unloads==0);
 std::puts("Runtime graph + actual X4/FatFs + real CpuPort: sticky retention PASS");std::fflush(stdout);std::_Exit(0);
}
int main(int argc,char**argv){
 assert(argc==3);const char*mode=argv[2];Port cpu(hardware());port=&cpu;
 RiscBoot::Port boot{NATIVE_SD_PORT_OWNER,[](risc_runtime_health_v1*){return true;},[](uint32_t ms){nowMs+=ms;},[](const char*){return true;},NATIVE_SD_PORT_BIND};
 boot.appExitSafe=[](){return port->appExitSafe();};boot.providerStorageSafe=[](){return port->providerStorageSafe();};
 RiscBoot::Runtime runtime(boot);if(!runtime.prepare(argv[1])){std::fprintf(stderr,"prepare: %s\n",runtime.error());assert(false);}
 assert(!loads&&!hardwareCalls);Pin anchor(runtime,cpu,55);const int admitted=anchor.acquire();if(admitted){std::fprintf(stderr,"anchor admission: %d, %s\n",admitted,runtime.error());assert(false);}
 assert(f&&loads==1&&!unloads&&f->counter(5)==0&&!f->session());const auto baseline=runtime.grantUsage().graphLive;
 if(!std::strcmp(mode,"wrapper-lifetime")){
  for(unsigned state=0;state<3;++state){
   int signalPipe[2];assert(pipe(signalPipe)==0);const pid_t pid=fork();assert(pid>=0);
   if(!pid){::close(signalPipe[0]);struct rlimit limit{0,0};assert(setrlimit(RLIMIT_CORE,&limit)==0);P*live=new P(runtime,cpu,3,55);assert(live->acquire()==0);
    if(state==1)assert(live->attempt()->refresh()==0);
    if(state==2){f->fault(4);assert(live->attempt()->refresh()==RISC_STORAGE_ASYNC_RETAINED);}
    assert(write(signalPipe[1],"D",1)==1);::close(signalPipe[1]);delete live;std::_Exit(88);
   }
   ::close(signalPipe[1]);char marker=0;assert(read(signalPipe[0],&marker,1)==1&&marker=='D');::close(signalPipe[0]);
   int status=0;assert(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  }
 }
 {
  P fresh(runtime,cpu,3,55);
  if(!std::strcmp(mode,"read-retained")){
   assert(fresh.acquire()==0);f->fault(1);assert(fresh.attempt()->refresh()==0);step();risc_storage_async_result_v1 r{};assert(fresh.attempt()->collect(&r)==RISC_STORAGE_ASYNC_IO);
   assert(close(*fresh.attempt())==RISC_STORAGE_ASYNC_RETAINED);retainedExit(runtime,fresh,baseline+1);
  }
  bootstrap(fresh);assert(f->counter(1)==1&&f->counter(3)==0);
  if(!std::strcmp(mode,"gates"))gates(fresh,anchor);
  if(!std::strcmp(mode,"provider-retained")){f->fault(3);assert(fresh.release()==RISC_STORAGE_ASYNC_RETAINED);retainedExit(runtime,fresh,baseline+1);}
  assert(fresh.release()==0&&fresh.exitSafe());
 }
 assert(!cpu.nativeStorageAttached()&&runtime.grantUsage().graphLive==baseline&&f->counter(1)==1);seed();ordinary();
 if(!std::strcmp(mode,"busy-session")||!std::strcmp(mode,"busy-worker")){
  const auto*v=f->volume;const auto*a=risc_storage_volume_async(v);uint64_t session=0,operation=0;assert(a->begin(v->context,&session)==0&&session);const auto reads=f->counter(5),checked=f->counter(3);
  if(!std::strcmp(mode,"busy-worker")){risc_storage_async_request_v1 q{};q.operation=RISC_STORAGE_ASYNC_READ_SMALL_FILE;std::strcpy(q.path,"/RTECFG/00000003/state.bin");assert(a->submit(v->context,session,&q,&operation)==0);}
  {P refused(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(refused.acquire()==RISC_STORAGE_ASYNC_BUSY&&refused.exitSafe());refusedIsInert(refused);}
  assert(!cpu.nativeStorageAttached()&&f->session()==session&&f->counter(5)==reads&&runtime.grantUsage().graphLive==baseline);
  assert(f->counter(3)==checked+unsigned(!operation));
  if(operation){assert(!mailbox.idle());step();risc_storage_async_result_v1 r{};assert(a->collect(v->context,session,operation,&r)==0&&r.size==5&&!std::memcmp(r.data,"hello",5));}
  endOrdinary(session);ordinary();
 }
 if(!std::strcmp(mode,"busy-handle")){
  const auto*v=f->volume;const auto*e=risc_storage_volume_extension(v);
  for(unsigned kind=0;kind<3;++kind){
   uint64_t n=0;uint32_t handle=kind==0?v->file_open_read(v->context,"/RTECFG/00000003/state.bin",&n):kind==1?v->dir_open(v->context,"/RTECFG/00000003"):v->file_open_write(v->context,"/RTECFG/00000003/pending.bin");assert(handle);
   const auto reads=f->counter(5),refreshes=f->counter(1);
   {P refused(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(refused.acquire()==RISC_STORAGE_ASYNC_BUSY&&refused.exitSafe());refusedIsInert(refused);}
   assert(f->handles()&&!f->session()&&f->counter(5)==reads&&f->counter(1)==refreshes&&!cpu.nativeStorageAttached());
   if(kind==0){char out[6]{};assert(n==5&&v->file_read(v->context,handle,out,5)==5&&!std::strcmp(out,"hello"));assert(v->file_close(v->context,handle,true));}
   if(kind==1){risc_storage_dirent_v1 entry{};assert(v->dir_next(v->context,handle,&entry)&&!std::strcmp(entry.name,"state.bin"));assert(e->dir_close_checked(v->context,handle));}
   if(kind==2){assert(v->file_write(v->context,handle,"safe",4)==4&&v->file_close(v->context,handle,true));assert(v->remove(v->context,"/RTECFG/00000003/pending.bin"));}
   ordinary();
  }
 }
 if(!std::strcmp(mode,"busy-export")){
  const auto*v=f->volume;const auto*e=risc_storage_volume_export(v);uint64_t token=0,sectors=0;uint32_t sectorSize=0;
  assert(e&&e->export_begin(v->context,&token,&sectors,&sectorSize)==0&&token&&sectors&&sectorSize==512);
  const auto reads=f->counter(5),refreshes=f->counter(1);
  {P refused(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(refused.acquire()==RISC_STORAGE_ASYNC_BUSY&&refused.exitSafe());refusedIsInert(refused);}
  assert(f->counter(5)==reads&&f->counter(1)==refreshes&&!cpu.nativeStorageAttached());
  unsigned char bytes[512]{};assert(e->export_read(v->context,token,0,1,bytes)==0&&bytes[510]==0x55&&bytes[511]==0xaa);
  assert(e->export_end(v->context,token)==0);ordinary();
 }
 if(!std::strcmp(mode,"busy-sleep")){
  const auto*v=f->volume;const auto*s=risc_storage_volume_sleep(v);assert(s&&s->prepare_sleep(v->context));
  const auto reads=f->counter(5),refreshes=f->counter(1);
  {P refused(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(refused.acquire()==RISC_STORAGE_ASYNC_BUSY&&refused.exitSafe());refusedIsInert(refused);}
  assert(f->counter(5)==reads&&f->counter(1)==refreshes&&!cpu.nativeStorageAttached());
  assert(s->resume_sleep(v->context)==0);ordinary();
 }
 if(!std::strcmp(mode,"busy-sync")){
  const auto*s=f->sync;uint64_t lock=0;assert(s->create(s->context,&lock)&&lock&&s->try_lock(s->context,lock));const auto checked=f->counter(3);
  {P refused(runtime,cpu,3,55,A::Profile::CheckedWarm);assert(refused.acquire()==RISC_STORAGE_ASYNC_BUSY&&refused.exitSafe());refusedIsInert(refused);}
  assert(f->counter(3)==checked&&!cpu.nativeStorageAttached());assert(s->unlock(s->context,lock)&&s->destroy(s->context,lock));ordinary();
 }
 if(!std::strcmp(mode,"contract-busy-token")||!std::strcmp(mode,"contract-ok-zero")){
  f->fault(!std::strcmp(mode,"contract-busy-token")?6:7);P*malformed=new P(runtime,cpu,3,55,A::Profile::CheckedWarm);
  assert(malformed->acquire()==RISC_STORAGE_ASYNC_RETAINED&&f->session());
  int signalPipe[2];assert(pipe(signalPipe)==0);const pid_t pid=fork();assert(pid>=0);
  if(!pid){::close(signalPipe[0]);struct rlimit limit{0,0};assert(setrlimit(RLIMIT_CORE,&limit)==0);assert(write(signalPipe[1],"D",1)==1);::close(signalPipe[1]);delete malformed;std::_Exit(88);}
  ::close(signalPipe[1]);char marker=0;assert(read(signalPipe[0],&marker,1)==1&&marker=='D');::close(signalPipe[0]);
  int status=0;assert(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  retainedExit(runtime,*malformed,baseline+1);
 }
 for(unsigned cycle=0;cycle<3;++cycle){
  P warm(runtime,cpu,3,55,A::Profile::CheckedWarm);
  if(!std::strcmp(mode,"cancel-running")&&cycle==0){
   bootstrap(warm);assert(warm.attempt()->openChild()==0);
   risc_storage_async_request_v1 q{};q.operation=RISC_STORAGE_ASYNC_READ_SMALL_FILE;std::strcpy(q.path,"/RTECFG/00000003/state.bin");
   assert(warm.attempt()->submit(q)==0);blockRead=true;
   std::thread thread([]{task=Task::ProviderWorker;assert(mailbox.workerStep());});
   {std::unique_lock<std::mutex> lock(gate);assert(cv.wait_for(lock,std::chrono::seconds(5),[]{return readEntered;}));}
   assert(!cpu.appExitSafe()&&!cpu.quiescent());warm.attempt()->cancel();assert(warm.attempt()->close()==RISC_STORAGE_ASYNC_PENDING&&!warm.exitSafe());
   {std::lock_guard<std::mutex> lock(gate);allowRead=true;}cv.notify_all();thread.join();blockRead=false;
   assert(close(*warm.attempt())==0&&warm.attempt()->state()==A::State::Cancelled);assert(warm.release()==0);ordinary();continue;
  }
  bootstrap(warm);assert(f->counter(1)==cycle+2);
  if(!std::strcmp(mode,"adapter-retained")){assert(warm.attempt()->openChild()==0);f->fault(4);risc_storage_async_request_v1 q{};q.operation=RISC_STORAGE_ASYNC_OPEN_DIRECTORY;std::strcpy(q.path,"/");assert(warm.attempt()->submit(q)==RISC_STORAGE_ASYNC_RETAINED);retainedExit(runtime,warm,baseline+1);}
  child(warm);assert(warm.release()==0&&warm.exitSafe());ordinary();exportRoundTrip();
 }
 assert(f->counter(1)==4&&!f->session()&&!f->handles()&&loads==1&&unloads==0&&runtime.grantUsage().graphLive==baseline);
 if(!std::strcmp(mode,"cleanup-retained")){
  f->fault(2);assert(anchor.release()==RISC_STORAGE_ASYNC_RETAINED&&runtime.retained());assert(!anchor.exitSafe()&&runtime.grantUsage().graphLive==baseline&&loads==1&&!unloads&&!cpu.quiescent());
  std::puts("Runtime graph retains actual X4 module/dependencies after failed hardware cleanup PASS");std::fflush(stdout);std::_Exit(0);
 }
 assert(anchor.release()==0&&anchor.exitSafe()&&!f&&unloads==1&&!mailbox.bound()&&cpu.quiescent());
 assert(runtime.grantUsage().graphLive==0);std::printf("Runtime graph + actual X4/FatFs + real CpuPort %s PASS\n",mode);
}
