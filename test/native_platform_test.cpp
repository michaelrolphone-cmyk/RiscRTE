// Native platform contract tests. Real selected ESP-IDF driver declarations are
// used; mocks below model resources and failures without touching hardware.
#include "ports/esp32s3/NativePlatform.h"
#if RISC_CONSOLE_USB_SERIAL_JTAG
#include <driver/usb_serial_jtag.h>
#else
#include <driver/uart.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr,"%s:%d: CHECK(%s) failed\n",__FILE__,__LINE__,#condition); \
  std::abort(); \
} } while (0)

static_assert(!std::is_copy_constructible<RiscPlatform::Console>::value,
              "Console ownership must not be copied");
static_assert(!std::is_copy_assignable<RiscPlatform::Console>::value,
              "Console ownership must not be copy assigned");
static_assert(!std::is_move_constructible<RiscPlatform::Console>::value,
              "Console ownership must not be implicitly moved");
static_assert(!std::is_move_assignable<RiscPlatform::Console>::value,
              "Console ownership must not be move assigned");

namespace {
struct ReadReply { int count; uint8_t byte; };
struct Driver {
  bool installed=false, connected=true, installConflict=false, txIdle=true;
  int core=0, installCore=-1;
  esp_err_t installResult=ESP_OK, uninstallResult=ESP_OK, configResult=ESP_OK;
  unsigned installCalls=0, uninstallCalls=0, configCalls=0;
  unsigned readCalls=0, writeCalls=0, connectedCalls=0;
  size_t txBytes=0, rxBytes=0;
  int64_t timeUs=0;
  uint32_t heapBytes=123456;
  std::deque<ReadReply> reads;
  std::deque<int> writes;
  std::vector<std::string> attempts;
  std::string accepted;
  std::vector<TickType_t> delays;
#if !RISC_CONSOLE_USB_SERIAL_JTAG
  uart_config_t uartConfig{};
#endif
} driver;

esp_err_t install(size_t rx,size_t tx) {
  ++driver.installCalls;
  CHECK(!driver.installed);
  driver.rxBytes=rx;driver.txBytes=tx;driver.installCore=driver.core;
  if(driver.installConflict){
    driver.installed=true;return ESP_ERR_INVALID_STATE;
  }
  if(driver.installResult==ESP_OK)driver.installed=true;
  return driver.installResult;
}
esp_err_t uninstall() {
  ++driver.uninstallCalls;
  CHECK(driver.installed);
  CHECK(driver.core==driver.installCore);
  if(driver.uninstallResult==ESP_OK)driver.installed=false;
  return driver.uninstallResult;
}
int readBytes(void* data,uint32_t count,uint32_t ticks) {
  ++driver.readCalls;
  CHECK(driver.installed);CHECK(data);CHECK(count==1);CHECK(ticks==0);
  if(driver.reads.empty())return 0;
  const auto reply=driver.reads.front();driver.reads.pop_front();
  if(reply.count==1)*static_cast<uint8_t*>(data)=reply.byte;
  return reply.count;
}
int writeBytes(const void* data,size_t count) {
  ++driver.writeCalls;
  CHECK(driver.installed);CHECK(data);CHECK(count>0);CHECK(count<=128);
  CHECK(driver.core==driver.installCore);
  const std::string bytes(static_cast<const char*>(data),count);
  driver.attempts.push_back(bytes);
  int result=int(count);
  if(!driver.writes.empty()){result=driver.writes.front();driver.writes.pop_front();}
  if(result>0 && size_t(result)<=count)driver.accepted.append(bytes,0,size_t(result));
  return result;
}
void reset(){CHECK(!driver.installed);driver=Driver{};}

void stoppedIsInert(){
  reset();RiscPlatform::Console console;
  const uint8_t byte=42;
  CHECK(!console);CHECK(console.available()==-1);CHECK(console.read()==-1);
  CHECK(console.availableForWrite()==0);CHECK(console.write(&byte,1)==0);
  CHECK(console.println("uninstalled")==0);console.end();
  CHECK(driver.readCalls==0);CHECK(driver.writeCalls==0);
  CHECK(driver.connectedCalls==0);CHECK(driver.uninstallCalls==0);
}
void ownedStartupAndRestart(){
  reset();RiscPlatform::Console console;
  console.begin(115200);
  CHECK(driver.installed);CHECK(driver.installCalls==1);CHECK(driver.installCore==0);
  CHECK(driver.rxBytes==256);CHECK(console.available()==0);
  CHECK(console.availableForWrite()==(RISC_CONSOLE_USB_SERIAL_JTAG?8192:128));CHECK(bool(console));
#if RISC_CONSOLE_USB_SERIAL_JTAG
  CHECK(driver.txBytes==8192);
  driver.connected=false;CHECK(!console);CHECK(console.availableForWrite()==(RISC_CONSOLE_USB_SERIAL_JTAG?8192:128));
  driver.connected=true;
#else
  CHECK(driver.configCalls==1);CHECK(driver.txBytes==0);
  CHECK(driver.uartConfig.baud_rate==115200);
  CHECK(driver.uartConfig.data_bits==UART_DATA_8_BITS);
  CHECK(driver.uartConfig.parity==UART_PARITY_DISABLE);
  CHECK(driver.uartConfig.stop_bits==UART_STOP_BITS_1);
  CHECK(driver.uartConfig.flow_ctrl==UART_HW_FLOWCTRL_DISABLE);
  CHECK(driver.uartConfig.source_clk==UART_SCLK_DEFAULT);
#endif
  console.begin(9600);CHECK(driver.installCalls==1);
  driver.core=1;console.begin(9600);CHECK(driver.installCalls==1);
  driver.core=0;console.end();
  CHECK(!driver.installed);CHECK(!console);CHECK(console.available()==-1);
  CHECK(console.availableForWrite()==0);CHECK(driver.uninstallCalls==1);
  console.end();CHECK(driver.uninstallCalls==1);
  driver.core=1;console.begin(9600);CHECK(driver.installCalls==2);
  CHECK(driver.installCore==1);CHECK(bool(console));console.end();
}
void foreignControllerIsNotAdopted(){
  reset();RiscPlatform::Console owner,foreign;
  owner.begin(115200);foreign.begin(115200);
  CHECK(driver.installCalls==1);CHECK(!foreign);CHECK(foreign.available()==-1);
  CHECK(foreign.availableForWrite()==0);
  const uint8_t byte=42;CHECK(foreign.write(&byte,1)==0);
  foreign.end();CHECK(driver.uninstallCalls==0);CHECK(driver.installed);
  owner.end();CHECK(!driver.installed);
  foreign.begin(115200);CHECK(driver.installCalls==2);CHECK(bool(foreign));
  foreign.end();
}
void failedInstallDoesNotGainOwnership(){
  reset();RiscPlatform::Console console;driver.installResult=ESP_ERR_NO_MEM;
  console.begin(115200);CHECK(driver.installCalls==1);CHECK(!driver.installed);
  CHECK(!console);CHECK(console.available()==-1);CHECK(console.availableForWrite()==0);
  console.end();CHECK(driver.uninstallCalls==0);
  driver.installResult=ESP_OK;driver.core=1;
  console.begin(115200);CHECK(driver.installCalls==2);CHECK(driver.installCore==1);
  CHECK(bool(console));console.end();
}
void installRaceDoesNotAdoptForeignController(){
  reset();RiscPlatform::Console console;
  // Another owner wins between is_driver_installed() and install(). The SDK
  // rejects this attempt while leaving the other owner's controller installed.
  driver.installConflict=true;console.begin(115200);
  CHECK(driver.installCalls==1);CHECK(driver.installed);CHECK(!console);
  CHECK(console.available()==-1);CHECK(console.availableForWrite()==0);
  console.end();CHECK(driver.uninstallCalls==0);
  console.begin(115200);CHECK(driver.installCalls==1);
  driver.installed=false; // The foreign owner releases its own driver.
  driver.installConflict=false;console.begin(115200);
  CHECK(driver.installCalls==2);CHECK(bool(console));console.end();
}
void txConfigurationBoundaries(){
  reset();RiscPlatform::Console console;
  CHECK(console.setTxBufferSize(0)==0);CHECK(console.setTxBufferSize(255)==0);
  CHECK(console.setTxBufferSize(8193)==0);
  CHECK(console.setTxBufferSize(std::numeric_limits<size_t>::max())==0);
  CHECK(console.setTxBufferSize(256)==256);CHECK(console.setTxBufferSize(8192)==8192);
  CHECK(console.setTxBufferSize(8193)==0);
  console.begin(115200);
#if RISC_CONSOLE_USB_SERIAL_JTAG
  CHECK(driver.txBytes==8192);
#else
  CHECK(driver.txBytes==0); // FIFO path; see native_platform_TESTING.md caveat.
#endif
  CHECK(console.setTxBufferSize(256)==0);console.end();
  CHECK(console.setTxBufferSize(256)==256);console.begin(115200);
#if RISC_CONSOLE_USB_SERIAL_JTAG
  CHECK(driver.txBytes==256);
#endif
  console.end();
}
void writesAreBoundedWithoutExplicitWait(){
  reset();RiscPlatform::Console console;console.begin(115200);
  const std::string data(400,'x');const auto bytes=reinterpret_cast<const uint8_t*>(data.data());
  console.setTxTimeoutMs(60000);
  CHECK(console.write(bytes,data.size())==128);CHECK(driver.writeCalls==1);
  CHECK(driver.attempts.back().size()==128);
  CHECK(console.write(nullptr,50)==0);CHECK(console.write(bytes,0)==0);
  CHECK(driver.writeCalls==1);
  CHECK(console.write(bytes,std::numeric_limits<size_t>::max())==128);
  CHECK(driver.attempts.back().size()==128);
  driver.core=1;CHECK(console.write(bytes,1)==0);CHECK(driver.writeCalls==2);
  driver.core=0;console.end();CHECK(driver.delays.empty());
}
void writeErrorsAndPartialResults(){
  reset();RiscPlatform::Console console;console.begin(115200);
  const uint8_t data[8]={0,1,2,3,4,5,6,7};
  driver.writes={3,0,-1,9};
  CHECK(console.write(data,8)==3);CHECK(console.write(data,8)==0);
  CHECK(console.write(data,8)==0);CHECK(console.write(data,8)==0);
  CHECK(driver.writeCalls==4);CHECK(driver.accepted==std::string("\0\1\2",3));
  console.end();
}
void printlnHonorsPartialFailures(){
  reset();RiscPlatform::Console console;console.begin(115200);
  CHECK(console.println(nullptr)==0);CHECK(driver.writeCalls==0);
  driver.writes={2,0};CHECK(console.println("hello")==2);
  CHECK(driver.writeCalls==2);CHECK(driver.attempts[0]=="hello");
  CHECK(driver.attempts[1]=="llo");CHECK(driver.accepted=="he");
  driver.attempts.clear();driver.accepted.clear();driver.writes={2,3,0};
  CHECK(console.println("hello")==5);CHECK(driver.attempts.size()==3);
  CHECK(driver.attempts[2]=="\n");CHECK(driver.accepted=="hello");
  driver.attempts.clear();driver.accepted.clear();driver.writes={-1};
  CHECK(console.println("hello")==0);CHECK(driver.attempts.size()==1);
  CHECK(driver.accepted.empty());
  driver.attempts.clear();CHECK(console.println("")==1);
  CHECK(driver.attempts.size()==1);CHECK(driver.accepted=="\n");
  driver.attempts.clear();driver.accepted.clear();
  const std::string longLine(300,'z');CHECK(console.println(longLine.c_str())==301);
  CHECK(driver.attempts.size()==4);CHECK(driver.attempts[0].size()==128);
  CHECK(driver.attempts[1].size()==128);CHECK(driver.attempts[2].size()==44);
  CHECK(driver.attempts[3]=="\n");CHECK(driver.accepted==longLine+"\n");
  CHECK(driver.delays.empty());console.end();
}
void readCacheAndErrors(){
  reset();RiscPlatform::Console console;console.begin(115200);
  driver.reads={{1,0},{1,255},{0,0},{-1,0},{2,0},{1,'A'}};
  CHECK(console.available()==1);CHECK(driver.readCalls==1);
  CHECK(console.available()==1);CHECK(driver.readCalls==1);
  CHECK(console.read()==0);CHECK(driver.readCalls==1);
  CHECK(console.read()==255);CHECK(driver.readCalls==2);
  CHECK(console.available()==0);CHECK(driver.readCalls==3);
  CHECK(console.read()==-1);CHECK(driver.readCalls==4);
  CHECK(console.available()==0);CHECK(driver.readCalls==5);
  CHECK(console.read()=='A');CHECK(driver.readCalls==6);
  CHECK(console.read()==-1);CHECK(driver.readCalls==7);console.end();
}
void wrongCoreCannotReleaseOwnership(){
  reset();RiscPlatform::Console console;console.begin(115200);
  driver.reads={{1,'x'}};CHECK(console.available()==1);
  driver.core=1;console.end();CHECK(driver.uninstallCalls==0);
  CHECK(driver.installed);CHECK(bool(console));CHECK(console.availableForWrite()==0);
  CHECK(console.setTxBufferSize(8192)==0);
  driver.core=0;CHECK(console.read()=='x');CHECK(driver.readCalls==1);
  console.end();CHECK(driver.uninstallCalls==1);CHECK(!driver.installed);
}
void failedUninstallRetainsOwnershipAndCache(){
  reset();RiscPlatform::Console console;console.begin(115200);
  driver.reads={{1,'y'}};CHECK(console.available()==1);
  driver.uninstallResult=ESP_FAIL;console.end();
  CHECK(driver.uninstallCalls==1);CHECK(driver.installed);CHECK(bool(console));
  CHECK(console.availableForWrite()==(RISC_CONSOLE_USB_SERIAL_JTAG?8192:128));CHECK(console.setTxBufferSize(8192)==0);
  console.begin(115200);CHECK(driver.installCalls==1);
  CHECK(console.read()=='y');CHECK(driver.readCalls==1);
  const uint8_t byte=42;CHECK(console.write(&byte,1)==1);
  driver.core=1;console.end();CHECK(driver.uninstallCalls==1);
  driver.core=0;driver.uninstallResult=ESP_OK;console.end();
  CHECK(driver.uninstallCalls==2);CHECK(!driver.installed);
  CHECK(console.available()==-1);CHECK(console.availableForWrite()==0);
}
void successfulEndClearsCachedByte(){
  reset();RiscPlatform::Console console;console.begin(115200);
  driver.reads={{1,'z'}};CHECK(console.available()==1);console.end();
  console.begin(115200);CHECK(console.read()==-1);CHECK(driver.readCalls==2);
  console.end();
}
void clockHeapAndDelay(){
  reset();
  driver.timeUs=0;CHECK(RiscPlatform::millis()==0);
  driver.timeUs=999;CHECK(RiscPlatform::millis()==0);
  driver.timeUs=1000;CHECK(RiscPlatform::millis()==1);
  const uint64_t rollover=uint64_t(1)<<32;
  driver.timeUs=int64_t((rollover-1)*1000+999);
  CHECK(RiscPlatform::millis()==UINT32_MAX);
  const uint32_t before=RiscPlatform::millis();
  driver.timeUs=int64_t(rollover*1000);CHECK(RiscPlatform::millis()==0);
  driver.timeUs=int64_t((rollover+7)*1000+999);CHECK(RiscPlatform::millis()==7);
  CHECK(uint32_t(RiscPlatform::millis()-before)==8);
  driver.timeUs=int64_t((rollover*2+19)*1000);CHECK(RiscPlatform::millis()==19);
  driver.heapBytes=12345;CHECK(RiscPlatform::freeHeap()==12345);
  RiscPlatform::delay(0);RiscPlatform::delay(1);RiscPlatform::delay(9);
  RiscPlatform::delay(10);RiscPlatform::delay(25);
  CHECK(driver.delays==std::vector<TickType_t>({1,1,1,2,3}));
}
#if !RISC_CONSOLE_USB_SERIAL_JTAG
void uartConfigFailureDoesNotInstall(){
  reset();RiscPlatform::Console console;driver.configResult=ESP_ERR_INVALID_ARG;
  console.begin(115200);CHECK(driver.configCalls==1);CHECK(driver.installCalls==0);
  CHECK(!console);CHECK(console.available()==-1);console.end();
  CHECK(driver.uninstallCalls==0);driver.configResult=ESP_OK;
  console.begin(115200);CHECK(driver.configCalls==2);CHECK(driver.installCalls==1);
  console.end();
}
#endif
} // namespace

