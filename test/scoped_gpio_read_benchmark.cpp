// Host CpuPort cost only. This neither measures nor claims physical SD speed.
#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
static volatile uint64_t nativeReads,nativeWrites;
#ifdef GPIO_READ_COUNT
static uint64_t probes;
extern "C" bool gpio_read_probe(bool matched){++probes;return matched;}
#endif
static bool owner(){return true;}
static bool readPin(uint8_t pin,bool*out){++nativeReads;*out=(pin&1)!=0;return true;}
static bool writePin(uint8_t,bool){++nativeWrites;return true;}
int main(int argc,char**argv){
 using namespace RiscCpu;Hardware h{};h.owner=owner;h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};h.gpioRead=readPin;h.gpioWrite=writePin;
 const char*mode=argc>1?argv[1]:"payload";bool cold=!strcmp(mode,"cold"),mixed=!strcmp(mode,"mixed"),collision=!strcmp(mode,"collision");
 Port p(h);auto&g=p.gpios_[0];g.port=&p;g.output=uint64_t(1)<<41;g.input=(uint64_t(1)<<40)|(uint64_t(1)<<42);
 uint64_t clk=0,data=0;assert(Port::gpioClaim(&g,41,true,false,false,&clk));assert(Port::gpioClaim(&g,40,false,false,false,&data));
 if(collision)p.serial_=data+63;
 uint64_t cmd=0;assert(Port::gpioClaim(&g,42,false,false,false,&cmd));
 std::array<double,7> elapsed{};constexpr size_t bytes=65536;bool level=false;
 for(auto&ms:elapsed){auto start=std::chrono::steady_clock::now();
  for(size_t n=0;n<bytes*8;++n){
   if(cold&&n%4096==0)memset(p.gpioWritePins_,0,sizeof(p.gpioWritePins_));
   const uint64_t selected=(mixed||collision)&&(n&1)?cmd:data;
   assert(Port::gpioRead(&g,selected,&level));assert(Port::gpioWrite(&g,clk,true));assert(Port::gpioRead(&g,clk,&level));
   assert(Port::gpioWrite(&g,clk,false));assert(Port::gpioRead(&g,clk,&level));
  }
  ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 }
 assert(nativeReads==elapsed.size()*bytes*24&&nativeWrites==elapsed.size()*bytes*16);
 std::sort(elapsed.begin(),elapsed.end());
 printf("mode=%s 65536 modeled bytes; 1572864 GPIO reads, 1048576 writes/trial; median %.3f ms (%.3f..%.3f); host scope/lookup overhead only\n",mode,elapsed[3],elapsed[0],elapsed[6]);
#ifdef GPIO_READ_COUNT
 printf("actual linear-search predicate evaluations=%llu across %llu native reads\n",(unsigned long long)probes,(unsigned long long)nativeReads);
#endif
}
