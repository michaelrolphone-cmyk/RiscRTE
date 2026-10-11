#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true, open_ok=true, close_ok=true, read_ok=true, write_ok=true, sync_ok=true, malformed=false;
static unsigned opens,closes,reads,writes,syncs;
static uint8_t media[64*512];
static Port* current;
static bool owner(){return owned;}
static Hardware hardware(){
 Hardware h{};h.owner=owner;
 h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};h.gpioClose=[](uint8_t){return true;};
 h.sdmmcOpen=[](uint8_t c,uint8_t m,uint8_t d,uint32_t hz,risc_sdmmc_card_info_v1* info){
  ++opens;assert(c==41 && m==42 && d==40 && hz==20000000 && current->transferring_);
  *info={sizeof(*info),malformed?513u:512u,64,20000000,0};return open_ok;
 };
 h.sdmmcClose=[](){++closes;assert(current->transferring_);return close_ok;};
 h.sdmmcRead=[](uint64_t lba,uint32_t n,void* out){++reads;assert(current->transferring_);if(!read_ok)return false;memcpy(out,media+lba*512,n*512);return true;};
 h.sdmmcWrite=[](uint64_t lba,uint32_t n,const void* in){++writes;assert(current->transferring_);if(!write_ok)return false;memcpy(media+lba*512,in,n*512);return true;};
 h.sdmmcSync=[](){++syncs;assert(current->transferring_);return sync_ok;};return h;
}
int main(){
 RiscBoot::Runtime runtime({});RiscBoot::Board::Device device{};
 strcpy(device.type,"gpio.bank");device.hardware.instance_id=5;
 device.config.gpio={sizeof(risc_hw_gpio_bank_v1),4,1,1,0,{5,41,42,40,0,0,0,0},0,0,0};
 Hardware old=hardware();old.sdmmcOpen=nullptr;Port legacy(old);Port::Gpio legacy_gpio{};
 assert(legacy.gpioScope(runtime,device,legacy_gpio));assert(!risc_gpio_sdmmc(&legacy_gpio.api.base));
 Port p(hardware());current=&p;auto& g=p.gpios_[0];assert(p.gpioScope(runtime,device,g));
 const auto* api=risc_gpio_sdmmc(&g.api.base);assert(api && api->context==&g);
 auto copy=g.api;copy.base.struct_size=sizeof(copy)-1;assert(!risc_gpio_sdmmc(&copy.base));copy=g.api;copy.sdmmc_tag=0;assert(!risc_gpio_sdmmc(&copy.base));
 copy=g.api;copy.sdmmc.read=nullptr;assert(!risc_gpio_sdmmc(&copy.base));
 uint64_t token=99; risc_sdmmc_card_info_v1 info{};
 assert(!api->open(&g,41,41,40,20000000,&token,&info) && !token && !opens);
 assert(!api->open(&g,41,42,39,20000000,&token,&info) && !opens);
 assert(!api->open(&g,41,42,40,40000000,&token,&info) && !opens);
 owned=false;assert(!api->open(&g,41,42,40,20000000,&token,&info) && !opens);owned=true;
 uint64_t pin=0;assert(g.api.base.claim(&g,42,true,false,false,&pin));
 assert(!api->open(&g,41,42,40,20000000,&token,&info) && !opens && !p.pins_[41].owner);
 assert(g.api.base.release(&g,pin));
 p.transferring_=true;assert(!api->open(&g,41,42,40,20000000,&token,&info));p.transferring_=false;
 assert(api->open(&g,41,42,40,20000000,&token,&info) && token && opens==1 && info.sector_count==64);
 assert(!p.quiescent() && p.providerStorageSafe() && p.appExitSafe());
 assert(!g.api.base.claim(&g,41,true,false,false,&pin));
 uint8_t bytes[4097],actual[4097];for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=uint8_t(i*31u+9u);
 assert(api->write(&g,token,56,8,bytes+1));assert(api->read(&g,token,56,8,actual+1));assert(!memcmp(bytes+1,actual+1,4096));
 const unsigned before=reads+writes;Port::Gpio foreign=g;
 assert(!api->read(&foreign,token,0,1,actual));assert(!api->read(&g,token+1,0,1,actual));
 assert(!api->read(&g,token,64,1,actual));assert(!api->read(&g,token,63,2,actual));
 assert(!api->read(&g,token,0,0,actual));assert(!api->read(&g,token,0,9,actual));assert(!api->read(&g,token,0,1,nullptr));
 assert(!api->read(&g,token,UINT64_MAX,1,actual));assert(!api->read(&g,token,0,1,reinterpret_cast<void*>(UINTPTR_MAX-3)));
 owned=false;assert(!api->read(&g,token,0,1,actual));assert(!api->release(&g,token));owned=true;
 assert(reads+writes==before && !closes);assert(api->sync(&g,token));
 close_ok=false;assert(!api->release(&g,token));assert(!p.providerStorageSafe() && !p.quiescent());
 assert(!api->read(&g,token,0,1,actual));assert(p.pins_[41].owner && p.pins_[42].owner && p.pins_[40].owner);
 close_ok=true;assert(api->release(&g,token) && p.quiescent());assert(!api->release(&g,token));
 uint64_t old_token=token;assert(api->open(&g,41,42,40,20000000,&token,&info) && token!=old_token);
 read_ok=false;memset(actual,0x55,sizeof(actual));assert(!api->read(&g,token,0,1,actual));assert(actual[0]==0x55 && !p.providerStorageSafe());
 assert(!api->write(&g,token,0,1,bytes));assert(api->release(&g,token));read_ok=true;
 for(unsigned which=0;which<4;++which){
  open_ok=which>=2;malformed=which>=2;close_ok=which%2==0;
  assert(!api->open(&g,41,42,40,20000000,&token,&info));
  if(close_ok)assert(!token && p.quiescent());else{assert(token && !p.quiescent());close_ok=true;assert(api->release(&g,token));}
 }
 open_ok=true;malformed=false;assert(api->open(&g,41,42,40,20000000,&token,&info));
 write_ok=false;assert(!api->write(&g,token,0,1,bytes));const auto failed_writes=writes;assert(!api->write(&g,token,0,1,bytes) && writes==failed_writes);assert(api->release(&g,token));write_ok=true;
 assert(api->open(&g,41,42,40,20000000,&token,&info));sync_ok=false;assert(!api->sync(&g,token));assert(api->release(&g,token));
 p.serial_=UINT64_MAX;assert(!api->open(&g,41,42,40,20000000,&token,&info) && !token);
 puts("SDMMC CPU port: legacy ABI, selected bank, pin conflicts, bounded copied API, owner/token checks, stale tokens, failed-open/read/write/sync cleanup and retained-release retry PASS");
}
