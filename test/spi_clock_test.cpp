#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true,endOk=true;static uint32_t rate=0;static unsigned begins=0,transfers=0;
static uint64_t now=0;
static bool spiBegin(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,uint32_t ms){assert(physical==2 && cs==7 && mode==0 && ms==8);rate=hz;++begins;return true;}
static bool spiTransfer(uint8_t,const uint8_t*,uint8_t*,size_t n,uint32_t ms){assert(n==480 && ms<=8);++transfers;return true;}
int main(){
  Hardware h{};h.owner=[](){return owned;};h.now=[](){return now;};h.spiBegin=spiBegin;h.spiTransfer=spiTransfer;
  h.spiEnd=[](uint8_t,uint8_t,uint32_t){return endOk;};
  Port p(h);auto& c=p.spis_[0];c.port=&p;c.physical=2;c.cs=7;c.token=42;c.bus.frequency_hz=40000000;c.bus.mode=0;
  // Exact admitted clock reaches the real CPU port; no silent 10 MHz clamp.
  assert(Port::spiBegin(&c,42,40000000,0,8) && rate==40000000 && begins==1);
  assert(!Port::spiBegin(&c,42,10000000,0,8)); // Held ownership is unchanged.
  uint8_t row[480]{};assert(Port::spiTransfer(&c,42,row,nullptr,sizeof(row)) && transfers==1);
  now=8;assert(!Port::spiTransfer(&c,42,row,nullptr,sizeof(row)));
  endOk=false;assert(!Port::spiEnd(&c,42) && p.spiBuses_[0].held==&c);
  endOk=true;assert(Port::spiEnd(&c,42) && !p.spiBuses_[0].held);
  assert(!Port::spiBegin(&c,42,40000001,0,8));assert(!Port::spiBegin(&c,41,40000000,0,8));
  assert(!Port::spiBegin(&c,42,40000000,1,8));owned=false;assert(!Port::spiBegin(&c,42,40000000,0,8));owned=true;
  c.bus.frequency_hz=10000000;assert(!Port::spiBegin(&c,42,40000000,0,8));
  assert(Port::spiBegin(&c,42,10000000,0,8) && rate==10000000 && begins==2);assert(Port::spiEnd(&c,42));
  puts("Actual CPU SPI: explicit 40MHz, legacy cap, owner/token/mode/deadline and failed-end retention PASS");
}
