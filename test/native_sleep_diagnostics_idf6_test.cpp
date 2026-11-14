#include "ports/esp32s3/SleepDiagnostics.h"
#include "ports/esp32s3/NativePlatform.h"
#include "NativeUsbTest.h"
#include <driver/usb_serial_jtag.h>
#include <hal/usb_serial_jtag_ll.h>
#include <esp_idf_version.h>
#include <cstdio>
#include <initializer_list>
using namespace NativeUsbTest;
static_assert(ESP_IDF_VERSION_MAJOR>=6,"Requires real selected IDF6 driver declarations");
extern "C" {
int64_t esp_timer_get_time(){return now;}
uint32_t esp_get_free_heap_size(){return 100000;}
int esp_reset_reason(){return ESP_RST_POWERON;}
int esp_sleep_get_wakeup_cause(){return 0;}
void vTaskDelay(TickType_t ticks){assert(!USB_SERIAL_JTAG.conf0.usb_pad_enable);now+=int64_t(ticks)*10000;}
BaseType_t xPortGetCoreID(){return core;}
TaskHandle_t xTaskGetCurrentTaskHandle(){return task;}
bool xPortInIsrContext(){return false;}
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t* config){
  assert(!installed && config && config->rx_buffer_size==256 && config->tx_buffer_size>=256);++installs;
  if(failure==Install)return ESP_ERR_NO_MEM;
  // Like IDF6: select internal PHY and pads, but do not reset or detach it.
  usb_serial_jtag_ll_phy_enable_external(false);usb_serial_jtag_ll_phy_enable_pad(true);
  installed=true;installedCore=core;return ESP_OK;
}
esp_err_t usb_serial_jtag_driver_uninstall(){
  assert(installed && installedCore==core);++uninstalls;
  if(failure==Uninstall)return ESP_FAIL;
  installed=false;installedCore=-1;
  // The real IDF6 uninstall deliberately leaves clock and pads unchanged.
  return ESP_OK;
}
bool usb_serial_jtag_is_driver_installed(){return installed;}
bool usb_serial_jtag_is_connected(){return host;}
int usb_serial_jtag_read_bytes(void*,uint32_t length,uint32_t ticks){assert(installed && length==1 && ticks==0);return 0;}
esp_err_t usb_serial_jtag_wait_tx_done(uint32_t ticks){assert(installed && ticks==0);return ESP_OK;}
int usb_serial_jtag_write_bytes(const void*,size_t length,uint32_t ticks){assert(installed && length<=128 && ticks==0);++writes;return int(length);}
void risc_native_diagnostic_observer(const char*){++observed;}
void risc_native_diagnostic_drain(){++drained;}
}
static bool routed(){
  return installed && SYSTEM.perip_clk_en1.usb_device_clk_en && !SYSTEM.perip_rst_en1.usb_device_rst &&
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel && !RTCCNTL.usb_conf.sw_usb_phy_sel &&
    !USB_SERIAL_JTAG.conf0.phy_sel && !USB_SERIAL_JTAG.conf0.pad_pull_override &&
    USB_SERIAL_JTAG.conf0.dp_pullup && USB_SERIAL_JTAG.conf0.usb_pad_enable;
}
static void sleepCycle(){
  failure=None;RiscDiagnostics::lightReturn(ESP_OK,4);
  auto before=resets;RiscDiagnostics::poll();
  assert(!installed && !USB_SERIAL_JTAG.conf0.usb_pad_enable && resets==before+1);
  auto allocated=installs;
  now+=19000;RiscDiagnostics::poll();assert(installs==allocated && !installed);
  now+=1000;RiscDiagnostics::poll();assert(installs==allocated+1 && routed());
}
int main(){
  RiscPlatform::console.begin(115200);RiscDiagnostics::start();assert(installed);
  // A pending request performs no hardware operation until the owner polls.
  auto before=resets;RiscDiagnostics::lightReturn(ESP_OK,4);assert(resets==before);
  RiscDiagnostics::poll();assert(!installed && !USB_SERIAL_JTAG.conf0.usb_pad_enable);
  now+=19000;RiscDiagnostics::poll();assert(!installed);
  now+=1000;RiscDiagnostics::poll();assert(routed());
  for(unsigned i=0;i<8;i++)sleepCycle();
  for(auto fault:{Clock,Reset,DetachPad,AttachPad,Route,Pull,Install,Uninstall}){
    failure=fault;RiscDiagnostics::lightReturn(ESP_OK,4);RiscDiagnostics::poll();
    now+=20000;RiscDiagnostics::poll();
    auto allocated=installs,reset=resets,sent=writes;
    for(unsigned i=0;i<32;i++){now+=100000;RiscDiagnostics::poll();RiscDiagnostics::line("retained recovery");}
    assert(installs==allocated && resets==reset && writes==sent);
    // Failure cannot retry itself; a new successful sleep explicitly permits it.
    sleepCycle();
  }
  // Wrong-core uninstall cannot reset the live DMA/IRQ owner.
  core=1;before=resets;RiscDiagnostics::lightReturn(ESP_OK,4);RiscDiagnostics::poll();
  assert(installed && resets==before);now+=20000;RiscDiagnostics::poll();assert(resets==before);
  core=0;sleepCycle();
  // A separately installed driver is never adopted, reset or disconnected.
  RiscPlatform::console.end();installed=true;installedCore=0;
  before=resets;auto pads=padChanges,release=uninstalls;
  RiscDiagnostics::lightReturn(ESP_OK,4);RiscDiagnostics::poll();now+=20000;RiscDiagnostics::poll();
  assert(installed && resets==before && padChanges==pads && uninstalls==release);
  installed=false;installedCore=-1;RiscPlatform::console.begin(115200);sleepCycle();
#if RISC_ENABLE_USB_PHY
  assert(RiscDiagnostics::usbPhyIdle());
  RiscDiagnostics::lightReturn(ESP_OK,4);
  assert(RiscDiagnostics::suspendUsbPhy() && !RiscDiagnostics::usbPhyIdle() && !installed && !USB_SERIAL_JTAG.conf0.usb_pad_enable);
  auto allocated=installs;before=resets;auto drainedBefore=drained,observedBefore=observed;
  for(unsigned i=0;i<100;i++){now+=1000;RiscDiagnostics::poll();RiscDiagnostics::line("leased");}
  assert(installs==allocated && resets==before && drained==drainedBefore && observed==observedBefore+100);
#if RISC_SLEEP_DIAGNOSTICS
  assert(RiscDiagnostics::checkpoint("owner.elf",12,"leased summary",14)==0);
#endif
  task=reinterpret_cast<void*>(2);assert(!RiscDiagnostics::resumeUsbPhy());task=reinterpret_cast<void*>(1);
  for(auto fault:{Clock,Reset,DetachPad,AttachPad,Route,Pull,Install}){
    failure=fault;assert(!RiscDiagnostics::resumeUsbPhy() && !RiscDiagnostics::usbPhyIdle());
    allocated=installs;before=resets;
    for(unsigned i=0;i<20;i++){now+=100000;RiscDiagnostics::poll();}
    assert(installs==allocated && resets==before);
  }
  failure=None;
  RTCCNTL.usb_conf.sw_usb_phy_sel=1;SYSTEM.perip_clk_en1.usb_device_clk_en=0;
  auto timeBefore=now;assert(RiscDiagnostics::resumeUsbPhy() && RiscDiagnostics::usbPhyIdle() && routed());
  assert(now-timeBefore>=20000 && now-timeBefore<=30000);
  failure=Uninstall;before=resets;
  assert(!RiscDiagnostics::suspendUsbPhy() && !RiscDiagnostics::usbPhyIdle() && installed && resets==before);
  assert(!RiscDiagnostics::resumeUsbPhy() && installed && resets==before);
  failure=None;assert(RiscDiagnostics::resumeUsbPhy() && RiscDiagnostics::usbPhyIdle());
  for(unsigned i=0;i<8;i++){
    assert(RiscDiagnostics::suspendUsbPhy());host=false;
    RTCCNTL.usb_conf.sw_usb_phy_sel=1;SYSTEM.perip_clk_en1.usb_device_clk_en=0;
    assert(RiscDiagnostics::resumeUsbPhy() && routed());
  }
#endif
  RiscPlatform::console.end();assert(!installed && !rccDepth);
  std::puts("Native IDF6 USB diagnostics: actual Console/driver declarations, atomic RCC, physical 20ms detach, same-core/foreign-owner safety, retained faults and lease fences PASS");
}
