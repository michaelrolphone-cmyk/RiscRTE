#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true,joinOk=true,scanOk=true,cleanOk=true,idle=true,malformed=false;
static unsigned joins=0,scans=0,closes=0;
int main(){
 Hardware h{};h.owner=[](){return owned;};h.radioIdle=[](){return idle;};
 h.radioJoin=[](const char* ssid,const char* pass){assert(!strcmp(ssid,"test") && !strcmp(pass,"test-pass"));++joins;idle=false;return joinOk;};
 h.radioState=[](uint8_t* state,int8_t* rssi){*state=2;*rssi=-42;return true;};
 h.radioLeave=[](){++closes;if(cleanOk)idle=true;return cleanOk;};
 h.radioAddresses=[](uint8_t* station,uint8_t*){station[0]=10;return true;};
 h.radioScanStart=[](){++scans;idle=false;return scanOk;};
 h.radioScanPoll=[](garden_radio_scan_result_v1* out){out->state=GARDEN_RADIO_SCAN_DONE;out->count=malformed?17:1;strcpy(out->entries[0].ssid,"test");return true;};
 h.radioScanCancel=h.radioLeave;
 Port p(h);auto& c=p.radios_[0];c.port=&p;c.config={sizeof(c.config),0,3};
 uint64_t token=99;
 owned=false;assert(!Port::radioClaim(&c,&token) && !token);owned=true;
 assert(Port::radioClaim(&c,&token) && token);const uint64_t first=token;
 assert(!joins && !scans && !closes && p.appExitSafe() && !p.quiescent());
 uint64_t other=0;assert(!Port::radioClaim(&c,&other) && !other);
 assert(!Port::radioJoin(&c,token+1,"test","test-pass"));
 assert(!Port::radioJoin(&c,token,"","test-pass"));
 assert(!Port::radioJoin(&c,token,"test","short"));
 char longSsid[34];memset(longSsid,'s',33);longSsid[33]=0;
 assert(!Port::radioJoin(&c,token,longSsid,"test-pass") && !joins);
 assert(!Port::radioStartAp(&c,token,"test","test-pass",nullptr,nullptr,nullptr));
 assert(Port::radioStopAp(&c,token));
 assert(Port::radioJoin(&c,token,"test","test-pass") && !p.appExitSafe() && p.providerStorageSafe());
 assert(!Port::radioScanStart(&c,token) && !Port::radioJoin(&c,token,"test","test-pass"));
 uint8_t state=0;int8_t rssi=0;assert(Port::radioState(&c,token,&state,&rssi) && state==2 && rssi==-42);
 uint8_t station[12]{},ap[12]{};assert(Port::radioAddresses(&c,token,station,ap) && station[0]==10 && !ap[0]);
 // Scanning cleanup cannot unexpectedly disconnect an unrelated join.
 assert(Port::radioScanCancel(&c,token) && !idle);
 auto& g=p.gpios_[0];g.port=&p;p.pins_[4]={&g,90,false};risc_light_sleep_result_v1 wake{sizeof(wake),0};
 assert(Port::gpioLightSleep(&g,90,false,&wake)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_BUSY);
 cleanOk=false;assert(!Port::radioLeave(&c,token) && c.token==token && c.closing && !p.appExitSafe() && !p.providerStorageSafe());
 assert(Port::gpioLightSleep(&g,90,false,&wake)==RISC_LIGHT_SLEEP_RETAINED);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_RETAINED);
 assert(!Port::radioState(&c,token,&state,&rssi) && state==0 && rssi==-127);
 assert(!Port::radioJoin(&c,token,"test","test-pass"));
 cleanOk=true;assert(Port::radioLeave(&c,token) && p.appExitSafe() && idle);
 // The still-owned idle claim no longer blocks Light/Deep platform validation.
 assert(Port::gpioLightSleep(&g,90,false,&wake)==RISC_LIGHT_SLEEP_UNSUPPORTED);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_UNSUPPORTED);p.pins_[4]={};
 assert(Port::radioScanStart(&c,token) && scans==1);
 garden_radio_scan_result_v1 results{};results.struct_size=sizeof(results)-1;
 assert(!Port::radioScanPoll(&c,token,&results));results.struct_size=sizeof(results);
 assert(Port::radioScanPoll(&c,token,&results) && results.count==1 && !strcmp(results.entries[0].ssid,"test"));
 malformed=true;assert(!Port::radioScanPoll(&c,token,&results) && results.count==0);malformed=false;
 cleanOk=false;assert(!Port::radioScanCancel(&c,token) && c.scanning && !p.appExitSafe());
 cleanOk=true;assert(Port::radioScanCancel(&c,token) && !c.scanning && p.appExitSafe());
 joinOk=false;assert(!Port::radioJoin(&c,token,"test","test-pass") && p.appExitSafe());
 cleanOk=false;assert(!Port::radioJoin(&c,token,"test","test-pass") && c.closing && !p.appExitSafe() && !p.providerStorageSafe());
 assert(!Port::radioRelease(&c,token));cleanOk=true;assert(Port::radioRelease(&c,token) && p.quiescent());
 assert(!Port::radioRelease(&c,token));assert(Port::radioClaim(&c,&token) && token>first);
 scanOk=false;assert(!Port::radioScanStart(&c,token) && !c.active && p.appExitSafe());
 owned=false;assert(!Port::radioRelease(&c,token));owned=true;assert(Port::radioRelease(&c,token));
 p.serial_=UINT64_MAX;assert(!Port::radioClaim(&c,&token) && !token);
 puts("CPU radio: exclusive logical claim, exact bounds, scan/cancel, failed cleanup/retry, idle handoffs and Light/Deep retention PASS");
}
