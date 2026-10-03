#include "runtime/drivers/ProviderGraphV2.h"
#include <TWatchHardwareV1.h>
#include <TWatchPlatformV1.h>
#include <RiscI2cBusV1.h>
#include <cassert>
#include <cstdio>
using namespace RuntimeProviders;
struct Context { uint8_t controller,sda,scl; unsigned opens=0,closes=0,transfers=0; bool live=false; };
static bool open(void* p,uint8_t c,uint8_t sda,uint8_t scl,uint32_t hz,uint64_t* token) {
  auto& x=*static_cast<Context*>(p); *token=0;
  if(x.live || c!=x.controller || sda!=x.sda || scl!=x.scl || hz!=100000) return false;
  x.live=true; ++x.opens; *token=123; return true;
}
static bool transfer(void* p,uint64_t t,uint8_t address,const uint8_t*,size_t,uint8_t* rx,size_t n,uint32_t ms) {
  auto& x=*static_cast<Context*>(p);
  if(!x.live || t!=123 || address!=0x34 || n!=1 || ms!=50) return false;
  ++x.transfers; *rx=x.sda; return true;
}
static bool close(void* p,uint64_t t) {
  auto& x=*static_cast<Context*>(p); if(!x.live || t!=123) return false;
  x.live=false; ++x.closes; return true;
}
int main(int argc,char** argv) {
  assert(argc==2); Context context[2]={{0,5,6},{1,9,10}};
  twatch_i2c_controller_v1 port[2]{}; tw_hw_i2c_controller_v1 configs[2]{};
  risc_hardware_device_v1 hardware[2]{}; RequirementV2 req[2][2]{};
  GraphV2 graph;
  for(unsigned i=0;i<2;++i) {
    port[i]={1,sizeof(port[i]),&context[i],open,transfer,close};
    auto& c=configs[i]; c.struct_size=sizeof(c); c.bus.struct_size=sizeof(c.bus);
    c.bus.kind=2;c.bus.instance_id=101+i;c.bus.controller=i;c.bus.frequency_hz=100000;
    c.bus.sda=context[i].sda;c.bus.scl=context[i].scl;c.bus.sclk=c.bus.mosi=c.bus.miso=-1;
    hardware[i]={1,sizeof(hardware[i]),7+i,"espressif,esp32s3-i2c","unspecified","controller.i2c",1,sizeof(c),&c};
    req[i][0]={"hardware.device",1};req[i][1]={"platform.i2c.controller",1,nullptr,0,&port[i]};
    SpecV2 spec{"twatch-i2c",argv[1],"i2c.bus",1,req[i],2};spec.hardware=&hardware[i];
    assert(graph.addVerified(spec));
  }
  assert(!graph.acquireFrom("twatch-i2c","i2c.bus",1).slot); // Ambiguous package alone.
  auto first=graph.acquireFrom("twatch-i2c","i2c.bus",1,7);
  auto second=graph.acquireFrom("twatch-i2c","i2c.bus",1,8);
  assert(first.slot && second.slot);
  auto* a=static_cast<const risc_i2c_bus_api_v1*>(graph.interfaceFor(first));
  auto* b=static_cast<const risc_i2c_bus_api_v1*>(graph.interfaceFor(second));
  assert(a && b && a!=b && a->claim_device!=b->claim_device);
  uint64_t ta=0,tb=0,duplicate=0;uint8_t value=0;
  assert(a->claim_device(a->context,0x34,&ta) && b->claim_device(b->context,0x34,&tb));
  assert(!a->claim_device(a->context,0x34,&duplicate) && !duplicate);
  assert(!graph.release(first)); // Live device lease retains first image/context.
  assert(!graph.interfaceFor(first) && context[0].live && !context[0].closes);
  assert(b->transact(b->context,tb,nullptr,0,&value,1,50) && value==9);
  assert(b->release_device(b->context,tb) && graph.release(second));
  assert(context[1].closes==1 && context[0].live);
  assert(a->release_device(a->context,ta) && graph.release(first));
  assert(context[0].closes==1 && graph.shutdown());
  puts("Real Watch I2C image twice: independent data/BSS, two wiring maps, scoped transport, retained failed quiescence PASS");
}
