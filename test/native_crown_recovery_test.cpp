#include "ports/esp32s3/NativeCrownRecovery.h"
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_system.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
namespace {
std::timed_mutex busLock;
std::atomic<uint32_t> ms{0},reboots{0};
std::atomic<bool> done{false};
void(*worker)(void*)=nullptr;
bool failTask=false,wrongId=false,busError=false,released=false;
uint32_t releaseAt=0;
uint8_t status=0,irqMask=0x80;
unsigned watchdogFeeds=0;bool watchdogRegistered=false;
esp_reset_reason_t resetReason=ESP_RST_POWERON;
struct Stop{};
struct Reboot{};
bool raw(uint8_t port,uint8_t address,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t){
  assert(port==0 && address==0x34 && tx && tn);
  if(busError)return false;
  if(releaseAt && ms>=releaseAt && !released){status|=1;released=true;}
  if(tn==1 && rx){
    for(size_t i=0;i<rn;++i)rx[i]=tx[0]+i==3?(wrongId?0:0x4a):tx[0]+i==0x49?status:tx[0]+i==0x41?irqMask:0x80;
  }else if(tn>1 && !rn){for(size_t i=1;i<tn;++i){if(tx[0]+i-1==0x49)status&=uint8_t(~tx[i]);if(tx[0]+i-1==0x41)irqMask=tx[i];}}
  else assert(false);
  return true;
}
}
SemaphoreHandle_t xSemaphoreCreateMutex(){return &busLock;}
int xSemaphoreTake(SemaphoreHandle_t p,TickType_t){return static_cast<std::timed_mutex*>(p)->try_lock()?pdTRUE:0;}
void xSemaphoreGive(SemaphoreHandle_t p){static_cast<std::timed_mutex*>(p)->unlock();}
int xTaskCreatePinnedToCore(void(*entry)(void*),const char* name,uint32_t stack,void*,unsigned priority,TaskHandle_t* handle,int core){
  assert(!strcmp(name,"core-crown") && stack>=3072 && priority==configMAX_PRIORITIES-2 && core==0);
  if(failTask)return 0;worker=entry;*handle=reinterpret_cast<void*>(1);return pdPASS;
}
void vTaskDelay(TickType_t ticks){ms+=ticks;if(ms>11000)throw Stop{};std::this_thread::yield();}
int esp_task_wdt_add(void*){watchdogRegistered=true;return 0;}
int esp_task_wdt_reset(){assert(watchdogRegistered);++watchdogFeeds;return 0;}
int64_t esp_timer_get_time(){return int64_t(ms.load())*1000;}
esp_reset_reason_t esp_reset_reason(){return resetReason;}
[[noreturn]] void esp_restart_noos(){++reboots;throw Reboot{};}
int main(int argc,char**argv){
  assert(argc==2);using namespace RiscCpu::NativeCrownRecovery;
  const bool exitTimeout=!strcmp(argv[1],"exit-timeout"),homeShort=!strcmp(argv[1],"home-short"),cleanExit=!strcmp(argv[1],"clean-exit");
  const bool release=!strcmp(argv[1],"release"),failure=!strcmp(argv[1],"bus-error"),sleep=!strcmp(argv[1],"sleep");
  failTask=!strcmp(argv[1],"task-failure");wrongId=!strcmp(argv[1],"wrong-chip");
  assert(!previousEmergency());assert(!opened(0,1,2,raw));
  const bool openedOk=opened(0,10,11,raw);
  if(failTask||wrongId){assert(!openedOk);puts("Native crown start refuses missing task/wrong chip PASS");return 0;}
  assert(openedOk && worker && irqMask==0x8f);
  uint8_t mask[]={0x41,8};assert(transact(0,0x34,mask,2,nullptr,0,40,raw)&&irqMask==15);
  // Native polling/ACK and the legacy PMU client see independent histories.
  status=8;uint8_t reg=0x49,value=0;
  assert(transact(0,0x34,&reg,1,&value,1,40,raw));assert(value==8&&status==0&&shortSequence()==1);
  uint8_t ack[]={0x49,15};assert(transact(0,0x34,ack,2,nullptr,0,40,raw));
  assert(shortSequence()==1);assert(transact(0,0x34,&reg,1,&value,1,40,raw)&&value==0);
  beginInvocation(exitTimeout||cleanExit);
  status=(exitTimeout||homeShort||cleanExit)?8:!strcmp(argv[1],"held-at-boot")?4:2;
  if(cleanExit){assert(transact(0,0x34,&reg,1,&value,1,40,raw));assert(exitRequested());
    assert(transact(0,0x34,ack,2,nullptr,0,40,raw)&&exitRequested());
    endInvocation();assert(!exitRequested());}
  if(release)releaseAt=4990;
  busError=failure;
  if(sleep)assert(pause());
  std::thread supervisor([&]{try{worker(nullptr);}catch(const Reboot&){if(!exitTimeout)busLock.unlock();}catch(const Stop&){}done=true;});
  // This foreground app never yields or calls any runtime/key API.
  uint64_t busyWork=0;while(!done.load(std::memory_order_acquire))++busyWork;
  supervisor.join();assert(busyWork>0 && watchdogRegistered && watchdogFeeds>0);
  if(release||failure||sleep||homeShort||cleanExit){assert(!reboots);assert(!previousEmergency());}
  else {const unsigned threshold=exitTimeout?3000:5000;assert(reboots==1&&ms>=threshold&&ms<threshold+30);resetReason=ESP_RST_SW;assert(previousReason()==(exitTimeout?2u:1u));assert(!previousEmergency());}
  if(sleep){status=0;resume();assert(transact(0,0x34,&reg,1,&value,1,40,raw)&&!value);assert(!reboots);}
  assert(closing(0));puts("Native crown task: non-yielding foreground, legacy ACK isolation, threshold/release/fault, sleep and reset diagnostics PASS");
}
