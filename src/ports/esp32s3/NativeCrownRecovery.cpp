#include "NativeCrownRecovery.h"
#if defined(ESP_PLATFORM) && RISC_NATIVE_RECOVERY_AXP2101
#if !defined(RISC_RECOVERY_I2C_PORT) || !defined(RISC_RECOVERY_SDA) || !defined(RISC_RECOVERY_SCL)
#error "Recovery requires an explicit trusted native board selection"
#endif
#include "runtime/control/CrownRecovery.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_attr.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_private/system_internal.h>
#include <atomic>
#include <cstring>
extern "C" const uint32_t risc_native_crown_recovery_abi=1;
namespace RiscCpu { namespace NativeCrownRecovery { namespace {
constexpr uint8_t Port=RISC_RECOVERY_I2C_PORT, Address=0x34, Status=0x49;
constexpr uint32_t Magic=0x43524e35;
static_assert(Port<2 && RISC_RECOVERY_SDA<49 && RISC_RECOVERY_SCL<49,"invalid recovery bus");
RTC_NOINIT_ATTR uint32_t resetMarker[3];
SemaphoreHandle_t mutex=nullptr;
TaskHandle_t task=nullptr;
Transfer raw=nullptr;
bool online=false;
std::atomic<bool> paused{false};
std::atomic<bool> wakeBoundary{false};
RiscControl::CrownEvents events;
RiscControl::CrownExit hostExit;
std::atomic<uint32_t> requestEpoch{0},requestSince{0};
std::atomic<uint32_t> shortSerial{0};
uint32_t now(){return uint32_t(uint64_t(esp_timer_get_time())/1000);}
bool lock(uint32_t ms){return mutex && xSemaphoreTake(mutex,pdMS_TO_TICKS(ms)+1)==pdTRUE;}
void unlock(){xSemaphoreGive(mutex);}
void emergency(uint32_t reason=1){
  resetMarker[0]=Magic;resetMarker[1]=~Magic;resetMarker[2]=reason;
  // Do not invoke app/provider cleanup, shutdown hooks, logging, filesystem
  // or locks on this explicit emergency path. IDF stalls the other core.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  esp_restart_noos();
}
bool sample(){
  uint8_t value=0;
  bool ok=online && raw(Port,Address,&Status,1,&value,1,20);
  const uint8_t keys=value&0x0f;
  if(ok && keys){const uint8_t ack[]={Status,keys};ok=raw(Port,Address,ack,2,nullptr,0,20);}
  if(!ok){events.discontinuity();return false;}
  if(wakeBoundary.exchange(false,std::memory_order_acq_rel))events.waking();
  const auto serial=events.shortSequence();
  const bool reboot=events.observe(keys,now());
  if(events.shortSequence()!=serial)hostExit.press(now());
  if(hostExit.pending() && !(requestEpoch.load(std::memory_order_relaxed)&1u)){
    requestSince.store(now(),std::memory_order_relaxed);
    requestEpoch.fetch_or(1u,std::memory_order_release);
  }
  shortSerial.store(events.shortSequence(),std::memory_order_release);
  if(reboot)emergency();
  return true;
}
void supervise(void*){
  // SDK task watchdog is initialized with panic enabled by the selected
  // ESP32-S3 SDK. Do not silently run an unmonitored recovery task.
  if(esp_task_wdt_add(nullptr)!=ESP_OK)esp_restart_noos();
  for(;;){
    if(lock(25)){if(online && !paused)(void)sample();unlock();}
    // Once requested, nonresponse recovery must not depend on the I2C mutex.
    // CAS races clean completion against timeout for this exact invocation.
    uint32_t epoch=requestEpoch.load(std::memory_order_acquire);
    if((epoch&1u) && uint32_t(now()-requestSince.load(std::memory_order_relaxed))>=RiscControl::CrownExit::CleanupMs &&
       requestEpoch.compare_exchange_strong(epoch,epoch&~1u,std::memory_order_acq_rel))emergency(2);
    // High priority, independent task on core0. No app, GUI, grant, yield,
    // owner-task heartbeat or provider callback is needed to reach sample().
    if(esp_task_wdt_reset()!=ESP_OK)esp_restart_noos();
    vTaskDelay(pdMS_TO_TICKS(10)+1);
  }
}
}
bool opened(uint8_t port,uint8_t sda,uint8_t scl,Transfer transfer){
  if(port!=Port)return true;
  if(sda!=RISC_RECOVERY_SDA || scl!=RISC_RECOVERY_SCL || !transfer)return false;
  if(!mutex)mutex=xSemaphoreCreateMutex();
  if(!lock(25))return false;
  raw=transfer;online=true;events.discontinuity();
  // Confirm chip identity before treating any status as key evidence.
  const uint8_t reg=3;uint8_t id=0;
  bool ok=raw(Port,Address,&reg,1,&id,1,20) && id==0x4a;
  // Native recovery owns these four interrupt sources. Falling-edge wake
  // brings the scheduler back before the hold threshold; preserve all other
  // PMU interrupt/charge/power policy. Wake release is not a second action.
  uint8_t enable=0;const uint8_t enableReg=0x41;
  if(ok)ok=raw(Port,Address,&enableReg,1,&enable,1,20);
  const uint8_t enableWrite[]={enableReg,uint8_t(enable|0x0f)};
  if(ok)ok=raw(Port,Address,enableWrite,2,nullptr,0,20);
  if(ok && !task)ok=xTaskCreatePinnedToCore(supervise,"core-crown",3072,nullptr,
                                          configMAX_PRIORITIES-2,&task,0)==pdPASS;
  if(!ok)online=false;
  unlock();return ok;
}
bool closing(uint8_t port){
  if(port!=Port)return true;
  if(!lock(25))return false;
  online=false;events.discontinuity();unlock();return true;
}
bool pause(){
  if(!mutex)return true;
  if(!lock(25))return false;
  paused=true;events.discontinuity();unlock();return true;
}
void resume(bool gpioWake){
  // pause() already drained the sampler and broke hold continuity. Resume
  // cannot silently remain disabled because of transient mutex contention.
  wakeBoundary.store(gpioWake,std::memory_order_release);
  paused.store(false,std::memory_order_release);
}
bool transact(uint8_t port,uint8_t address,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t ms,Transfer transfer){
  if(port!=Port)return transfer(port,address,tx,tn,rx,rn,ms);
  if(!lock(ms))return false;
  bool ok=true;
  if(address==Address){
    ok=sample();
    if(ok && tn>1 && !rn){
      uint8_t write[257];
      if(tn>sizeof(write) || size_t(tx[0])+tn-1>256){unlock();return false;}
      std::memcpy(write,tx,tn);uint8_t acknowledge=0;
      for(size_t i=1;i<tn;++i){
        const unsigned reg=unsigned(tx[0])+i-1;
        if(reg==Status){acknowledge=write[i];write[i]&=0xf0;}
        if(reg==0x41)write[i]|=0x0f;
      }
      ok=transfer(port,address,write,tn,nullptr,0,ms);
      if(ok)events.acknowledge(acknowledge);
    }else if(ok){
      ok=transfer(port,address,tx,tn,rx,rn,ms);
      if(ok && tn==1 && rx && tx[0]<=Status && size_t(Status-tx[0])<rn)
        rx[Status-tx[0]]|=events.legacy();
    }
  }else ok=transfer(port,address,tx,tn,rx,rn,ms);
  unlock();return ok;
}
uint32_t shortSequence(){return shortSerial.load(std::memory_order_acquire);}
uint32_t previousReason(){
  const bool found=esp_reset_reason()==ESP_RST_SW && resetMarker[0]==Magic && resetMarker[1]==~Magic;
  const uint32_t reason=found?resetMarker[2]:0;
  resetMarker[0]=resetMarker[1]=resetMarker[2]=0;return reason;
}
bool previousEmergency(){return previousReason()!=0;}
void beginInvocation(bool child){
  if(mutex&&!lock(25))emergency(3);
  hostExit.begin(child);requestEpoch.store((requestEpoch.load(std::memory_order_relaxed)+2u)&~1u,std::memory_order_release);
  if(mutex)unlock();
}
void endInvocation(){beginInvocation(false);}
bool exitRequested(){return (requestEpoch.load(std::memory_order_acquire)&1u)!=0;}
}}
#endif
