#include <memory>
#include <sstream>
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "retention_stage_capture.h"
static bool owned=true,hciSafe=true,httpSafe=true,httpIdle=true,usbIdle=true,maintenanceIdle=true;
static unsigned hciChecks=0,nativeChecks=0,reasonReads=0;
static RiscCpu::Port* cpu;
static bool owner(){return owned;}
static bool appExit(){++nativeChecks;return cpu->appExitSafe();}
static bool storageSafe(){++nativeChecks;return cpu->providerStorageSafe();}
static const char* reason(){++reasonReads;return cpu->retentionReason();}
static void expect(const char* text){
#if RISC_STAGE_LOGS
 assert(count(text)==1);
#else
 (void)text;assert(lines.empty());
#endif
}
static RiscCpu::Hardware hardware(){
 RiscCpu::Hardware h{};h.owner=owner;
 h.hciSafe=[](){++hciChecks;return hciSafe;};h.httpSafe=[](){return httpSafe;};
 h.httpIdle=[](){return httpIdle;};h.usbPhyIdle=[](){return usbIdle;};h.maintenanceIdle=[](){return maintenanceIdle;};return h;
}
static void cpuPredicates(){
 using P=RiscCpu::Port;
 struct Case {const char* label;bool storage;std::function<void(P&)> arm;};
 const Case cases[]={
  {"storage-work-fault",true,[](P& p){p.storageWork_.gpio=&p.gpios_[0];p.storageWork_.fault=true;}},
  {"sdmmc-closing",true,[](P& p){p.sdmmc_.closing=true;}},
  {"tcp-custody",true,[](P& p){static const RiscBoot::TcpListenerBackend b{};p.hw_.tcpListener=&b;}},
  {"entropy-custody",true,[](P& p){static const RiscBoot::EntropyBackend b{};p.hw_.entropy=&b;}},
  {"radio-custody",true,[](P& p){static risc_native_radio_async_v1 b{};b.custodySafe=[](){return false;};p.hw_.radioAsync=&b;}},
  {"owner",true,[](P&){owned=false;}},
  {"poisoned",true,[](P& p){p.poisoned_=true;}},
  {"sleeping",true,[](P& p){p.sleeping_=true;}},
  {"sleep-retained",true,[](P& p){p.sleepRetained_=true;}},
  {"transferring",true,[](P& p){p.transferring_=true;}},
  {"iq-closing",true,[](P& p){p.iq_.closing=true;}},
  {"iq-unparked",true,[](P& p){p.iq_.token=1;}},
  {"sync-held",true,[](P& p){p.syncCount_=1;p.syncs_[0].locks[0].held=true;}},
  {"http-custody",true,[](P&){httpSafe=false;}},
  {"gpio-held",true,[](P& p){p.pins_[3].held=true;}},
  {"i2s-closing",true,[](P& p){p.i2ss_[0].closing=true;}},
  {"radio-closing",true,[](P& p){p.radios_[0].closing=true;}},
  {"hci-closing",true,[](P& p){p.hci_.closing=true;}},
  {"hci-custody",true,[](P&){hciSafe=false;}},
  {"storage-work-busy",false,[](P& p){
    static RiscStorage::Worker worker;static const RiscCpu::StorageWorkBackend b{&worker,nullptr,nullptr,nullptr};
    assert(worker.bind([](void*,const void*,uint32_t,void*,uint32_t,uint32_t*){return int32_t(RISC_STORAGE_WORK_OK);},nullptr)==RISC_STORAGE_WORK_OK);
    uint64_t token;const uint8_t request=1;assert(worker.submit(&request,1,&token)==RISC_STORAGE_WORK_OK);p.hw_.storageWork=&b;
  }},
  {"iq-owned",false,[](P& p){p.iq_.token=1;p.iq_.worker=true;}},
  {"usb-owned",false,[](P& p){p.usb_.token=1;}},
  {"usb-closing",false,[](P& p){p.usb_.closing=true;}},
  {"usb-active",false,[](P&){usbIdle=false;}},
  {"gpio-wake-armed",false,[](P& p){p.pins_[3].wakeModes=1;}},
  {"tcp-active",false,[](P& p){static RiscBoot::TcpListenerBackend b{};b.safe=[](void*){return true;};p.hw_.tcpListener=&b;}},
  {"entropy-active",false,[](P& p){static RiscBoot::EntropyBackend b{};b.safe=[](void*){return true;};p.hw_.entropy=&b;}},
  {"http-active",false,[](P&){httpIdle=false;}},
  {"i2s-owned",false,[](P& p){p.i2ss_[0].token=1;}},
  {"radio-active",false,[](P& p){p.radios_[0].active=true;}},
  {"radio-service-lease",false,[](P& p){p.radios_[0].serviceLease=1;}},
  {"maintenance-active",false,[](P&){maintenanceIdle=false;}}
 };
 for(const auto& c:cases){
  owned=hciSafe=httpSafe=httpIdle=usbIdle=maintenanceIdle=true;P p(hardware());c.arm(p);
  const auto before=lines.size();
  for(unsigned i=0;i<1000;++i){
   assert(p.providerStorageSafe()!=c.storage);assert(!p.appExitSafe());
#if RISC_STAGE_LOGS
   assert(p.retentionReason() && !strcmp(p.retentionReason(),c.label));
#else
   assert(!p.retentionReason());
#endif
  }
  assert(lines.size()==before); // Safety polling itself never emits.
 }
 owned=hciSafe=httpSafe=httpIdle=usbIdle=maintenanceIdle=true;
 P p(hardware());p.sdmmc_.closing=true;p.hci_.closing=true;hciChecks=0;
 assert(!p.appExitSafe() && !hciChecks);
#if RISC_STAGE_LOGS
 assert(!strcmp(p.retentionReason(),"sdmmc-closing"));
#endif
 p.sdmmc_.closing=p.hci_.closing=false;p.hci_.token=1;
 assert(p.appExitSafe()); // Healthy owned HCI is still allowed across navigation.
#if RISC_STAGE_LOGS
 assert(!p.retentionReason());
#endif
}
static void hciFailureStages(){
 auto h=hardware();static bool closeOkay=false;static bool idle=false;
 h.hciSend=[](uint8_t,const uint8_t*,size_t,uint32_t){return false;};
 h.hciClose=[](){idle=closeOkay;return closeOkay;};h.hciIdle=[](){return idle;};
 RiscCpu::Port p(h);auto& c=p.hci_;c.port=&p;c.token=1;
 const uint8_t command[]={3,12,0};uint8_t state=0;
 assert(!RiscCpu::Port::hciSend(&c,1,1,command,sizeof(command),0));
 expect("native hci failure step=send");
 const auto emitted=lines.size();
 for(unsigned i=0;i<1000;++i){
  assert(!RiscCpu::Port::hciSend(&c,1,1,command,sizeof(command),0));
  assert(RiscCpu::Port::hciStatus(&c,1,&state) && state==RISC_HCI_CONTROLLER_RETAINED);
  assert(!RiscCpu::Port::hciClose(&c,1));
 }
 assert(lines.size()==emitted); // Retries never replace or flood the first stage.
 closeOkay=true;assert(RiscCpu::Port::hciClose(&c,1));c.token=2;idle=false;
 assert(!RiscCpu::Port::hciSend(&c,2,1,command,sizeof(command),0));
#if RISC_STAGE_LOGS
 assert(count("native hci failure step=send")==2); // Only successful close rearms it.
#endif
 assert(RiscCpu::Port::hciClose(&c,2));
}
static RiscBoot::Runtime* runtime(){
 RiscBoot::Port p{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};
 p.appExitSafe=appExit;p.providerStorageSafe=storageSafe;p.nativeRetentionReason=reason;
 return new RiscBoot::Runtime(p);
}
static void barriers(){
 cpu=new RiscCpu::Port(hardware());auto* rt=runtime();
 assert(rt->appExitBarrier() && rt->appExitBarrier(false));
 cpu->radios_[0].active=true;
 assert(rt->appExitBarrier(false)); // Provider-owned healthy activity does not block acquire.
 assert(!rt->appExitBarrier());
 expect("app retention barrier mode=exit predicate=native-app-exit detail=radio-active");
 const unsigned checks=nativeChecks,reads=reasonReads;const size_t emitted=lines.size();
 const std::string firstError=rt->error();
 for(unsigned i=0;i<1000;++i){assert(!rt->appExitBarrier());assert(!rt->appExitBarrier(false));}
 assert(nativeChecks==checks && reasonReads==reads && lines.size()==emitted && firstError==rt->error());
 assert(rt->retained() && !rt->mainInvocation_.active_ && !rt->mainInvocation_.queued_[0]);
 cpu=new RiscCpu::Port(hardware());rt=runtime();cpu->hci_.closing=true;
 assert(!rt->appExitBarrier(false));
 expect("app retention barrier mode=acquire predicate=provider-storage detail=hci-closing");
 rt=runtime();rt->port_.nativeRetentionReason=nullptr;
 assert(!rt->appExitBarrier(false));
 expect("app retention barrier mode=acquire predicate=provider-storage detail=unavailable");
 rt=runtime();rt->entropyRetained_=true;nativeChecks=0;
 assert(!rt->appExitBarrier(false) && !nativeChecks);
 expect("app retention barrier mode=acquire predicate=provider-storage detail=entropy-retained");
 rt=runtime();rt->metadataCloseRetained_=true;nativeChecks=0;
 assert(!rt->appExitBarrier() && !nativeChecks);
 expect("app retention barrier mode=exit predicate=metadata-close detail=unavailable");
 rt=runtime();rt->retained_=true;rt->fail("specific earlier failure");nativeChecks=0;
 assert(!rt->appExitBarrier() && !nativeChecks && !strcmp(rt->error(),"specific earlier failure"));
 expect("app retention barrier mode=exit predicate=invocation-retained detail=unavailable");
#if RISC_STAGE_LOGS
 assert(count("app retention barrier")==6);
#else
 assert(!reasonReads && lines.empty());
#endif
}
int main(){cpuPredicates();hciFailureStages();barriers();puts("Native retention diagnostics: production predicates, short circuiting, acquire/exit distinction, first failure preservation and 1000-poll silence PASS");fflush(stdout);std::_Exit(0);}
