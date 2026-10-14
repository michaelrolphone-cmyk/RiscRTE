// Real CpuPort pad-hold implementation beneath the production Watch panel.
// The C harness models panel SPI and GPIO; this bridge owns only pad retention.
#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
#include <cstdlib>
namespace {
RiscCpu::Hardware hardware(){
 RiscCpu::Hardware h{};h.owner=[](){return true;};
 h.deepHold=[](uint8_t pin,bool){assert(pin==45);return true;};return h;
}
RiscCpu::Port port{hardware()};
uint64_t current=0;
}
extern "C" int32_t display_test_cpu_hold(uint64_t token,bool enable){
 auto& g=port.gpios_[0];g.port=&port;g.instance=5;
 auto& pin=port.pins_[45];
 if(!current){current=token;pin.owner=&g;pin.token=token;pin.output=true;}
 assert(token==current && pin.owner==&g && pin.token==token && !pin.pwm);
 // A pending native app-data write is a normal background condition on wake.
 port.hw_.maintenanceIdle=enable?nullptr:+[](){return false;};
 const int32_t rc=RiscCpu::Port::gpioDeepSleepHold(&g,token,enable);
 const bool broken=std::getenv("PAD_HOLD_EXPECT_BROKEN")!=nullptr;
 std::printf("WATCH_PAD enable=%d result=%ld background_write=%d\n",enable,long(rc),!enable);
 std::fflush(stdout);
 assert(rc==((broken&&!enable)?RISC_DEEP_SLEEP_BUSY:0));
 if(!enable)assert(!port.appExitSafe());
 return rc;
}
