#include "vendor/HWCDC.cpp"
#include "ports/esp32s3/SleepDiagnostics.h"

// Run the real Runtime/Port lifecycle against the same pinned HWCDC adapter,
// rather than replacing native ownership with an independent boolean.
extern "C" void test_native_usb_start(){
 usb_serial_jtag_conn_status_init();Serial.begin(115200);RiscDiagnostics::start();
 assert(Serial.available()>=0 && Serial.availableForWrite()>0);
}
extern "C" bool test_native_usb_idle(){return RiscDiagnostics::usbPhyIdle();}
extern "C" bool test_native_usb_suspend(){return RiscDiagnostics::suspendUsbPhy();}
extern "C" bool test_native_usb_resume(bool fail){
 Stub::failure=fail?Stub::UsbRoute:Stub::None;
 return RiscDiagnostics::resumeUsbPhy();
}
extern "C" void test_native_usb_finish(){
 assert(RiscDiagnostics::usbPhyIdle());
 assert(Serial.available()>=0 && Serial.availableForWrite()>0 && Stub::isr);
 // A resumed console must also have the selected, running physical backend.
 if(Stub::usbResets){
  assert(SYSTEM.perip_clk_en1.usb_device_clk_en && !SYSTEM.perip_rst_en1.usb_device_rst);
  assert(RTCCNTL.usb_conf.sw_hw_usb_phy_sel && !RTCCNTL.usb_conf.sw_usb_phy_sel);
 }
 Serial.end();assert(Stub::live==0);
}
