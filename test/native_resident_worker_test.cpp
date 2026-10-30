#include "ports/esp32s3/NativeResidentWorker.h"
#include <cassert>
#include <cstring>
#include <cstdio>
struct Deleted{};
static bool owned=true,inISR=false,failAllocation=false,failTask=false,early=false;
static bool stalled=false,rollback=false;static unsigned timerCalls=0,delays=0;
static unsigned allocated=0,freed=0,deleted=0,entered=0;static uint64_t clockMs=0;
static void(*scheduled)(void*)=nullptr;static void*scheduledArg=nullptr;
bool xPortInIsrContext(){return inISR;}
void vTaskDelay(TickType_t t){++delays;if(!stalled)clockMs+=t;}
void vTaskDelete(void*p){assert(!p);++deleted;throw Deleted{};}
int64_t esp_timer_get_time(){return int64_t((clockMs-(rollback&&timerCalls++?1:0))*1000);}
void* heap_caps_malloc(size_t n,unsigned caps){assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(failAllocation)return nullptr;++allocated;return std::malloc(n);}
void heap_caps_free(void*p){assert(p);++freed;std::free(p);}
static void dispatch(){auto fn=scheduled;auto arg=scheduledArg;scheduled=nullptr;scheduledArg=nullptr;try{fn(arg);assert(false);}catch(const Deleted&) {}}
int xTaskCreatePinnedToCore(void(*fn)(void*),const char*name,uint32_t stack,void*arg,uint32_t priority,void**out,int core){
 assert(!strcmp(name,"rte-worker")&&stack==4096&&priority==1&&!out&&core==-1);
 if(failTask)return 0;
 scheduled=fn;scheduledArg=arg;if(early)dispatch();return pdPASS;
}
static void callback(void*,const risc_worker_control_v1*c){assert(allocated==freed);++entered;c->yield_ms(c->context,0);}
int main(){
 using namespace RiscCpu::NativeResidentWorker;configure([](){return owned;});uint64_t t=9;
 inISR=true;assert(authority.start(1,callback,nullptr,4096,1,-1,&t)==RISC_WORKER_CONTEXT&&!t);inISR=false;
 failAllocation=true;assert(authority.start(1,callback,nullptr,4096,1,-1,&t)==RISC_WORKER_ALLOCATION&&!t&&!scheduled&&!entered);failAllocation=false;
 failTask=true;assert(authority.start(1,callback,nullptr,4096,1,-1,&t)==RISC_WORKER_ALLOCATION&&!t&&!scheduled&&!entered&&allocated==freed);failTask=false;
 early=true;assert(authority.start(1,callback,nullptr,4096,1,-1,&t)==0&&entered==1&&deleted==1&&!authority.idle());
 assert(authority.release(1,t)==RISC_WORKER_BUSY&&authority.join(1,t,0)==0&&authority.release(1,t)==0&&authority.idle());
 early=false;assert(authority.start(1,callback,nullptr,4096,1,-1,&t)==0&&!authority.idle());assert(authority.join(1,t,2)==RISC_WORKER_TIMEOUT);
 stalled=true;delays=0;assert(authority.join(1,t,2)==RISC_WORKER_TIMEOUT&&delays==2000);stalled=false;
 clockMs=100;rollback=true;timerCalls=0;assert(authority.join(1,t,2)==RISC_WORKER_TIMEOUT);rollback=false;
 assert(authority.cancel(1,t)==0);dispatch();assert(authority.join(1,t,0)==0&&authority.release(1,t)==0);
 assert(entered==2&&deleted==2&&allocated==freed&&authority.idle());puts("Actual native trampoline: ISR/allocator/create failure, internal envelope lifetime, early completion, join and resident self-delete PASS");
}
