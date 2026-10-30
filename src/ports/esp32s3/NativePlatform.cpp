#if RISC_NATIVE_ESP_IDF
#include "NativePlatform.h"
#include <cstring>
#if RISC_CONSOLE_USB_SERIAL_JTAG
#include <driver/usb_serial_jtag.h>
#else
#include <driver/uart.h>
#endif
namespace RiscPlatform {
Console console;
void Console::begin(unsigned baud){
  if(installed_)return;
#if RISC_CONSOLE_USB_SERIAL_JTAG
  (void)baud;
  // Never adopt an independently initialized controller or its ownership.
  if(usb_serial_jtag_is_driver_installed())return;
  usb_serial_jtag_driver_config_t config{};
  config.tx_buffer_size=txBytes_;config.rx_buffer_size=256;
  if(usb_serial_jtag_driver_install(&config)!=ESP_OK)return;
#else
  if(uart_is_driver_installed(UART_NUM_0))return;
  uart_config_t config{};config.baud_rate=baud;config.data_bits=UART_DATA_8_BITS;
  config.parity=UART_PARITY_DISABLE;config.stop_bits=UART_STOP_BITS_1;
  config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;config.source_clk=UART_SCLK_DEFAULT;
  if(uart_param_config(UART_NUM_0,&config)!=ESP_OK)return;
  if(uart_driver_install(UART_NUM_0,256,0,0,nullptr,0)!=ESP_OK)return;
#endif
  core_=xPortGetCoreID();installed_=true;cached_=-1;txPending_=0;
}
void Console::end(){
  if(!installed_ || core_!=xPortGetCoreID())return;
#if RISC_CONSOLE_USB_SERIAL_JTAG
  const auto result=usb_serial_jtag_driver_uninstall();
#else
  const auto result=uart_driver_delete(UART_NUM_0);
#endif
  if(result==ESP_OK){installed_=false;cached_=-1;core_=-1;txPending_=0;}
}
Console::operator bool()const{
  if(!installed_)return false;
#if RISC_CONSOLE_USB_SERIAL_JTAG
  return usb_serial_jtag_is_connected();
#else
  return true;
#endif
}
int Console::available(){
  if(!installed_)return -1;
  if(cached_>=0)return 1;
  uint8_t byte=0;
#if RISC_CONSOLE_USB_SERIAL_JTAG
  const int n=usb_serial_jtag_read_bytes(&byte,1,0);
#else
  const int n=uart_read_bytes(UART_NUM_0,&byte,1,0);
#endif
  if(n==1){cached_=byte;return 1;}return 0;
}
int Console::read(){if(available()<=0)return -1;const int value=cached_;cached_=-1;return value;}
int Console::availableForWrite()const{
  if(!installed_ || core_!=xPortGetCoreID())return 0;
#if RISC_CONSOLE_USB_SERIAL_JTAG
  // The SDK exposes completion, not ring free-space. Count accepted bytes
  // conservatively until its zero-timeout completion probe succeeds. Partial
  // drain never invents room, and a missing host never blocks the owner.
  if(txPending_ && usb_serial_jtag_wait_tx_done(0)==ESP_OK)txPending_=0;
  return int(txBytes_-txPending_);
#else
  return 128; // UART FIFO writes retain their independent partial-write API.
#endif
}
size_t Console::write(const uint8_t* data,size_t count){
  if(!installed_ || !data || !count || core_!=xPortGetCoreID())return 0;
  if(count>128)count=128;
#if RISC_CONSOLE_USB_SERIAL_JTAG
  const size_t room=size_t(availableForWrite());if(count>room)count=room;
  if(!count)return 0;
  const int n=usb_serial_jtag_write_bytes(data,count,0);
  if(n>0 && size_t(n)<=count)txPending_+=size_t(n);
#else
  const int n=uart_tx_chars(UART_NUM_0,reinterpret_cast<const char*>(data),count);
#endif
  return n>0 && size_t(n)<=count?size_t(n):0;
}
size_t Console::println(const char* text){
  if(!text)return 0;
  const size_t size=std::strlen(text);size_t sent=0;
  while(sent<size){const size_t n=write(reinterpret_cast<const uint8_t*>(text)+sent,size-sent);if(!n)return sent;sent+=n;}
  const uint8_t newline='\n';return sent+write(&newline,1);
}
size_t Console::setTxBufferSize(size_t bytes){
  if(installed_ || bytes<256 || bytes>8192)return 0;
  txBytes_=bytes;return bytes;
}
}
#else
#include "NativePlatform.h"
namespace RiscPlatform {uint32_t freeHeap(){return ESP.getFreeHeap();}}
#endif
