// Execute the adapter against the actual pinned HWCDC producer and existing
// native observer/drain/USB-lease path. The vendor source remains unchanged.
#include "hwcdc_pinned/vendor/HWCDC.cpp"
#include "ports/esp32s3/ProviderDiagnostics.h"
#include "ports/esp32s3/SleepDiagnostics.h"
#include <iostream>

namespace {
unsigned observed=0,drained=0,observerReentries=0,drainReentries=0;
bool testObserverReentry=false,testDrainReentry=false;
std::vector<std::string> captured;
const char* invalid(){return reinterpret_cast<const char*>(uintptr_t(1));}

void rejectBeforeArguments(){
  const auto text=observed,storage=drained;
  assert(!RiscDiagnostics::providerDiagnosticReady());
  assert(risc_provider_diagnostic_printf(invalid())==-1);
  assert(risc_provider_diagnostic_printf("%s",invalid())==-1);
  assert(risc_provider_diagnostic_puts(invalid())==-1);
  assert(risc_provider_diagnostic_putchar('x')==-1);
  assert(observed==text && drained==storage);
}
void host(bool present){
  Stub::host=present;s_usb_serial_jtag_conn_status=present;connected=present;
}
void drainTx(){
  assert(tx_ring_buf);
  for(unsigned i=0;i<200 && !tx_ring_buf->data.empty();++i){
    Stub::intrStatus|=USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;
    assert(Stub::isr);Stub::isr(nullptr);
  }
  assert(tx_ring_buf->data.empty());
}
}

extern "C" void risc_native_diagnostic_observer(const char* text){
  ++observed;captured.emplace_back(text);
  if(testObserverReentry){
    ++observerReentries;
    rejectBeforeArguments();
  }
}
extern "C" void risc_native_diagnostic_drain(){
  ++drained;
  if(testDrainReentry){
    ++drainReentries;
    rejectBeforeArguments();
  }
}

int main(){
  using namespace RiscDiagnostics;
  assert(risc_provider_diagnostic_build_abi_v1==1);
  assert(risc_provider_diagnostic_abi_v1()==1);
  // No owner exists until start. These calls must not even read their input.
  rejectBeforeArguments();
  usb_serial_jtag_conn_status_init();
#if RISC_STAGE_LOGS
  assert(prepareSerial());
#endif
  Serial.begin(115200);start();host(true);
  assert(providerDiagnosticReady() && usbPhyIdle());
  assert(Stub::positiveWaits==0 && tx_timeout_ms==0);
  currentTask=reinterpret_cast<void*>(2);rejectBeforeArguments();
  currentTask=reinterpret_cast<void*>(1);
  diagnosticIsr=true;rejectBeforeArguments();diagnosticIsr=false;
  assert(providerDiagnosticReady());

  // Both entry routes reject provider callbacks during observer and drain.
  testObserverReentry=testDrainReentry=true;
  auto text=observed,storage=drained;
  assert(risc_provider_diagnostic_printf("wrapper %d\r\n",7)==9);
  assert(captured.back()=="wrapper 7" && observed==text+1 && drained==storage+1);
  line("ordinary native line");
  assert(captured.back()=="ordinary native line" && observed==text+2 && drained==storage+2);
  assert(observerReentries==2 && drainReentries==2);
  testObserverReentry=testDrainReentry=false;
  drainTx();
  assert(Stub::hostTx=="wrapper 7\nordinary native line\n");
  Stub::hostTx.clear();

  // Accepted records remain observable even when the live ring is full.
  tx_ring_buf->data.assign(tx_ring_buf->capacity,'F');
  const auto full=tx_ring_buf->data;
  text=observed;storage=drained;
  const auto waits=Stub::positiveWaits,delays=Stub::delays;
  assert(risc_provider_diagnostic_puts("full-ring capture")==17);
  assert(captured.back()=="full-ring capture" && observed==text+1 && drained==storage+1);
  assert(tx_ring_buf->data==full && Stub::hostTx.empty());
  assert(Stub::positiveWaits==waits && Stub::delays==delays);
  tx_ring_buf->data.clear();
  // A busy driver mutex is another bounded, successful capture-only path.
  tx_lock->locked=true;
  assert(risc_provider_diagnostic_putchar('m')=='m');
  assert(captured.back()=="m" && tx_ring_buf->data.empty());
  tx_lock->locked=false;
  assert(Stub::positiveWaits==waits && Stub::delays==delays);

  host(false);text=observed;storage=drained;
  assert(risc_provider_diagnostic_printf("host absent")==11);
  assert(captured.back()=="host absent" && observed==text+1 && drained==storage+1);
  assert(Stub::positiveWaits==waits && Stub::delays==delays);
  host(true);drainTx();

  // A pending recovery may not recreate serial resources during the lease.
  lightReturn(ESP_OK,4);
  assert(suspendUsbPhy() && !usbPhyIdle());
  assert(!tx_ring_buf && !rx_queue && !tx_lock && !Stub::isr && Stub::live==0);
  const auto allocations=Stub::allocationCalls,pins=Stub::pinChanges;
  const auto interruptAllocations=Stub::interruptAllocations,interruptFrees=Stub::interruptFrees;
  const auto flushes=Stub::flushes,mask=Stub::intrMask,status=Stub::intrStatus;
  const auto hostTx=Stub::hostTx;
  text=observed;storage=drained;
  assert(providerDiagnosticReady());
  for(unsigned i=0;i<1000;++i){
    const std::string expected="held "+std::to_string(i);
    assert(risc_provider_diagnostic_printf("held %u",i)==static_cast<int>(expected.size()));
    assert(captured.back()==expected);
    poll();++Stub::tick;
  }
  assert(observed==text+1000 && drained==storage);
  assert(Stub::allocationCalls==allocations && Stub::pinChanges==pins);
  assert(Stub::interruptAllocations==interruptAllocations && Stub::interruptFrees==interruptFrees);
  assert(Stub::flushes==flushes && Stub::intrMask==mask && Stub::intrStatus==status);
  assert(Stub::hostTx==hostTx && Stub::positiveWaits==waits && Stub::delays==delays);
  assert(!tx_ring_buf && !rx_queue && !tx_lock && !Stub::isr && Stub::live==0);
  currentTask=reinterpret_cast<void*>(2);rejectBeforeArguments();
  currentTask=reinterpret_cast<void*>(1);
  diagnosticIsr=true;rejectBeforeArguments();diagnosticIsr=false;

  assert(resumeUsbPhy() && usbPhyIdle());
  host(true);text=observed;storage=drained;
  assert(risc_provider_diagnostic_puts("restored")==8);
  assert(captured.back()=="restored" && observed==text+1 && drained==storage+1);
  drainTx();assert(Stub::hostTx==hostTx+"restored\n");
  Serial.end();assert(Stub::live==0 && Stub::positiveWaits==0);
  std::cout<<"provider diagnostics actual pinned HWCDC: context gates, observer/drain reentry, lossy capture, held-PHY fence and restoration PASS\n";
}
