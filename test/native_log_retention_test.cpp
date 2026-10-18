// Production Console, diagnostics and protocol. Only SDK transport is a finite host model.
// SDK function bodies below model finite BYTEBUF enqueue and IRQ drain; this
// does not execute SDK IRQ code, USB endpoints, FreeRTOS, or target instructions.
#include "ports/esp32s3/NativeSerialFiles.h"
#include "ports/esp32s3/SleepDiagnostics.h"
#include "ports/esp32s3/NativePlatform.h"
#include "NativeUsbTest.h"
#include <driver/usb_serial_jtag.h>
#include <hal/usb_serial_jtag_ll.h>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>

using namespace NativeUsbTest;
namespace N=RiscCpu::NativeSerialFiles;
namespace {
std::deque<uint8_t> rx,queued;
std::string wire,startup;
size_t txCapacity=0,rxCapacity=0;
unsigned rxReads=0,zeroWrites=0,polls=0,irqSteps=0,controlObservations=0;
size_t writeLimit=128;
bool inIsr=false;
bool owner(){return task==reinterpret_cast<void*>(1);}
bool ready(){return true;}
const std::string hello="RTEF1 14cc1e14 1 HELLO\n";
const std::string response="RTEF1 14cc1e14 1 0 HELLO 48 511 0\n";
void observe(uint32_t sid,uint32_t sequence,const char*op,uint32_t waited,bool deadline){
  assert(sid==0x14cc1e14 && sequence==1 && std::string(op)=="HELLO");
  assert(waited==0 && !deadline);++controlObservations;
}
void irqDrain(){
  ++irqSteps;
  for(size_t n=std::min(size_t(64),queued.size());n;--n){wire+=char(queued.front());queued.pop_front();}
}
void drainAll(){while(!queued.empty())irqDrain();}
void ownerPoll(){++polls;const auto bytes=queued.size();RiscDiagnostics::poll();assert(queued.size()<=bytes+64);N::controlCheckpoint();}

}
extern "C" {
int64_t esp_timer_get_time(){return now;}
uint32_t esp_get_free_heap_size(){return 100000;}
int esp_reset_reason(){return ESP_RST_POWERON;}
int esp_sleep_get_wakeup_cause(){return 0;}
void vTaskDelay(TickType_t ticks){now+=int64_t(ticks)*10000;}
BaseType_t xPortGetCoreID(){return core;}
TaskHandle_t xTaskGetCurrentTaskHandle(){return task;}
bool xPortInIsrContext(){return inIsr;}
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t* config){
  assert(!installed && config && config->tx_buffer_size==8192 && config->rx_buffer_size==256);
  txCapacity=config->tx_buffer_size;rxCapacity=config->rx_buffer_size;
  usb_serial_jtag_ll_phy_enable_external(false);usb_serial_jtag_ll_phy_enable_pad(true);
  installed=true;installedCore=core;++installs;return ESP_OK;
}
esp_err_t usb_serial_jtag_driver_uninstall(){
  assert(installed && installedCore==core);installed=false;++uninstalls;queued.clear();rx.clear();return ESP_OK;
}
bool usb_serial_jtag_is_driver_installed(){return installed;}
bool usb_serial_jtag_is_connected(){return host;}
int usb_serial_jtag_read_bytes(void* data,uint32_t length,uint32_t ticks){
  assert(installed && length==1 && ticks==0);++rxReads;
  if(rx.empty())return 0;
  *static_cast<uint8_t*>(data)=rx.front();rx.pop_front();return 1;
}
int usb_serial_jtag_write_bytes(const void* data,size_t length,uint32_t ticks){
  assert(installed && core==installedCore && data && length && length<=128 && ticks==0);++writes;
  // The pinned driver submits an entire chunk via xRingbufferSend, never a
  // partial chunk. No worker drains implicitly while the test calls write().
  length=std::min(length,writeLimit);
  if(!length || length>txCapacity-queued.size()){++zeroWrites;return 0;}
  const auto* p=static_cast<const uint8_t*>(data);queued.insert(queued.end(),p,p+length);return int(length);
}
}
int main(int argc,char**argv){
  assert(argc==2);const std::string scenario=argv[1];
  assert(RiscDiagnostics::prepareSerial());RiscPlatform::console.begin(115200);
  RiscDiagnostics::start();host=true;
  N::Admission admission{nullptr,0,ready};assert(N::prepare(&admission,owner,ready,observe));
  auto pump=[](unsigned count){for(unsigned i=0;i<count;++i){ownerPoll();irqDrain();}};
  if(scenario=="long-line"){
    const std::string shortLine(127,'a'),longLine(128,'b'),maximum(255,'c');
    for(const auto& line:{shortLine,longLine,maximum})RiscDiagnostics::line(line.c_str());
    pump(1000);drainAll();
    std::printf("long-line expected=513 actual=%zu\n",wire.size());std::fflush(stdout);
    assert(wire==shortLine+'\n'+longLine+'\n'+maximum+'\n');
  }else if(scenario=="full-tx-ingress"){
    const uint8_t bytes[128]={};
    for(unsigned i=0;i<64;++i)assert(RiscPlatform::console.write(bytes,128)==128);
    RiscDiagnostics::line("diagnostic while full");
    rx.insert(rx.end(),hello.begin(),hello.end());
    ownerPoll();
    std::printf("full-tx ingress remaining=%zu hello_commits=%u\n",rx.size(),controlObservations);std::fflush(stdout);
    assert(rx.empty() && controlObservations==1);
    pump(1000);drainAll();assert(wire.find(response)!=std::string::npos);
  }else if(scenario=="partial-priority"){
    const std::string first(199,'d'),last="after reply";
    writeLimit=17;RiscDiagnostics::line(first.c_str());ownerPoll();
    rx.insert(rx.end(),hello.begin(),hello.end());RiscDiagnostics::line(last.c_str());
    ownerPoll();assert(rx.empty() && controlObservations==1);
    pump(1000);drainAll();
    assert(wire==first+'\n'+response+last+'\n');
  }else if(scenario=="disconnected-burst"){
    host=false;std::string expected;
    for(unsigned i=0;i<(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY?150u:20u);++i){auto line="startup-"+std::to_string(i)+std::string(120,'s');RiscDiagnostics::line(line.c_str());expected+=line+'\n';}
    pump(10);assert(wire.empty());host=true;pump(2000);drainAll();assert(wire==expected);
  }else if(scenario=="reconnect-partial"){
    const std::string first(199,'p'),last="retained after disconnect";
    writeLimit=17;RiscDiagnostics::line(first.c_str());ownerPoll();drainAll();
    assert(wire==first.substr(0,17));host=false;ownerPoll();
    RiscDiagnostics::line(last.c_str());pump(10);host=true;wire.clear();
    rx.insert(rx.end(),hello.begin(),hello.end());ownerPoll();assert(rx.empty() && controlObservations==1);
    pump(1000);drainAll();assert(wire=="\n"+response+first+'\n'+last+'\n');
  }else if(scenario=="queue-wrap"){
    std::string expected;
    for(unsigned i=0;i<1000;++i){auto line=std::to_string(i)+std::string(95,'w');RiscDiagnostics::line(line.c_str());expected+=line+'\n';pump(4);}
    pump(1000);drainAll();assert(wire==expected);
  }else if(scenario=="foreign-owner"){
    task=reinterpret_cast<void*>(2);RiscDiagnostics::line("foreign");RiscDiagnostics::poll();task=reinterpret_cast<void*>(1);
    inIsr=true;RiscDiagnostics::line("isr");RiscDiagnostics::poll();inIsr=false;
    pump(10);drainAll();assert(wire.empty());
  }else if(scenario=="recovery-retention"){
    host=false;RiscDiagnostics::line("before recovery");RiscDiagnostics::lightReturn(ESP_OK,4);
    RiscDiagnostics::line("during recovery");RiscDiagnostics::poll();now+=20000;RiscDiagnostics::poll();
    host=true;pump(1000);drainAll();assert(wire=="before recovery\nduring recovery\n");
  }else if(scenario=="overflow"){
    host=false;for(unsigned i=0;i<1000;++i)RiscDiagnostics::line(std::string(255,'z').c_str());
    host=true;rx.insert(rx.end(),hello.begin(),hello.end());ownerPoll();assert(rx.empty() && controlObservations==1);
    pump(2000);drainAll();assert(wire.find(response)!=std::string::npos);
    assert(wire.find("RTE_LOG lost=")!=std::string::npos);
    assert(wire.size()<40000);
  }else assert(false);
  std::printf("PASS %s wire=%zu polls=%u writes=%u zero=%u\n",scenario.c_str(),wire.size(),polls,writes,zeroWrites);
}
