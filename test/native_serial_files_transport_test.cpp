#include "ports/esp32s3/NativeSerialFiles.cpp" // Inspect real workspace lifetime without a firmware test hook.
#include "ports/esp32s3/SleepDiagnostics.h"
#include "ports/esp32s3/NativePlatform.h"
#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include "runtime/storage/PosixFiles.h"
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
std::deque<uint8_t> rx;
std::string tx;
size_t maxWrite=64;
bool inIsr=false,settled=false,ready=true,platformSafe=true,uncertainClose=false;
unsigned providerCalls=0,closeCalls=0;
risc_storage_files_v1 real{};
bool owner(){return task==reinterpret_cast<void*>(1) && !inIsr;}
bool safe(){return platformSafe;}
void entered(){assert(settled && owner());++providerCalls;}
void checkpoint(){settled=true;N::checkpoint();settled=false;}
void feed(const std::string& text){for(const auto c:text)rx.push_back(uint8_t(c));}
void parse(){
  const auto before=providerCalls;
  for(unsigned i=0;!rx.empty() && i<100;++i)RiscDiagnostics::poll();
  assert(rx.empty() && providerCalls==before);
}
void drain(){
  const auto before=providerCalls;
  for(unsigned i=0;N::outputSize() && i<1000;++i)RiscDiagnostics::poll();
  assert(!N::outputSize() && providerCalls==before);
}
std::string command(const std::string& text){
  const auto at=tx.size();feed(text+"\n");parse();checkpoint();drain();return tx.substr(at);
}
void expect(const std::string& text,const std::string& response){assert(command(text)==response);}
struct DeepEntered {};
static void idleLightDeep(unsigned kind) {
  RiscCpu::Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};
  h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};
  h.gpioRead=[](uint8_t,bool*level){*level=true;return true;};
  h.deepWakeValid=[](uint8_t){return true;};h.deepReady=[](){return true;};
  h.deepWakeArm=[](uint8_t,bool,bool){return true;};h.deepWakeClear=[](uint8_t,bool){return true;};
  h.deepWakeSetValid=[](uint64_t,uint64_t){return true;};
  h.deepWakeSetArm=[](uint64_t,uint64_t,uint64_t){return true;};
  h.deepWakeSetClear=[](uint64_t,uint64_t,uint64_t){return true;};
  h.timerArm=[](uint32_t){return true;};h.timerClear=[](){return true;};
  h.deepSleep=[](){throw DeepEntered{};};
  RiscCpu::Port p(h);auto& g=p.gpios_[0];g.port=&p;g.input=uint64_t(1)<<7;
  uint64_t token=0;assert(RiscCpu::Port::gpioClaim(&g,7,false,false,false,&token));
  const auto before=providerCalls;RiscDiagnostics::lightReturn(ESP_OK,4);
  assert(providerCalls==before);int32_t rc=0;bool entered=false;
  try {
    rc=kind==0?RiscCpu::Port::gpioDeepSleep(&g,token,false):kind==1?
      RiscCpu::Port::gpioDeepSleepFor(&g,token,false,1000):RiscCpu::Port::gpioDeepSleepSet(&g,token,false,0);
  }catch(const DeepEntered&){entered=true;}
  std::printf("idle Light -> Deep kind=%u host=%d pending=%d exit_safe=%d result=%d terminal=%d provider_calls=%u\n",
    kind,host,N::pending(),N::exitSafe(),rc,entered,providerCalls-before);std::fflush(stdout);
  assert(entered && N::exitSafe() && providerCalls==before);
}
void sleepGuards(int32_t expected){
  RiscCpu::Hardware hardware{};hardware.owner=owner;hardware.now=[]()->uint64_t{return 0;};
  RiscCpu::Port port(hardware);auto& gpio=port.gpios_[0];gpio.port=&port;
  risc_light_sleep_result_v1 result{sizeof(result),0};
  assert(RiscCpu::Port::gpioLightSleep(&gpio,1,false,&result)==expected);
  assert(RiscCpu::Port::gpioLightSleepFor(&gpio,1,false,10,&result)==expected);
  assert(RiscCpu::Port::gpioDeepSleep(&gpio,1,false)==expected);
  assert(RiscCpu::Port::gpioDeepSleepFor(&gpio,1,false,10)==expected);
  assert(RiscCpu::Port::gpioLightSleepSet(&gpio,1,false,10,&result)==expected);
  assert(RiscCpu::Port::gpioDeepSleepSet(&gpio,1,false,10)==expected);
  assert(!port.restartResourcesSafe());
  // A healthy transfer is independent native custody, so ordinary resident
  // callback and app checkpoints are not falsely made terminal.
  if(expected==RISC_LIGHT_SLEEP_BUSY)assert(port.appExitSafe() && port.providerStorageSafe());
}
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
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t*config){
  assert(!installed && config && config->rx_buffer_size==256 && config->tx_buffer_size>=256);
  ++installs;usb_serial_jtag_ll_phy_enable_external(false);usb_serial_jtag_ll_phy_enable_pad(true);
  installed=true;installedCore=core;return ESP_OK;
}
esp_err_t usb_serial_jtag_driver_uninstall(){assert(installed && installedCore==core);++uninstalls;installed=false;return ESP_OK;}
bool usb_serial_jtag_is_driver_installed(){return installed;}
bool usb_serial_jtag_is_connected(){return host;}
int usb_serial_jtag_read_bytes(void* data,uint32_t length,uint32_t ticks){
  assert(installed && length==1 && ticks==0);
  if(rx.empty())return 0;
  *static_cast<uint8_t*>(data)=rx.front();rx.pop_front();return 1;
}
int usb_serial_jtag_write_bytes(const void* data,size_t length,uint32_t ticks){
  assert(installed && length<=64 && ticks==0);++writes;
  length=std::min(length,maxWrite);tx.append(static_cast<const char*>(data),length);return int(length);
}
}
int main(int argc,char**argv){
  assert(argc>=2);const std::string root=argv[1];
  // Guard every public entry before construction, even over poisoned raw BSS.
  std::memset(N::workspaceStorage,0xa5,sizeof(N::workspaceStorage));
  auto inert=[](){
    N::input('x');N::disconnect();N::consumeOutput(99);
    assert(N::checkpoint() && N::shutdown() && !N::pending());
    assert(!N::output() && !N::outputSize() && N::exitSafe() && N::custodySafe());
  };
  inert();
  task=reinterpret_cast<void*>(2);assert(!N::prepare(nullptr,owner,safe));task=reinterpret_cast<void*>(1);
  inIsr=true;assert(!N::prepare(nullptr,owner,safe));inIsr=false;
  for(auto byte:N::workspaceStorage)assert(byte==0xa5);
  RiscPlatform::console.begin(115200);RiscDiagnostics::start();host=true;
  if(argc==3){
    const std::string scenario=argv[2];
    RiscBoot::RuntimeFiles unavailable;
    RiscSerial::Namespace invalidMap[]={{1,"valid",&unavailable,true},{1,"duplicate",&unavailable,true}};
    N::Admission invalid{invalidMap,2,[](){return true;}};
    if(scenario=="malformed")invalid.count=RiscSerial::Files::NamespaceMax+1;
    if(scenario=="bad-label")invalidMap[0].label="";
    assert(N::prepare(scenario=="absent"?nullptr:&invalid,owner,safe)==(scenario=="absent"));
    unsigned char failed[sizeof(N::workspaceStorage)];std::memcpy(failed,N::workspaceStorage,sizeof(failed));
    assert(!N::prepare(nullptr,owner,safe));
    feed("RTEF1 12345678 1 HELLO\n");parse();checkpoint();
    assert(tx.empty() && N::exitSafe() && N::custodySafe());
    inert();assert(!std::memcmp(failed,N::workspaceStorage,sizeof(failed)));
    if(scenario=="absent" || scenario=="malformed")for(auto byte:N::workspaceStorage)assert(byte==0xa5);
    puts("Native serial absent/failed admission remains inert, one-shot and unchanged PASS");return 0;
  }
  RiscStorage::PosixFiles provider(root.c_str());real=*provider.api();auto table=real;
  table.open=[](void*c,const char*p,uint32_t f,uint32_t*h){entered();N::checkpoint();return real.open(c,p,f,h);};
  table.close=[](void*c,uint32_t h){entered();++closeCalls;const int32_t rc=real.close(c,h);return uncertainClose?int32_t(RISC_FILES_IO):rc;};
  table.write=[](void*c,uint32_t h,const void*b,uint32_t n,uint32_t*a){entered();return real.write(c,h,b,n,a);};
  table.sync=[](void*c,uint32_t h){entered();return real.sync(c,h);};
  RiscBoot::RuntimeFiles files,unavailable;assert(files.bind(&table,owner,safe));
  char label[]="exact-files";
  RiscSerial::Namespace namespaces[]={{42,label,&files,true},{77,"unavailable",&unavailable,false}};
  N::Admission admission{namespaces,2,[](){return ready;}};
  ready=false; // Metadata-only admission is independent of dispatch readiness.
  assert(N::prepare(&admission,owner,safe));assert(!N::prepare(&admission,owner,safe));
  assert(reinterpret_cast<uintptr_t>(&N::workspace())%alignof(N::Workspace)==0);
  assert(!N::workspace().endpoint.pending() && !N::workspace().endpoint.outputSize());
  for(size_t i=admission.count;i<RiscSerial::Files::NamespaceMax;++i){
    assert(N::workspace().namespaces[i].id==0 && !N::workspace().namespaces[i].files);
    for(auto byte:N::workspace().labels[i])assert(byte==0);
  }
  // Foreign-task/ISR inspection must reject before reading PSRAM at all. Poison
  // the live representation while inaccessible, then restore it for owner use.
  unsigned char live[sizeof(N::workspaceStorage)];std::memcpy(live,N::workspaceStorage,sizeof(live));
  std::memset(N::workspaceStorage,0xa5,sizeof(N::workspaceStorage));
  for(unsigned mode=0;mode<2;++mode){
    task=reinterpret_cast<void*>(mode?1:2);inIsr=bool(mode);
    assert(!N::checkpoint() && !N::shutdown() && N::pending());
    assert(!N::exitSafe() && !N::custodySafe() && !N::output() && !N::outputSize());
    N::input('x');N::disconnect();N::consumeOutput(99);
    for(auto byte:N::workspaceStorage)assert(byte==0xa5);
  }
  task=reinterpret_cast<void*>(1);inIsr=false;std::memcpy(N::workspaceStorage,live,sizeof(live));
  settled=true;assert(N::shutdown());settled=false;
  assert(providerCalls==0 && N::exitSafe());ready=true;
  host=false;for(unsigned kind=0;kind<3;++kind)idleLightDeep(kind);
  host=true;for(unsigned kind=0;kind<3;++kind)idleLightDeep(kind);
  // The modeled terminal exits leave USB recovery requested; finish that
  // independent RAM/transport state before the wire-protocol cases below.
  RiscDiagnostics::poll();now+=20000;RiscDiagnostics::poll();
  // A live admitted session is bounded RAM custody even after all response
  // bytes have been handed to USB. Cable presence alone above remains idle.
  auto sessionHeld=[&](){
    assert(!N::pending()&&!N::exitSafe()&&N::custodySafe());
    sleepGuards(RISC_LIGHT_SLEEP_BUSY);
#if RISC_ENABLE_USB_PHY
    const auto released=uninstalls;assert(!RiscDiagnostics::suspendUsbPhy());
    assert(uninstalls==released);
#endif
  };
  expect("RTEF1 42345678 1 HELLO","RTEF1 42345678 1 0 HELLO 48 511 2\n");sessionHeld();
  expect("RTEF1 42345678 2 NS 0","RTEF1 42345678 2 0 NS 42 1 65786163742d66696c6573\n");sessionHeld();
  expect("RTEF1 42345678 3 NS 1","RTEF1 42345678 3 0 NS 77 0 756e617661696c61626c65\n");sessionHeld();
  expect("RTEF1 42345678 4 PATH 0 2f676170","RTEF1 42345678 4 0 PATH 4\n");sessionHeld();
  expect("RTEF1 42345678 5 PUT 42 create 0","RTEF1 42345678 5 0 PUT\n");
  expect("RTEF1 42345678 6 COMMIT 00000000","RTEF1 42345678 6 0 COMMIT\n");sessionHeld();
  expect("RTEF1 42345678 7 GET 42","RTEF1 42345678 7 0 GET 0\n");
  expect("RTEF1 42345678 8 CLOSE","RTEF1 42345678 8 0 CLOSE 00000000\n");sessionHeld();
  expect("RTEF1 42345678 9 ABORT","RTEF1 42345678 9 0 ABORT\n");sessionHeld();
  const auto expiryCalls=providerCalls;ready=false;
  now+=int64_t(RiscSerial::Files::TimeoutMs-1)*1000;N::controlCheckpoint();sessionHeld();
  now+=1000;N::controlCheckpoint();assert(N::exitSafe()&&providerCalls==expiryCalls);ready=true;
  expect("RTEF1 52345678 1 HELLO","RTEF1 52345678 1 0 HELLO 48 511 2\n");sessionHeld();
  N::disconnect();assert(N::exitSafe());
  // The production64-byte RX budget may split a valid121-byte PATH. No
  // readiness/provider callback is needed even at a one-byte transport cadence.
  for(unsigned chunk : {1u,5u,63u,64u,65u,128u}) {
    ready=false;unsigned seq=0;
    for(const std::string &op : {std::string("HELLO"),std::string("NS 0"),std::string("NS 1"),std::string("PATH 0 ")+std::string(96,'6')}) {
      const std::string frame="RTEF1 62345678 "+std::to_string(++seq)+" "+op+"\n";
      const auto at=tx.size();const auto before=providerCalls;
      for(size_t k=0;k<frame.size();k+=chunk) {
        feed(frame.substr(k,chunk));
        while(!rx.empty()){RiscDiagnostics::poll();N::controlCheckpoint();now+=16000;}
      }
      N::controlCheckpoint();drain();assert(providerCalls==before);
      assert(tx.substr(at).rfind("RTEF1 62345678 "+std::to_string(seq)+" 0 ",0)==0);sessionHeld();
    }
    N::disconnect();assert(N::exitSafe());
  }
  ready=true;
  puts("Native serial session sleep/USB/restart custody, bounded expiry and split PATH PASS");
  const auto previousProviderCalls=providerCalls;
  label[0]='X';namespaces[0].id=99; // Admission owns copied immutable metadata.
  expect("RTEF1 11345678 1 HELLO","RTEF1 11345678 1 0 HELLO 48 511 2\n");
  expect("RTEF1 11345678 2 NS 1","RTEF1 11345678 2 0 NS 77 0 756e617661696c61626c65\n");
  expect("RTEF1 11345678 3 PATH 0 2f66696c65","RTEF1 11345678 3 0 PATH 5\n");
  expect("RTEF1 11345678 4 STAT 77","RTEF1 11345678 4 "+std::to_string(RISC_FILES_UNAVAILABLE)+" STAT 0 0\n");
  assert(providerCalls==previousProviderCalls); // An unavailable session never falls back to another namespace.
  expect("RTEF1 12345678 1 HELLO","RTEF1 12345678 1 0 HELLO 48 511 2\n");
  expect("RTEF1 12345678 2 NS 0","RTEF1 12345678 2 0 NS 42 1 65786163742d66696c6573\n");
  expect("RTEF1 12345678 3 PATH 0 2f66696c65","RTEF1 12345678 3 0 PATH 5\n");
  const auto before=providerCalls;
  feed("RTEF1 12345678 4 PUT 42 create 3\n");parse();assert(N::pending());
  assert(providerCalls==before && !N::exitSafe());
  ready=false;checkpoint();assert(providerCalls==before && N::pending());ready=true;
  task=reinterpret_cast<void*>(2);checkpoint();assert(providerCalls==before);task=reinterpret_cast<void*>(1);
  inIsr=true;checkpoint();assert(providerCalls==before);inIsr=false;
  checkpoint();drain();assert(providerCalls==before+1 && !N::exitSafe() && N::custodySafe());
  sleepGuards(RISC_LIGHT_SLEEP_BUSY);
#if RISC_ENABLE_USB_PHY
  const auto releases=uninstalls;assert(!RiscDiagnostics::suspendUsbPhy());
  assert(uninstalls==releases && RiscDiagnostics::usbPhyIdle());
#endif
  auto at=tx.size();feed("RTEF1 12345678 5 WRITE 0 616263\n");parse();checkpoint();
  maxWrite=0;RiscDiagnostics::poll();assert(tx.size()==at && N::outputSize());
  RiscDiagnostics::line("must not split protocol");assert(tx.size()==at);
  maxWrite=3;drain();maxWrite=64;
  assert(tx.substr(at)=="RTEF1 12345678 5 0 WRITE 3\n");
  const auto closes=closeCalls;host=false;RiscDiagnostics::poll();
  assert(closeCalls==closes && !N::exitSafe());checkpoint();assert(closeCalls==closes+1 && N::exitSafe());
  for(unsigned i=0;i<8;++i)RiscDiagnostics::poll();assert(N::exitSafe());
  host=true;RiscDiagnostics::poll();
  // A recognized but incomplete frame has bounded RAM custody too.
  feed("RTEF1 ");parse();assert(N::pending());
  now+=int64_t(RiscSerial::Files::TimeoutMs)*1000;checkpoint();assert(N::exitSafe());
  const auto beforeRecovery=providerCalls;
  RiscDiagnostics::lightReturn(ESP_OK,4);assert(providerCalls==beforeRecovery);
  RiscDiagnostics::poll();assert(!installed);
  now+=20000;RiscDiagnostics::poll();assert(installed);checkpoint();assert(N::exitSafe());
#if RISC_ENABLE_USB_PHY
  assert(RiscDiagnostics::suspendUsbPhy() && !installed);
  assert(RiscDiagnostics::resumeUsbPhy() && installed);checkpoint();assert(N::exitSafe());
#endif
#if RISC_SLEEP_DIAGNOSTICS
  at=tx.size();feed("diag\n");parse();
  for(unsigned i=0;i<100;++i)RiscDiagnostics::poll();
  assert(tx.find("RTE_DIAG begin",at)!=std::string::npos && tx.find("RTE_DIAG end",at)!=std::string::npos);
#endif
  expect("RTEF1 22345678 1 HELLO","RTEF1 22345678 1 0 HELLO 48 511 2\n");
  expect("RTEF1 22345678 2 PATH 0 2f6661696c","RTEF1 22345678 2 0 PATH 5\n");
  expect("RTEF1 22345678 3 PUT 42 create 0","RTEF1 22345678 3 0 PUT\n");
  uncertainClose=true;
  settled=true;assert(!N::shutdown());settled=false;
  assert(!N::custodySafe() && !N::exitSafe());sleepGuards(RISC_LIGHT_SLEEP_RETAINED);
  const auto terminalCalls=providerCalls;
  unsigned char retained[sizeof(N::workspaceStorage)];
  std::memcpy(retained,N::workspaceStorage,sizeof(retained));
  assert(!N::prepare(&admission,owner,safe));
  assert(!std::memcmp(retained,N::workspaceStorage,sizeof(retained)));
  platformSafe=false; // Terminal RAM status replies must not require I/O permission.
  const auto terminal=command("RTEF1 32345678 1 HELLO");
  assert(terminal=="RTEF1 32345678 1 "+std::to_string(RISC_FILES_RETAINED)+"\n");
  for(unsigned i=0;i<8;++i){checkpoint();settled=true;assert(!N::shutdown());settled=false;RiscDiagnostics::poll();}
  assert(providerCalls==terminalCalls);
  puts("Native serial actual Console/diagnostics: single RX, parse-only yield, immutable admission, bounded partial TX, disconnect close, sleep/USB/restart fences and no retained retry PASS");
}