extern "C" {
int64_t esp_timer_get_time(void){return driver.timeUs;}
uint32_t esp_get_free_heap_size(void){return driver.heapBytes;}
void vTaskDelay(TickType_t ticks){driver.delays.push_back(ticks);}
BaseType_t xPortGetCoreID(void){return driver.core;}
#if RISC_CONSOLE_USB_SERIAL_JTAG
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t* config){
  CHECK(config);return install(config->rx_buffer_size,config->tx_buffer_size);
}
esp_err_t usb_serial_jtag_driver_uninstall(void){return uninstall();}
esp_err_t usb_serial_jtag_wait_tx_done(uint32_t ticks){CHECK(driver.installed && ticks==0 && driver.core==driver.installCore);return driver.txIdle?ESP_OK:ESP_ERR_TIMEOUT;}
bool usb_serial_jtag_is_driver_installed(void){return driver.installed;}
bool usb_serial_jtag_is_connected(void){++driver.connectedCalls;return driver.connected;}
int usb_serial_jtag_read_bytes(void* data,uint32_t length,uint32_t ticks){return readBytes(data,length,ticks);}
int usb_serial_jtag_write_bytes(const void* data,size_t count,uint32_t ticks){
  CHECK(ticks==0);return writeBytes(data,count);
}
#else
esp_err_t uart_param_config(uart_port_t port,const uart_config_t* config){
  CHECK(port==UART_NUM_0);CHECK(config);CHECK(!driver.installed);
  ++driver.configCalls;driver.uartConfig=*config;return driver.configResult;
}
esp_err_t uart_driver_install(uart_port_t port,int rx,int tx,int queueSize,
                              QueueHandle_t* queue,int flags){
  CHECK(port==UART_NUM_0);CHECK(queueSize==0);CHECK(!queue);CHECK(flags==0);
  return install(size_t(rx),size_t(tx));
}
esp_err_t uart_driver_delete(uart_port_t port){CHECK(port==UART_NUM_0);return uninstall();}
bool uart_is_driver_installed(uart_port_t port){CHECK(port==UART_NUM_0);return driver.installed;}
int uart_read_bytes(uart_port_t port,void* data,uint32_t length,uint32_t ticks){
  CHECK(port==UART_NUM_0);return readBytes(data,length,ticks);
}
int uart_tx_chars(uart_port_t port,const char* data,uint32_t count){
  CHECK(port==UART_NUM_0);return writeBytes(data,count);
}
#endif
}
#if RISC_CONSOLE_USB_SERIAL_JTAG
void wholeRecordCapacity(){
  reset();RiscPlatform::Console console;console.begin(115200);
  driver.txIdle=false;driver.connected=false;
  // Default, untraced native console accepts the complete 8 KiB burst.
  const std::string line(255,'s');
  for(unsigned i=0;i<32;++i){CHECK(console.availableForWrite()>=256);CHECK(console.println(line.c_str())==256);}
  CHECK(console.availableForWrite()==0);const auto calls=driver.writeCalls;
  const uint8_t byte=1;CHECK(console.write(&byte,1)==0 && driver.writeCalls==calls);
  CHECK(driver.accepted.size()==8192);
  // Connection is not completion. Keep credits consumed until the SDK drains.
  driver.connected=true;CHECK(console.availableForWrite()==0);
  driver.txIdle=true;CHECK(console.availableForWrite()==8192);
  driver.txIdle=false;CHECK(console.println(line.c_str())==256);
  CHECK(console.availableForWrite()==7936);
  // A failed uninstall retains pending bytes and capacity accounting.
  driver.uninstallResult=ESP_FAIL;console.end();CHECK(console.availableForWrite()==7936);
  driver.uninstallResult=ESP_OK;console.end();console.begin(115200);CHECK(console.availableForWrite()==8192);
  console.end();CHECK(driver.delays.empty());
}
#endif
int main(){
#if RISC_CONSOLE_USB_SERIAL_JTAG
  wholeRecordCapacity();
#endif
  stoppedIsInert();ownedStartupAndRestart();foreignControllerIsNotAdopted();
  failedInstallDoesNotGainOwnership();installRaceDoesNotAdoptForeignController();
  txConfigurationBoundaries();
  writesAreBoundedWithoutExplicitWait();writeErrorsAndPartialResults();
  printlnHonorsPartialFailures();readCacheAndErrors();
  wrongCoreCannotReleaseOwnership();failedUninstallRetainsOwnershipAndCache();
  successfulEndClearsCachedByte();clockHeapAndDelay();
#if !RISC_CONSOLE_USB_SERIAL_JTAG
  uartConfigFailureDoesNotInstall();
#endif
  CHECK(!driver.installed);
  std::puts(RISC_CONSOLE_USB_SERIAL_JTAG?
    "native platform: USB Serial/JTAG contract tests passed (real SDK headers)":
    "native platform: UART contract tests passed (real SDK headers)");
}
