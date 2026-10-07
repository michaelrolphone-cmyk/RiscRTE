// Informational host timing only; never a physical panel throughput claim.
#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
extern bool benchmarkNativeWrite(uint8_t,bool);
extern void benchmarkNativeSetup(unsigned);
extern uint64_t benchmarkNativeWrites();
static bool owner(){return true;}
int main(int argc,char**argv){
 constexpr size_t frameBytes=120000,writesPerFrame=frameBytes*24;
 RiscCpu::Hardware hw{};hw.owner=owner;hw.gpioOpen=[](uint8_t,bool,bool,bool){return true;};hw.gpioWrite=benchmarkNativeWrite;
 RiscCpu::Port p(hw);auto& g=p.gpios_[0];g.port=&p;g.output=(uint64_t(1)<<11)|(uint64_t(1)<<12);
 uint64_t mosi=0,sclk=0;assert(RiscCpu::Port::gpioClaim(&g,11,true,false,false,&mosi));assert(RiscCpu::Port::gpioClaim(&g,12,true,false,false,&sclk));
 garden_gpio_v1 api{};api.context=&g;api.write=RiscCpu::Port::gpioWrite;
 benchmarkNativeSetup(argc>1?unsigned(std::strtoul(argv[1],nullptr,10)):15);
 std::array<double,9> elapsed{};
 for(auto& ms:elapsed){
  const auto start=std::chrono::steady_clock::now();
  for(size_t n=0;n<frameBytes;++n)for(int bit=7;bit>=0;--bit){
   assert(api.write(api.context,mosi,(n>>bit)&1));assert(api.write(api.context,sclk,true));assert(api.write(api.context,sclk,false));
  }
  ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 }
 assert(benchmarkNativeWrites()==elapsed.size()*writesPerFrame);
 std::sort(elapsed.begin(),elapsed.end());
 std::printf("120000 bytes, %zu scoped writes/frame, 9 frames; median %.3f ms (range %.3f..%.3f), %.2f ns/write\n",writesPerFrame,elapsed[4],elapsed.front(),elapsed.back(),elapsed[4]*1e6/writesPerFrame);
}
