#include "native_radio_async_sdk_fixture.inc"
#include "ports/esp32s3/NativeRadioAsyncTask.h"
namespace Async=RiscCpu::NativeRadioAsync;
static unsigned taskCreates=0;
BaseType_t xTaskCreate(void(*entry)(void*),const char* name,uint32_t bytes,void* argument,unsigned priority,TaskHandle_t* out){
 ++taskCreates;assert(entry==Async::task && !strcmp(name,"native-wifi") && bytes==8192 && !argument && priority==1);
 *out=reinterpret_cast<void*>(uintptr_t(1));return pdPASS;
}
void vTaskDelay(TickType_t){assert(false);}
namespace RiscDiagnostics {uint64_t monotonicUs(){return uint64_t(now.load());}}
int main(){using namespace RiscCpu;
 {
  struct Span{const void* pointer;size_t size;};
#define ATOMIC_SPAN(value) Span{&(value),sizeof(value)}
  const Span spans[]={ATOMIC_SPAN(NativeRadio::cancelRequested),ATOMIC_SPAN(NativeRadio::generation),
   ATOMIC_SPAN(NativeRadio::eventOperation),ATOMIC_SPAN(NativeRadio::scanEvent),ATOMIC_SPAN(NativeRadio::joinFailed),
   ATOMIC_SPAN(Async::mailbox),ATOMIC_SPAN(Async::activeId),ATOMIC_SPAN(Async::finishedId),ATOMIC_SPAN(Async::phase),
   ATOMIC_SPAN(Async::sharedLease),ATOMIC_SPAN(Async::restoring),
#if RISC_STAGE_LOGS
   ATOMIC_SPAN(NativeRadio::disconnectReason),ATOMIC_SPAN(NativeRadio::scanStatus),ATOMIC_SPAN(Async::recordWrite),
   ATOMIC_SPAN(Async::recordRead),ATOMIC_SPAN(Async::lost),ATOMIC_SPAN(Async::latestSeq),ATOMIC_SPAN(Async::latestOperation),
   ATOMIC_SPAN(Async::latestStage),ATOMIC_SPAN(Async::latestResult),ATOMIC_SPAN(Async::latestBegin),
   ATOMIC_SPAN(Async::latestLow),ATOMIC_SPAN(Async::latestHigh),
#endif
  };
#undef ATOMIC_SPAN
  
  for(const auto& span:spans)for(size_t offset:{size_t(0),span.size-1}){
   NativeRadio::generation.store(1);NativeRadio::eventOperation.store(NativeRadio::Join);NativeRadio::joinFailed.store(0);
   nativeAtomicRejectedAddress=static_cast<const uint8_t*>(span.pointer)+offset;
   assert(!Async::internalAtomicsReady()&&!Async::provision()&&!Async::api());
   assert(!Async::allocateBuffers()&&!Async::provisioned());
   assert(!taskCreates&&!workspaceAllocations&&!Async::buffers&&!Async::workerTask);
   risc_radio_request_v1 request{};request.struct_size=sizeof(request);request.kind=RISC_RADIO_REQUEST_SCAN;uint32_t id=9;
   risc_radio_progress_v1 progress{};progress.struct_size=sizeof(progress);
   assert(Async::begin(&request,&id)==RISC_RADIO_UNAVAILABLE&&!id);
   assert(Async::poll(1,&progress)==RISC_RADIO_UNAVAILABLE&&Async::cancel(1)==RISC_RADIO_UNAVAILABLE);
   assert(!Async::idle()&&!Async::sharedReady()&&!Async::sharedPhaseReady()&&!Async::tryShared()&&!Async::custodySafe());
   assert(!Async::lock()&&!Async::join("test","")&&!Async::scanStart()&&!Async::leave());
   uint8_t status=0,station[12]{},ap[12]{};int8_t rssi=0;garden_radio_scan_result_v1 scan{};scan.struct_size=sizeof(scan);
   assert(!Async::state(&status,&rssi)&&!Async::addresses(station,ap)&&!Async::scanPoll(&scan));
   Async::endShared();Async::unlock();Async::step();Async::capture(1,0,true);Async::drain();
   if(!NativeRadio::internalAtomicsReady()){
    assert(!NativeRadio::idle()&&!NativeRadio::leave()&&!NativeRadio::join("test","")&&!NativeRadio::scanStart());
    assert(!NativeRadio::state(&status,&rssi)&&!NativeRadio::scanPoll(&scan));
    NativeRadio::event(reinterpret_cast<void*>(uintptr_t(1)),WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,nullptr);
    assert(NativeRadio::generation.load()==1&&NativeRadio::eventOperation.load()==NativeRadio::Join&&!NativeRadio::joinFailed.load());
   }
   assert(calls.empty());
  }
  nativeAtomicRejectedAddress=nullptr;NativeRadio::generation.store(0);NativeRadio::eventOperation.store(NativeRadio::None);assert(Async::internalAtomicsReady()&&Async::provision()&&Async::api());
  assert(taskCreates==1&&workspaceAllocations==1);puts("Native atomics reject every state span before worker/API admission PASS");return 0;
 }
}
