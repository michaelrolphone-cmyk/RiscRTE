#define main original_http_main
#include "native_http_test.cpp"
#undef main
#include "ports/esp32s3/NativeTimeSample.h"
#include "ports/esp32s3/TimeSampleWorker.h"
#include <thread>
#include <condition_variable>
#include <mutex>
static void* samplerAlloc(size_t,unsigned);static void samplerFree(void*);
#define heap_caps_malloc samplerAlloc
#define heap_caps_free samplerFree
#include "ports/esp32s3/NativeAsyncTimeSample.h"
#undef heap_caps_malloc
#undef heap_caps_free
static bool samplerOom=false,taskFail=false,internalMemory=true;
static unsigned samplerLive=0,taskCreates=0,notifications=0;static TaskHandle_t currentTask=nullptr;
static void* samplerAlloc(size_t n,unsigned caps){assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(samplerOom)return nullptr;++samplerLive;return malloc(n);}
static void samplerFree(void* p){assert(samplerLive);--samplerLive;free(p);}
bool esp_ptr_in_dram(const void*){return internalMemory;}
int xTaskCreate(void(*)(void*),const char*,uint32_t size,void*,unsigned,TaskHandle_t* out){assert(size==8192);++taskCreates;if(taskFail)return 0;*out=reinterpret_cast<void*>(1);return pdPASS;}
TaskHandle_t xTaskGetCurrentTaskHandle(){return currentTask;}
bool xPortInIsrContext(){return false;}
void vTaskDelay(unsigned){}
void xTaskNotifyGive(TaskHandle_t task){assert(task);++notifications;}
uint32_t ulTaskNotifyTake(int,uint32_t){return 0;}
namespace N=RiscCpu::NativeTimeSample;
static risc_time_sample_v1 snap(){return {sizeof(risc_time_sample_v1),99,99,99,99,99};}
static uint64_t beginSample(uint32_t timeout=5000){
 N::configure([](){return owned;},[](){return online;});
 char name[]="time.example.com";risc_time_sample_request_v1 r{sizeof(r),timeout,name};uint64_t t=0;
 assert(N::open(nullptr,&r,&t)==0&&t);name[0]='x';return t;
}
static int advance(uint64_t t){auto out=snap(),before=out;int rc=N::step(nullptr,t,&out);if(rc!=0)assert(!memcmp(&out,&before,sizeof(out)));return rc;}
static void sendRequest(uint64_t t){assert(advance(t)==1&&resolvedName=="time.example.com");assert(advance(t)==1);}
static std::mutex dnsMutex;static std::condition_variable dnsCv;static bool dnsEntered=false,dnsResume=false;
static void blockedDns(){std::unique_lock<std::mutex> lock(dnsMutex);dnsEntered=true;dnsCv.notify_all();dnsCv.wait(lock,[]{return dnsResume;});}
static void startupTests(){
 namespace A=RiscCpu::NativeAsyncTimeSample;
 auto owner=[](){return currentTask==nullptr;};auto network=[](){return online;};
 samplerOom=true;assert(!A::prepare(owner,network)&&!samplerLive&&!taskCreates);samplerOom=false;
 internalMemory=false;assert(!A::prepare(owner,network)&&!samplerLive&&!taskCreates);internalMemory=true;
 taskFail=true;assert(!A::prepare(owner,network)&&!samplerLive&&taskCreates==1);uint64_t unavailable=99;assert(A::open(nullptr,nullptr,&unavailable)==RISC_TIME_SAMPLE_CLOSED&&!unavailable);taskFail=false;
 reset();assert(A::prepare(owner,network)&&samplerLive==1&&taskCreates==2);assert(A::prepare(owner,network)&&taskCreates==2);
 risc_time_sample_request_v1 r{sizeof(r),5000,"time.example.com"};uint64_t token=0;assert(A::open(nullptr,&r,&token)==0);assert(!dnsCalls&&notifications==1&&A::mailbox->workPending());
 currentTask=A::task;auto out=snap();assert(A::step(nullptr,token,&out)==RISC_TIME_SAMPLE_CONTEXT);A::mailbox->workerStep();A::mailbox->workerStep();currentTask=nullptr;
 assert(dnsCalls==1);assert(A::close(nullptr,token)==1);currentTask=A::task;A::mailbox->workerStep();currentTask=nullptr;assert(A::close(nullptr,token)==0&&A::idle()&&A::safe()&&!A::mailbox->workPending()&&notifications==2);
 // No real task exists in this host startup fixture; reclaim its test allocation.
 A::mailbox->~TimeSampleWorker();samplerFree(A::mailbox);A::mailbox=nullptr;A::task=nullptr;
 puts("Native sampler startup: internal placement, OOM, task failure, single permanent task, owner separation and asynchronous cleanup PASS");
}
static void workerTests(){
 for(unsigned phase=0;phase<5;++phase){reset();N::configure([](){return owned;},[](){return online;});TimeSampleWorker worker(N::api(),HttpTimeSample::now);
  char host[]="time.example.com";risc_time_sample_request_v1 request{sizeof(request),5000,host};uint64_t token=0;assert(worker.open(&request,&token)==0);host[0]='x';auto out=snap();
  for(unsigned n=0;n<phase;++n){if(n==3)datagrams.push_back(response());worker.workerStep();}
  if(phase==4){assert(worker.step(token,&out)==0&&out.utc_seconds==1780000000&&out.flags==RISC_TIME_SAMPLE_UNAUTHENTICATED);}
  assert(worker.close(token)==1&&!worker.idle());worker.workerStep();assert(worker.close(token)==0&&worker.close(token)==0&&worker.idle()&&!udpLive);
  assert(worker.step(token,&out)==RISC_TIME_SAMPLE_CLOSED);
 }
 for(unsigned cancel=0;cancel<2;++cancel){reset();N::configure([](){return owned;},[](){return online;});TimeSampleWorker worker(N::api(),HttpTimeSample::now);
  risc_time_sample_request_v1 request{sizeof(request),50,"time.example.com"};uint64_t token=0;assert(worker.open(&request,&token)==0);worker.workerStep();
  dnsEntered=dnsResume=false;dnsHook=blockedDns;std::thread thread([&]{worker.workerStep();});
  {std::unique_lock<std::mutex> lock(dnsMutex);dnsCv.wait(lock,[]{return dnsEntered;});}
  auto out=snap(),before=out;assert(worker.step(token,&out)==1&&!memcmp(&out,&before,sizeof(out)));uint64_t other=9;assert(worker.open(&request,&other)==RISC_TIME_SAMPLE_BUSY&&!other);
  if(cancel){assert(worker.close(token)==1);assert(worker.step(token,&out)==RISC_TIME_SAMPLE_CLOSED);}else clockMs=50;
  {std::lock_guard<std::mutex> lock(dnsMutex);dnsResume=true;}dnsCv.notify_all();thread.join();dnsHook=nullptr;
  if(cancel)assert(worker.close(token)==0);else{assert(worker.step(token,&out)==RISC_TIME_SAMPLE_TIMEOUT);assert(worker.close(token)==1);worker.workerStep();assert(worker.close(token)==0);}
  assert(worker.idle()&&!udpLive);assert(worker.open(&request,&other)==0&&other!=token);assert(worker.step(token,&out)==RISC_TIME_SAMPLE_CLOSED);assert(worker.close(other)==1);worker.workerStep();assert(worker.close(other)==0);
 }
 reset();{N::configure([](){return owned;},[](){return online;});TimeSampleWorker worker(N::api(),HttpTimeSample::now);risc_time_sample_request_v1 r{sizeof(r),5000,"time.example.com"};uint64_t token=0;assert(worker.open(&r,&token)==0);worker.workerStep();worker.workerStep();online=false;worker.workerStep();auto out=snap();assert(worker.step(token,&out)==RISC_TIME_SAMPLE_NETWORK);assert(worker.close(token)==1);worker.workerStep();assert(worker.close(token)==0&&!udpLive);}
 reset();{N::configure([](){return owned;},[](){return online;});TimeSampleWorker worker(N::api(),HttpTimeSample::now);risc_time_sample_request_v1 r{sizeof(r),5000,"time.example.com"};uint64_t token=0;assert(worker.open(&r,&token)==0);worker.workerStep();worker.workerStep();udpCloseFail=true;assert(worker.close(token)==1);worker.workerStep();auto closes=udpCloses;assert(worker.close(token)==RISC_TIME_SAMPLE_RETAINED&&!worker.safe()&&!worker.idle());worker.workerStep();assert(udpCloses==closes);N::session=N::Session{};udpLive=false;}
 puts("Async sampler: copied mailbox, cancellation at all phases, blocked-DNS responsive owner, timeout, stale generations, Wi-Fi loss and retained cleanup PASS");
}
int main(){
 original_http_main();unsigned count=0;
 for(uint64_t utc:{HttpBounds::FirstUtc,UINT64_C(2085978495),UINT64_C(2085978496),UINT64_C(2085978497),HttpBounds::LastUtc}){
  reset();auto t=beginSample();sendRequest(t);datagrams.push_back(response(utc));clockMs=20;
  auto out=snap();assert(N::step(nullptr,t,&out)==0&&out.utc_seconds==utc&&out.observed_monotonic_ms==20&&out.flags==1&&out.resolution_ns==1000000000u&&!out.reserved&&!udpLive);
  assert(!N::idle()&&N::safe());auto again=snap();assert(N::step(nullptr,t,&again)==0&&!memcmp(&out,&again,sizeof(out)));
  assert(N::close(nullptr,t)==0&&N::close(nullptr,t)==0&&N::idle());++count;
 }
 for(unsigned phase=0;phase<3;++phase){reset();auto t=beginSample();for(unsigned i=0;i<phase;++i)assert(advance(t)==1);assert(N::close(nullptr,t)==0&&!udpLive);++count;}
 for(unsigned which=0;which<25;++which){
  reset();auto t=beginSample();sendRequest(t);auto reply=response();int expected=RISC_TIME_SAMPLE_PROTOCOL;
  switch(which){
   case 0:reply.bytes.resize(47);break;case 1:reply.bytes.resize(49);break;
   case 2:reply.bytes[0]=0x23;break;case 3:reply.bytes[0]=0x14;break;
   case 4:reply.bytes[0]=0x2c;break;case 5:reply.bytes[0]=0xe4;break;
   case 6:reply.bytes[1]=0;break;case 7:reply.bytes[1]=16;break;
   case 8:stamp(reply.bytes,32,0);break;case 9:stamp(reply.bytes,40,0);break;
   case 10:stamp(reply.bytes,32,1780000001);break;case 11:stamp(reply.bytes,40,1780000006);break;
   case 12:stamp(reply.bytes,40,1780000000,1);break;
   case 13:std::fill(reply.bytes.begin()+32,reply.bytes.begin()+40,0);break;
   case 14:reply.source.sin_port=htons(124);expected=1;break;
   case 15:reply.source.sin_addr.s_addr^=1;expected=1;break;
   case 16:reply.source.sin_family=AF_INET6;expected=1;break;
   case 17:reply.length=0;expected=1;break;
   case 18:reply.bytes[24]^=1;expected=1;break;
   case 19:receiveError=EIO;expected=RISC_TIME_SAMPLE_IO;break;
   case 20:clockMs=5000;expected=RISC_TIME_SAMPLE_TIMEOUT;break;
   case 21:udpDelay=5000;expected=RISC_TIME_SAMPLE_TIMEOUT;break;
   case 22:online=false;expected=RISC_TIME_SAMPLE_NETWORK;break;
   case 23:clockMs=10;assert(advance(t)==1);clockMs=9;expected=RISC_TIME_SAMPLE_TIMEOUT;break;
   case 24:owned=false;expected=RISC_TIME_SAMPLE_CONTEXT;break;
  }
  datagrams.push_back(reply);assert(advance(t)==expected);owned=true;assert(N::close(nullptr,t)==0&&!udpLive);++count;
 }
 for(unsigned which=0;which<5;++which){reset();auto t=beginSample();int expected=RISC_TIME_SAMPLE_IO;
  if(which==0){dnsDelay=5000;expected=RISC_TIME_SAMPLE_TIMEOUT;}if(which==1){dnsResult=EAI_NONAME;expected=RISC_TIME_SAMPLE_PROTOCOL;}if(which==2)socketFail=true;if(which==3)fcntlFail=true;
  if(which==4){assert(advance(t)==1);shortSend=true;}
  assert(advance(t)==expected);assert(N::close(nullptr,t)==0&&!udpLive);++count;
 }
 reset();{auto t=beginSample();sendRequest(t);auto wrong=response();wrong.bytes[24]^=1;for(unsigned i=0;i<8;++i)datagrams.push_back(wrong);for(unsigned i=0;i<7;++i)assert(advance(t)==1);assert(advance(t)==RISC_TIME_SAMPLE_PROTOCOL);assert(N::close(nullptr,t)==0);++count;}
 reset();{auto t=beginSample();sendRequest(t);udpCloseFail=true;datagrams.push_back(response());assert(advance(t)==RISC_TIME_SAMPLE_RETAINED);unsigned closed=udpCloses;assert(!N::safe()&&!N::idle());assert(N::close(nullptr,t)==RISC_TIME_SAMPLE_RETAINED&&advance(t)==RISC_TIME_SAMPLE_RETAINED&&udpCloses==closed);N::session=N::Session{};udpLive=false;++count;}
 reset();{auto t=beginSample();assert(advance(t)==1);udpCloseFail=true;assert(N::close(nullptr,t)==RISC_TIME_SAMPLE_RETAINED);auto closed=udpCloses;assert(N::close(nullptr,t)==RISC_TIME_SAMPLE_RETAINED&&udpCloses==closed);N::session=N::Session{};udpLive=false;++count;}
 for(const char* name:{"","a..b","https://time.example.com","a:123","-a","a-"}){reset();risc_time_sample_request_v1 r{sizeof(r),5000,name};uint64_t t=9;assert(N::open(nullptr,&r,&t)==RISC_TIME_SAMPLE_INVALID&&!t&&!dnsCalls);++count;}
 reset();{auto t=beginSample();auto out=snap();out.struct_size--;assert(N::step(nullptr,t,&out)==RISC_TIME_SAMPLE_INVALID);assert(N::close(nullptr,t+1)==RISC_TIME_SAMPLE_CLOSED);assert(N::close(nullptr,t)==0);++count;}
 for(uint32_t timeout:{0u,5001u}){reset();risc_time_sample_request_v1 r{sizeof(r),timeout,"time.example.com"};uint64_t t=9;assert(N::open(nullptr,&r,&t)==RISC_TIME_SAMPLE_INVALID&&!t&&!dnsCalls);++count;}
 reset();{risc_time_sample_request_v1 r{sizeof(r),5000,"time.example.com"};uint64_t t=9;owned=false;assert(N::open(nullptr,&r,&t)==RISC_TIME_SAMPLE_CONTEXT&&!t&&!dnsCalls);owned=true;online=false;assert(N::open(nullptr,&r,&t)==RISC_TIME_SAMPLE_NETWORK&&!t&&!dnsCalls);++count;}
 reset();{auto t=beginSample(1);clockMs=1;assert(advance(t)==RISC_TIME_SAMPLE_TIMEOUT&&!dnsCalls);assert(N::close(nullptr,t)==0);++count;}
 reset();{auto t=beginSample();risc_time_sample_request_v1 r{sizeof(r),5000,"other.example.com"};uint64_t other=9;assert(N::open(nullptr,&r,&other)==RISC_TIME_SAMPLE_BUSY&&!other);assert(N::close(nullptr,t)==0);assert(N::step(nullptr,t,nullptr)==RISC_TIME_SAMPLE_CLOSED);++count;}
 reset();{auto old=N::generation;N::generation=UINT64_MAX;risc_time_sample_request_v1 r{sizeof(r),5000,"time.example.com"};uint64_t t=9;assert(N::open(nullptr,&r,&t)==RISC_TIME_SAMPLE_CLOSED&&!t&&!dnsCalls);N::generation=old;++count;}
 workerTests();startupTests();
 std::cout<<"Native read-only time sample: "<<count<<" cases PASS; HTTP semantics preserved\n";
}
