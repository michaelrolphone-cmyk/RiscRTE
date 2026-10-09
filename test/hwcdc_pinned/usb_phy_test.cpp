#include "vendor/HWCDC.cpp"
#include "ports/esp32s3/SleepDiagnostics.h"
#include <iostream>
using namespace RiscDiagnostics;
static unsigned observed,drained;
extern "C" void risc_native_diagnostic_observer(const char*){++observed;}
extern "C" void risc_native_diagnostic_drain(){++drained;}
int main(){
 usb_serial_jtag_conn_status_init();Serial.begin(115200);start();assert(usbPhyIdle());
 // A pending wake recovery must never restart HWCDC after the lease begins.
 lightReturn(ESP_OK,4);assert(suspendUsbPhy()&&!usbPhyIdle());
 assert(Stub::intrMask==0&&!Stub::isr&&Stub::live==0);
 auto allocations=Stub::allocationCalls,pins=Stub::pinChanges,frees=Stub::interruptFrees;
 auto storage=drained,text=observed;
 for(unsigned n=0;n<1000;++n){poll();line("trace during USB export");++Stub::tick;}
 assert(Stub::allocationCalls==allocations&&Stub::pinChanges==pins&&Stub::interruptFrees==frees);
 assert(drained==storage&&observed==text+1000);
 assert(!suspendUsbPhy());
 currentTask=reinterpret_cast<void*>(2);assert(!resumeUsbPhy()&&!usbPhyIdle());
 currentTask=reinterpret_cast<void*>(1);assert(!usbPhyIdle());
 for(auto failure:{Stub::Mutex,Stub::Rx,Stub::Tx,Stub::Interrupt}){
  Stub::failure=failure;assert(!resumeUsbPhy()&&!usbPhyIdle());
  allocations=Stub::allocationCalls;pins=Stub::pinChanges;frees=Stub::interruptFrees;
  for(unsigned n=0;n<100;++n){poll();line("retained restore");}
  assert(Stub::allocationCalls==allocations&&Stub::pinChanges==pins&&Stub::interruptFrees==frees);
 }
 Stub::failure=Stub::None;assert(resumeUsbPhy()&&usbPhyIdle());
 assert(Serial.available()>=0&&Serial.availableForWrite()>0&&Stub::isr);
 allocations=Stub::allocationCalls;for(unsigned n=0;n<100;++n)poll();assert(Stub::allocationCalls==allocations);
 assert(!resumeUsbPhy());assert(suspendUsbPhy()&&resumeUsbPhy());
 Serial.end();assert(Stub::live==0);
 std::cout<<"Actual pinned HWCDC: lease fences pending recovery, IRQ/RX/TX, preserves RAM trace, suppresses SD drains, checked restore failures and retry PASS\n";
}
