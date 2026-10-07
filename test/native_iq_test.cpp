#ifdef IQ_PORT_LIFECYCLE
#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#endif
#include "ports/esp32s3/NativeRadioIq.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
esp_chip_info_t test_chip{CHIP_ESP32S3,CHIP_FEATURE_WIFI_BGN|CHIP_FEATURE_BLE};
#ifndef IQ_ROM_CHIP
#define IQ_ROM_CHIP CHIP_ESP32S3
#endif
#ifndef IQ_ROM_ECO
#define IQ_ROM_ECO 0
#endif
extern "C" { extern const uint32_t _rom_chip_id=IQ_ROM_CHIP; extern const int32_t _rom_eco_version=IQ_ROM_ECO; }
int test_wifi_status=ESP_ERR_WIFI_NOT_INIT;
static unsigned phy_enables=0,phy_disables=0,modem_refs=0,power_refs=0,phy_refs=0;
static unsigned resets=0,calibrations=0,wakeups=0;
static bool powered=false,calibrated=false,digital_valid=false,backup=false;
static uint32_t run=0,banks=0,reads=0;
static std::vector<std::string> calls,logs;
// Model IDF 4.4.7's distinct references and persistent calibration flag. This
// deliberately does not make esp_phy_enable implicitly power the modem: the old
// adapter fails here after a normal BT controller disable/deinit lifecycle.
extern "C" void esp_phy_modem_init(void){calls.push_back("modem-init");++modem_refs;}
extern "C" void esp_wifi_bt_power_domain_on(void){
 calls.push_back("power-on");assert(modem_refs && !phy_refs);
 if(!power_refs++){powered=true;digital_valid=false;++resets;}
}
extern "C" void esp_phy_enable(void){
 calls.push_back("phy-enable");assert(powered && power_refs && modem_refs && !phy_refs);
 ++phy_refs;++phy_enables;
 if(calibrated)++wakeups;else{calibrated=true;++calibrations;}
 digital_valid=true;
}
extern "C" void esp_phy_disable(void){
 calls.push_back("phy-disable");assert(powered && modem_refs && phy_refs==1 && digital_valid);
 assert(!(run&0x80000000u) && !(banks&0xfu));
 --phy_refs;++phy_disables;backup=true;
}
extern "C" void esp_wifi_bt_power_domain_off(void){
 calls.push_back("power-off");assert(power_refs && !phy_refs);
 if(!--power_refs){powered=false;digital_valid=false;}
}
extern "C" void esp_phy_modem_deinit(void){
 calls.push_back("modem-deinit");assert(modem_refs && !phy_refs && !power_refs);
 if(!--modem_refs)backup=false;
}
#if RISC_SLEEP_DIAGNOSTICS
namespace RiscDiagnostics { void line(const char* line){logs.emplace_back(line);} }
#endif
uint32_t iq_reg_read(uint32_t address){++reads;if(address==0x60033D5C)return run;assert(address==0x600C101C);return banks;}
static void released(){assert(!modem_refs && !power_refs && !phy_refs && !powered && !backup);}
static void bluetooth_cycle(){
 esp_phy_modem_init();esp_wifi_bt_power_domain_on();esp_phy_enable();
 esp_phy_disable();esp_wifi_bt_power_domain_off();esp_phy_modem_deinit();released();
}
int main(){
 using namespace RiscCpu::NativeRadioIq;
 if(IQ_ROM_CHIP!=CHIP_ESP32S3 || IQ_ROM_ECO<0){assert(!supported() && !ready() && !prepare() && !reads && calls.empty());puts("Native IQ: unsupported actual ROM rejected before MMIO PASS");return 0;}
#ifdef IQ_BAD_LAYOUT
 assert(supported() && !reserved() && !ready() && !prepare() && !reads && calls.empty());puts("Native IQ: static bank overlap rejected before MMIO PASS");return 0;
#endif
 assert(supported() && reserved() && ready());
 // Cold IQ use, then repeated use through SDK's persistent calibrated state.
 for(unsigned i=0;i<3;++i){
  calls.clear();assert(prepare() && phyPrepared && digital_valid && !prepare());
  assert((calls==std::vector<std::string>{"modem-init","power-on","phy-enable"}));
  const auto before=calls.size();
  run=0x80000000u;assert(!cleanup() && phyPrepared && calls.size()==before);run=0;
  for(unsigned bit=0;bit<4;++bit){banks=1u<<bit;assert(!cleanup() && phyPrepared && calls.size()==before);}
  banks=0;assert(cleanup() && !phyPrepared);released();
  assert((calls==std::vector<std::string>{"modem-init","power-on","phy-enable","phy-disable","power-off","modem-deinit"}));
  assert(cleanup() && calls.size()==6); // no double-decrements
 }
 assert(calibrations==1 && wakeups==2 && resets==3 && phy_enables==3 && phy_disables==3);
#if RISC_SLEEP_DIAGNOSTICS
 assert((logs==std::vector<std::string>{"RTE_IQ stage=modem-init","RTE_IQ stage=power-on","RTE_IQ stage=phy-enable","RTE_IQ stage=prepared","RTE_IQ stage=phy-disable","RTE_IQ stage=power-off","RTE_IQ stage=released"}));
#endif
 auto refused=[&](){const auto before=calls.size();assert(!prepare() && !phyPrepared && calls.size()==before);released();};
 test_wifi_status=0;reads=0;assert(!ready() && !reads);refused();test_wifi_status=-1;assert(!ready() && !reads);refused();test_wifi_status=ESP_ERR_WIFI_NOT_INIT;
 run=0x80000000;assert(!ready());refused();run=0;
 for(unsigned b=0;b<4;++b){banks=1u<<b;assert(!ready());refused();}
 banks=0x10;assert(ready());banks=0;
 test_chip.model=5;reads=0;assert(!ready() && !reads);refused();test_chip.model=CHIP_ESP32S3;
 test_chip.features=CHIP_FEATURE_WIFI_BGN;assert(!ready() && !reads);refused();test_chip.features|=CHIP_FEATURE_BLE;
 auto saved=reserved_region_risc_radio_iq;reserved_region_risc_radio_iq.end-=4;assert(!ready() && !reads);refused();reserved_region_risc_radio_iq=saved;
 assert(ready());
 // Reproduce a Bluetooth teardown before the first subsequent IQ burst: PHY is
 // already calibrated, its backup is freed, and the domain is powered off.
 bluetooth_cycle();const auto prior_wakeups=wakeups;
 assert(calibrated && !powered && !backup);
 assert(prepare() && powered && digital_valid && wakeups==prior_wakeups+1);
 assert(cleanup());released();
#ifdef IQ_PORT_LIFECYCLE
 // Drive the actual broker through the native adapter, including no-token
 // rollback and failed release retention, rather than replacing its callbacks.
 using namespace RiscCpu;
 static bool owner=true;
 Hardware h{};h.owner=[](){return owner;};
 h.radioIdle=[](){return true;};h.hciIdle=[](){return true;};h.hciSafe=[](){return true;};
 h.httpIdle=[](){return true;};h.httpSafe=[](){return true;};h.maintenanceIdle=[](){return true;};
 h.radioIqReady=ready;h.radioIqPrepare=prepare;h.radioIqCleanup=cleanup;
 Port p(h);auto& c=p.iq_;c.port=&p;
 for(unsigned cycle=0;cycle<3;++cycle){
  bluetooth_cycle();calls.clear();uint64_t token=99;
  owner=false;assert(!Port::radioIqClaim(&c,&token) && !token && calls.empty());owner=true;
  assert(Port::radioIqClaim(&c,&token) && token && phyPrepared && powered);
  uint64_t second=99;assert(!Port::radioIqClaim(&c,&second) && !second && calls.size()==3);
  owner=false;assert(!Port::radioIqRelease(&c,token) && phyPrepared);owner=true;
  assert(!Port::radioIqRelease(&c,token+1) && phyPrepared && calls.size()==3);
  banks=1;assert(!Port::radioIqRelease(&c,token) && c.token==token && c.closing && phyPrepared && powered && calls.size()==3);
  banks=0;assert(Port::radioIqRelease(&c,token) && !c.token && !c.closing && !phyPrepared);released();
  assert(!Port::radioIqRelease(&c,token) && calls.size()==6);
 }
 p.serial_=UINT64_MAX;calls.clear();uint64_t exhausted=99;
 assert(!Port::radioIqClaim(&c,&exhausted) && !exhausted && !c.token && !p.poisoned_ && !phyPrepared && calls.size()==6);released();
#endif
 puts("Native IQ: SDK modem/power/PHY balance, cold and post-Bluetooth claims, rejection and retained cleanup retry, bounded trace PASS");
}
