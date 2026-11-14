#include "ports/esp32s3/SleepDiagnostics.h"
#include "ports/esp32s3/NativePlatform.h"
#include "NativeUsbTest.h"
#include <driver/usb_serial_jtag.h>
#include <hal/usb_serial_jtag_ll.h>
#include <esp_idf_version.h>
#include <cstdio>
#include <initializer_list>
#include <string>
using namespace NativeUsbTest;
static bool idle=true;static size_t pending=0,capacity=0;static std::string captured;
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
  assert(!installed && config && config->rx_buffer_size==256 && config->tx_buffer_size>=256);++installs;capacity=config->tx_buffer_size;pending=0;
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
esp_err_t usb_serial_jtag_wait_tx_done(uint32_t ticks){assert(installed && ticks==0);if(!idle)return ESP_ERR_TIMEOUT;pending=0;return ESP_OK;}
int usb_serial_jtag_write_bytes(const void* bytes,size_t length,uint32_t ticks){assert(installed && length<=128 && ticks==0);++writes;if(length>capacity-pending)return 0;pending+=length;captured.append(static_cast<const char*>(bytes),length);return int(length);}
void risc_native_diagnostic_observer(const char*){++observed;}
void risc_native_diagnostic_drain(){++drained;}
}
int main(){
  // Actual native Console + actual line sink, both with and without stage logs.
  // Do not call prepareSerial: the core default must work for ordinary builds.
  RiscPlatform::console.begin(115200);assert(capacity==8192);
  RiscDiagnostics::start();host=true;
  for(unsigned n:{127u,128u,129u,200u,255u}){
    std::string line(n,'a');captured.clear();RiscDiagnostics::line(line.c_str());
    assert(captured==line+"\n");
  }
  assert(RiscPlatform::console.availableForWrite()==8192);
  captured.clear();idle=false;
  std::string line(255,'b');for(unsigned i=0;i<32;++i)RiscDiagnostics::line(line.c_str());
  assert(captured.size()==8192 && RiscPlatform::console.availableForWrite()==0);
  const auto before=captured;RiscDiagnostics::line("full buffer");assert(captured==before);
  // Reconnection does not fabricate capacity; complete drain reclaims it.
  host=false;host=true;assert(RiscPlatform::console.availableForWrite()==0);
  idle=true;captured.clear();RiscDiagnostics::poll();captured.clear();
  RiscDiagnostics::line(std::string(200,'c').c_str());assert(captured==std::string(200,'c')+"\n");
  // No record was chopped into another one, and no disconnected-host wait ran.
  RiscPlatform::console.end();puts("Actual native Console/line sink: 127..255-byte records, default 8 KiB burst, full-buffer backpressure, reconnect and drain PASS");
}
