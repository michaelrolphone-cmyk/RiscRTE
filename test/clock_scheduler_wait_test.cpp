#include <thread>
#include <cassert>
#include <cstring>
#include <cstdio>
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include "ports/esp32s3/CooperativeDelay.h"
using namespace RiscCpu;
static const auto ownerThread=std::this_thread::get_id();
static bool owned=true,inIsr=false;
static unsigned waits=0,legacySleeps=0,diagnostics=0,io=0,providerPolls=0,storage=0,radio=0,usb=0;
static uint32_t lastTicks=0,lastLegacyMs=0;
static uint64_t now=73;
static bool owner(){return owned && std::this_thread::get_id()==ownerThread;}
static bool xPortInIsrContext(){return inIsr;}
static void vTaskDelay(uint32_t ticks){assert(ticks);++waits;lastTicks=ticks;}
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 1000
#endif
namespace RiscDiagnostics {void poll(){++diagnostics;}}
namespace RiscCpu { namespace {
static bool (*ownerTask)()=owner;
#include "ports/esp32s3/NativeSchedulerWait.inc"
}}
static Hardware hardware(bool suffix){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return now;};h.sleep=[](uint32_t ms){++legacySleeps;lastLegacyMs=ms;RiscDiagnostics::poll();};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++io;return true;};h.gpioWrite=[](uint8_t,bool){++io;return true;};
 h.gpioRead=[](uint8_t,bool*){++io;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++io;return true;};h.gpioClose=[](uint8_t){++io;return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++io;return true;};h.i2cClose=[](uint8_t){++io;return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++io;return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++io;return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++io;return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++io;return true;};h.spiClose=[](uint8_t){++io;return true;};
 h.hciIdle=[](){++radio;return false;};h.hciSafe=[](){++radio;return false;};h.radioIdle=[](){++radio;return false;};
 h.usbPhyIdle=[](){++usb;return true;};h.usbPhySuspend=[](){++usb;return true;};h.usbPhyResume=[](){++usb;return true;};
 if(suffix)configureSchedulerWait(h);
 return h;
}
static const risc_platform_clock_api_v1* clock(RiscBoot::Runtime& runtime){
 for(size_t i=0;i<runtime.platformCount_;++i){const auto& entry=runtime.platforms_[i];
  if(!strcmp(entry.capability,"platform.clock")){assert(entry.api==1 && entry.scope==RiscBoot::Runtime::Scope::Global && !entry.id);return static_cast<const risc_platform_clock_api_v1*>(entry.table);}
 }
 assert(false);return nullptr;
}
extern "C" bool clock_legacy_consumer(const risc_platform_clock_api_v1*);
extern "C" bool clock_wait_consumer(const risc_platform_clock_api_v1*,uint32_t);
static void untouched(){assert(!io && !providerPolls && !storage && !radio && !usb);}
int main(){
 static_assert(offsetof(risc_platform_clock_wait_v1,base)==0,"exact base prefix");
 static_assert(offsetof(risc_platform_clock_wait_v1,wait_tag)==sizeof(risc_platform_clock_api_v1),"append-only suffix");
 static_assert(offsetof(Hardware,schedulerWait)==offsetof(Hardware,entropy)+sizeof(Hardware::entropy),"Hardware callback is appended");
 for(bool selected:{false,true}){
  auto hw=hardware(selected);Port p(hw);
  RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){++providerPolls;},[](const char*){++diagnostics;return true;},nullptr});
  assert(p.bind(runtime));const auto* base=clock(runtime);assert(base && base->context==&p);
  assert(base->api_version==1 && base->monotonic_ms(base->context)==73);
  assert(clock_legacy_consumer(base));assert(lastLegacyMs==5000);untouched();
  const auto* suffix=risc_platform_clock_wait_from_v1(base);
  if(!selected){assert(base->struct_size==sizeof(*base) && !suffix && !p.clock_.scheduler_wait_ms && !clock_wait_consumer(base,1));continue;}
  assert(base->struct_size==sizeof(*suffix) && suffix && hw.schedulerWait==schedulerWait);
  const auto oldLegacy=legacySleeps,oldDiagnostics=diagnostics;
  for(uint32_t ms:{1u,2u,11u,49u,50u}){
   const auto oldWaits=waits;assert(clock_wait_consumer(base,ms));
   assert(waits==oldWaits+1 && lastTicks==cooperativeDelayTicks(ms,configTICK_RATE_HZ));
  }
  const auto accepted=waits;
  for(uint32_t ms:{0u,51u,UINT32_MAX}){assert(!suffix->scheduler_wait_ms(base->context,ms));assert(!hw.schedulerWait(ms));}
  owned=false;assert(!suffix->scheduler_wait_ms(base->context,1) && !hw.schedulerWait(1));assert(!base->monotonic_ms(base->context));base->sleep_ms(base->context,1);owned=true;
  std::thread stranger([&](){assert(!suffix->scheduler_wait_ms(base->context,1) && !hw.schedulerWait(1));});stranger.join();
  inIsr=true;assert(!suffix->scheduler_wait_ms(base->context,1) && !hw.schedulerWait(1));inIsr=false;
  ownerTask=nullptr;assert(!suffix->scheduler_wait_ms(base->context,1) && !hw.schedulerWait(1));ownerTask=owner;
  assert(waits==accepted && legacySleeps==oldLegacy && diagnostics==oldDiagnostics);untouched();
  // Cleanup cooperation does not require or restore mutation permission.
  p.poisoned_=true;p.hci_.token=91;p.hci_.closing=true;p.radios_[0].operation=17;p.radios_[0].closing=true;p.transferring_=true;
  assert(suffix->scheduler_wait_ms(base->context,1));
  assert(p.poisoned_ && p.hci_.token==91 && p.hci_.closing && p.radios_[0].operation==17 && p.radios_[0].closing && p.transferring_);
  assert(legacySleeps==oldLegacy && diagnostics==oldDiagnostics);untouched();
 }
 // A real prefix-only object, including at the end of an ASan allocation, is
 // rejected before reading suffix fields. Tagged but malformed suffixes reject.
 auto* prefix=new risc_platform_clock_api_v1{1,sizeof(risc_platform_clock_api_v1),nullptr,nullptr,nullptr};
 assert(!risc_platform_clock_wait_from_v1(prefix));delete prefix;
 assert(!risc_platform_clock_wait_from_v1(nullptr));
 risc_platform_clock_wait_v1 malformed{{1,sizeof(malformed),nullptr,nullptr,nullptr},RISC_PLATFORM_CLOCK_WAIT_TAG_V1,1,[](void*,uint32_t){return true;}};
 assert(risc_platform_clock_wait_from_v1(&malformed.base));
 malformed.base.api_version=2;assert(!risc_platform_clock_wait_from_v1(&malformed.base));malformed.base.api_version=1;
 malformed.base.struct_size--;assert(!risc_platform_clock_wait_from_v1(&malformed.base));malformed.base.struct_size++;
 malformed.wait_tag^=1;assert(!risc_platform_clock_wait_from_v1(&malformed.base));malformed.wait_tag^=1;
 malformed.wait_version=2;assert(!risc_platform_clock_wait_from_v1(&malformed.base));malformed.wait_version=1;
 malformed.scheduler_wait_ms=nullptr;assert(!risc_platform_clock_wait_from_v1(&malformed.base));
 printf("Actual CpuPort clock binding + native scheduler-only wait: legacy, suffix, owner/ISR, bounds, retained custody and no side effects PASS (tick rate %u)\n",configTICK_RATE_HZ);
}
