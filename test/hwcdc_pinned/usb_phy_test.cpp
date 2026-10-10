#include "vendor/HWCDC.cpp"
#include "ports/esp32s3/SleepDiagnostics.h"
#include <iostream>
using namespace RiscDiagnostics;
static unsigned observed,drained;
extern "C" void risc_native_diagnostic_observer(const char*){++observed;}
extern "C" void risc_native_diagnostic_drain(){++drained;}
static bool serialRoute(){
 return SYSTEM.perip_clk_en1.usb_device_clk_en && !SYSTEM.perip_rst_en1.usb_device_rst &&
  RTCCNTL.usb_conf.sw_hw_usb_phy_sel && !RTCCNTL.usb_conf.sw_usb_phy_sel &&
  !USB_SERIAL_JTAG.conf0.phy_sel && !USB_SERIAL_JTAG.conf0.pad_pull_override &&
  USB_SERIAL_JTAG.conf0.dp_pullup && USB_SERIAL_JTAG.conf0.usb_pad_enable;
}
static void connectHost(){
 assert(serialRoute() && Stub::isr);Stub::host=true;
 USB_SERIAL_JTAG.int_raw.sof_int_raw=1;usb_serial_jtag_sof_tick_hook();
 Stub::hostRx.push_back('!');Stub::intrStatus|=USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT;
 Stub::isr(nullptr);assert(Serial.available()==1 && Serial.read()=='!' && bool(Serial));
 const char message[]="serial restored\n";
 assert(Serial.write(reinterpret_cast<const uint8_t*>(message),sizeof(message)-1)==sizeof(message)-1);
 Stub::intrStatus|=USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;Stub::isr(nullptr);
 assert(Stub::hostTx.find(message)!=std::string::npos);Stub::hostTx.clear();
}
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
 // Regression: HWCDC allocations can succeed with the physical backend still
 // clock-gated and assigned to OTG. Release must actually restore that backend.
 RTCCNTL.usb_conf.sw_hw_usb_phy_sel=1;RTCCNTL.usb_conf.sw_usb_phy_sel=1;
 SYSTEM.perip_clk_en1.usb_device_clk_en=0;
 auto resets=Stub::usbResets;
 assert(resumeUsbPhy() && usbPhyIdle());
 assert(serialRoute() && Stub::usbResets==resets+1);
 connectHost();assert(suspendUsbPhy());
 for(auto failure:{Stub::Mutex,Stub::Rx,Stub::Tx,Stub::Interrupt,Stub::UsbClock,Stub::UsbReset,Stub::UsbRoute,Stub::UsbPad}){
  Stub::failure=failure;assert(!resumeUsbPhy()&&!usbPhyIdle());
  allocations=Stub::allocationCalls;pins=Stub::pinChanges;frees=Stub::interruptFrees;
  for(unsigned n=0;n<100;++n){poll();line("retained restore");}
  assert(Stub::allocationCalls==allocations&&Stub::pinChanges==pins&&Stub::interruptFrees==frees);
 }
 // Both an unselected PHY and a clock disabled while leased must be repaired,
 // irrespective of whether a computer is present. Queue readiness is not a
 // substitute for this hardware boundary, as the old adapter demonstrates.
 RTCCNTL.usb_conf.sw_hw_usb_phy_sel=1;RTCCNTL.usb_conf.sw_usb_phy_sel=1;
 SYSTEM.perip_clk_en1.usb_device_clk_en=0;
 resets=Stub::usbResets;
 Stub::failure=Stub::None;assert(resumeUsbPhy()&&usbPhyIdle());
 assert(serialRoute() && Stub::usbResets==resets+1);
 assert(Serial.available()>=0&&Serial.availableForWrite()>0&&Stub::isr);
 connectHost();
 allocations=Stub::allocationCalls;for(unsigned n=0;n<100;++n)poll();assert(Stub::allocationCalls==allocations);
 assert(!resumeUsbPhy());
 for(unsigned n=0;n<16;++n){
  assert(suspendUsbPhy());Stub::host=false;
  for(unsigned tick=0;tick<8;++tick)usb_serial_jtag_sof_tick_hook();
  assert(!HWCDC::isPlugged());
  RTCCNTL.usb_conf.sw_hw_usb_phy_sel=1;RTCCNTL.usb_conf.sw_usb_phy_sel=1;
  const auto before=Stub::tick,detached=Stub::detachedDelays;
  assert(resumeUsbPhy() && usbPhyIdle() && serialRoute() && Stub::tick-before==20);
  assert(Stub::detachedDelays==detached+1);
  connectHost();
 }
 Serial.end();assert(Stub::live==0);
 std::cout<<"Actual pinned HWCDC: checked clock/reset/PHY/pads, repeated absent-host resume with RX/TX, retained allocation/register failures, lease fences and retry PASS\n";
}
