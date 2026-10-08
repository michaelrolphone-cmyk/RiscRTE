#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include "native_spi_shim/Sdk.h"
#include <cstdio>
#include <fstream>
using namespace RiscCpu;
using NativeShim::model;
using NativeShim::Fault;
static Port* current=nullptr;
static Hardware hardware(){
  Hardware h{};h.owner=[](){return model.owner;};h.now=[](){return uint64_t(model.us/1000);};h.sleep=[](uint32_t){};
  h.gpioOpen=[](uint8_t pin,bool output,bool initial,bool pullup){++model.calls;model.direction[pin]=output?3:1;model.level[pin]=initial;model.pullup[pin]=pullup;return true;};
  h.gpioWrite=NativeShim::gpioWrite;h.gpioRead=[](uint8_t pin,bool* out){++model.calls;*out=model.level[pin];return true;};
  h.gpioClose=NativeShim::gpioClose;h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return false;};
  h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return false;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return false;};h.i2cClose=[](uint8_t){return true;};
  h.spiOpen=NativeShim::spiOpen;h.spiBegin=NativeShim::spiBegin;h.spiTransfer=NativeShim::spiTransfer;
  h.spiEnd=NativeShim::spiEnd;h.spiClose=NativeShim::spiClose;h.spiBeginThreeWire=NativeShim::spiBeginThreeWire;return h;
}
static RiscBoot::Port runtime(){return {[](){return model.owner;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},[](RiscBoot::Runtime& r){return current->bind(r);}};}
static void stage(const std::string& root,int miso=-1,unsigned physical=2){
  auto save=[&](const char* path,const std::string& text){std::ofstream(root+"/"+path)<<text;};
  auto display=[](unsigned id,unsigned cs,unsigned dc){return std::string(R"({"instance_id":)")+std::to_string(id)+R"(,"chip":{"vendor":"test","model":"display","revision":"unspecified"},"compatible":"test,display","config_type":"display.spi","config_version":1,"config":{"bus_instance_id":100,"cs":)"+std::to_string(cs)+R"(,"dc":)"+std::to_string(dc)+R"(,"reset":-1,"backlight":-1,"busy":-1,"width":240,"height":240,"offset_x":0,"offset_y":0,"rotation":0,"reset_active_high":false,"busy_active_high":true,"backlight_active_high":true,"power_pins":[],"power_active_high":[],"reset_assert_ms":0,"reset_recovery_ms":0}})";};
  save("board.json",std::string(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-three-wire","revision":"unspecified","buses":[{"instance_id":100,"kind":"spi","controller_namespace":"riscrte.logical","controller":0,"physical_controller":)")+std::to_string(physical)+R"(,"frequency_hz":2000000,"mode":0,"pins":{"sclk":4,"mosi":5,"miso":)"+std::to_string(miso)+R"(}}],"devices":[)"+display(7,7,8)+","+display(9,11,12)+"]}");
  save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"display.json","instance_id":7},{"manifest":"display.json","instance_id":9}]})");
  save("display.json",R"({"type":"driver","id":"generic-display","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"display.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1},{"capability":"spi.bus","api":1},{"capability":"platform.clock","api":1}],"provides":[{"capability":"test.display","api":1}],"hardware_compatibility":[{"compatible":"test,display","revisions":["unspecified"],"config_type":"display.spi","config_version":1}]})");
}
struct Fixture {
  Port p;RiscBoot::Runtime r;
  Fixture(const std::string& root,int miso=-1,bool supported=true,unsigned physical=2):p(hardware()),r(runtime()){
    model={};for(auto& state:NativeShim::spi)state={};stage(root,miso,physical);current=&p;
    if(!supported)p.hw_.spiBeginThreeWire=nullptr;
    assert(r.prepare(root.c_str()) && !model.calls && p.spiCount_==2 && p.gpioCount_==2);
  }
  ~Fixture(){assert(p.quiescent() && !model.initialized && !model.device.live && !model.pending);}
};
struct LegacySpi {
  uint32_t api_version,struct_size;void* context;
  bool (*claim)(void*,uint8_t,uint8_t,int8_t,uint8_t,uint64_t*);
  bool (*begin)(void*,uint64_t,uint32_t,uint8_t,uint32_t);
  bool (*exchange)(void*,uint64_t,const uint8_t*,uint8_t*,size_t);
  bool (*end)(void*,uint64_t);bool (*idle_clocks)(void*,uint64_t,uint32_t,uint16_t);bool (*release)(void*,uint64_t);
};
static_assert(offsetof(garden_spi_v1,claim_three_wire)==sizeof(LegacySpi),"Legacy SPI layout moved");
static_assert(GARDEN_SPI_THREE_WIRE_V1_SIZE==sizeof(garden_spi_v1),"Suffix size does not cover callback");
int main(int argc,char** argv){
  assert(argc==2);std::string root=argv[1];
  for(unsigned physical:{2u,3u}){
    Fixture f(root,-1,true,physical);auto& a=f.p.spis_[0].api;auto& b=f.p.spis_[1].api;auto& g=f.p.gpios_[0].api;
    assert(a.api_version==1 && a.struct_size>=GARDEN_SPI_THREE_WIRE_V1_SIZE && a.claim_three_wire);
    uint64_t t=99,u=0,dc=0;
    for(uint8_t pin:{4,5,7,11,12})assert(!g.claim(g.context,pin,true,false,false,&t) && !t && !model.calls);
    assert(!a.claim_three_wire(a.context,3,5,7,&t) && !t && !model.calls);
    assert(!a.claim_three_wire(a.context,4,6,7,&t) && !t && !model.calls);
    assert(!a.claim_three_wire(a.context,4,5,11,&t) && !t && !model.calls);
    assert(!a.claim_three_wire(a.context,4,5,7,nullptr) && !model.calls);
    model.owner=false;assert(!a.claim_three_wire(a.context,4,5,7,&t) && !model.calls);model.owner=true;
    assert(a.claim_three_wire(a.context,4,5,7,&t) && t);
    assert(b.claim(b.context,4,5,-1,11,&u) && u!=t && model.opens==1);
    assert(model.bus.flags==NativeShim::SPICOMMON_BUSFLAG_GPIO_PINS);
    assert(g.claim(g.context,8,true,false,false,&dc));
    const unsigned before=model.calls;
    assert(!a.begin(a.context,u,2000000,0,1000));assert(!a.begin(a.context,t,2000001,0,1000));
    assert(!a.begin(a.context,t,2000000,1,1000));assert(!a.begin(a.context,t,2000000,0,0));assert(!a.begin(a.context,t,2000000,0,1001));
    model.owner=false;assert(!a.begin(a.context,t,2000000,0,1000));model.owner=true;assert(model.calls==before);
    assert(a.begin(a.context,t,2000000,0,1000));
    assert(model.device.config.flags==(NativeShim::SPI_DEVICE_3WIRE|NativeShim::SPI_DEVICE_HALFDUPLEX));
    assert(model.pullup[5] && model.direction[5]==NativeShim::GPIO_MODE_INPUT && !model.level[7]);
    assert(!b.begin(b.context,u,2000000,0,20));assert(!b.end(b.context,t));assert(!b.release(b.context,u));
    uint8_t tx[513]{0x71,0x19},rx[513]{};
    assert(!a.exchange(a.context,t,tx,rx,1));assert(!a.exchange(a.context,t,nullptr,nullptr,1));
    assert(!a.exchange(a.context,t,tx,nullptr,0));assert(!a.exchange(a.context,t,tx,nullptr,513));assert(!a.exchange(a.context,u,tx,nullptr,1));
    model.owner=false;assert(!a.exchange(a.context,t,tx,nullptr,1));assert(!a.end(a.context,t));assert(!a.release(a.context,t));model.owner=true;
    assert(a.exchange(a.context,t,tx,nullptr,2) && model.queueWait==9 && model.drainWait==9);
    assert(g.write(g.context,dc,true) && !model.level[7]);
    model.queueDelay=3;assert(a.exchange(a.context,t,nullptr,rx,512) && rx[511]==0xa5 && model.drainWait==6 && !model.level[7]);model.queueDelay=0;
    assert(a.exchange(a.context,t,tx,nullptr,1) && model.direction[5]==NativeShim::GPIO_MODE_INPUT_OUTPUT && !model.level[7]);
    model.us=999000;assert(a.exchange(a.context,t,tx,nullptr,1) && model.queueWait==2);
    model.us=1000000;assert(!a.exchange(a.context,t,tx,nullptr,1));assert(a.end(a.context,t) && model.level[7]);
    // Old claim remains full duplex/dummy-clock capable on the same bus.
    assert(b.begin(b.context,u,2000000,0,20) && model.device.config.flags==0 && !model.pullup[5]);
    assert(b.exchange(b.context,u,tx,rx,1) && model.queueWait==21);
    assert(b.exchange(b.context,u,nullptr,nullptr,1));
    assert(static_cast<const uint8_t*>(NativeShim::spi[physical-2].transaction.tx_buffer)[0]==0xff);
    assert(b.end(b.context,u));
    const uint64_t stale=t;assert(a.release(a.context,t));assert(!a.begin(a.context,stale,2000000,0,20));
    assert(a.claim_three_wire(a.context,4,5,7,&t) && t!=stale);assert(!a.end(a.context,stale));assert(!a.release(a.context,stale));
    assert(a.release(a.context,t));assert(b.release(b.context,u));assert(g.release(g.context,dc));
  }
  for(bool supported:{true,false}){
    Fixture f(root,supported?6:-1,supported);auto& a=f.p.spis_[0].api;uint64_t t=99;
    assert(!a.claim_three_wire(a.context,4,5,7,&t) && !t && !model.calls);
    if(supported){
      uint8_t tx=0x42,rx=0;auto& g=f.p.gpios_[0].api;
      assert(!g.claim(g.context,6,false,false,false,&t) && !model.calls);
      assert(a.claim(a.context,4,5,6,7,&t) && model.bus.flags==0);
      assert(a.begin(a.context,t,2000000,0,20) && model.device.config.flags==0);
      assert(a.exchange(a.context,t,&tx,&rx,1) && rx==0xa5 && model.queueWait==21);
      assert(a.end(a.context,t));assert(a.release(a.context,t));
    }
  }
  // Begin failure retains both the actual CPU bus owner and native cleanup
  // path, including mode-switch errors before the pin configuration runs.
  for(Fault fault:{Fault::Remove,Fault::Add,Fault::Pulldown,Fault::Pullup,Fault::Direction,Fault::CsLow}){
    Fixture f(root);auto& a=f.p.spis_[0].api;auto& b=f.p.spis_[1].api;uint64_t t=0,u=0;
    assert(a.claim_three_wire(a.context,4,5,7,&t));assert(b.claim(b.context,4,5,-1,11,&u));
    assert(b.begin(b.context,u,2000000,0,1000));assert(b.end(b.context,u));model.fault=fault;
    assert(!a.begin(a.context,t,2000000,0,1000) && f.p.spiBuses_[0].held==&f.p.spis_[0] && NativeShim::spi[0].held);
    uint8_t tx=0x13;assert(!a.exchange(a.context,t,&tx,nullptr,1));assert(!a.release(a.context,t));assert(!b.begin(b.context,u,2000000,0,20));
    model.fault=Fault::CsHigh;assert(!a.end(a.context,t) && f.p.spiBuses_[0].held);model.fault=Fault::None;
    assert(a.end(a.context,t));assert(a.begin(a.context,t,2000000,0,1000));assert(a.exchange(a.context,t,&tx,nullptr,1));assert(a.end(a.context,t));
    assert(a.release(a.context,t));assert(b.release(b.context,u));
  }
  // Pending RX lives in native storage. End retries never use the caller RX
  // pointer, nor raise CS or free the SDK device before completion is proved.
  for(bool receive:{false,true}){
    Fixture f(root);auto& a=f.p.spis_[0].api;uint64_t t=0;assert(a.claim_three_wire(a.context,4,5,7,&t));assert(a.begin(a.context,t,2000000,0,1000));
    uint8_t tx[4]{1,2,3,4},rx[4]{9,9,9,9};model.fault=Fault::Drain;
    assert(!a.exchange(a.context,t,receive?nullptr:tx,receive?rx:nullptr,4) && !model.level[7] && model.pending);
    assert(rx[0]==9 && model.pending->tx_buffer!=tx && model.pending->rx_buffer!=rx);
    tx[0]=99;if(!receive)assert(static_cast<const uint8_t*>(model.pending->tx_buffer)[0]==1);
    assert(!a.exchange(a.context,t,tx,nullptr,1));assert(!a.end(a.context,t) && model.drainWait==9 && model.pending && !model.level[7]);
    assert(!a.release(a.context,t) && f.p.spis_[0].token==t);
    model.us=1000000;assert(!a.end(a.context,t) && model.drainWait==0 && model.pending);
    model.fault=Fault::CsHigh;assert(!a.end(a.context,t) && !model.pending && !model.level[7] && f.p.spiBuses_[0].held);
    model.fault=Fault::None;assert(a.end(a.context,t) && model.level[7] && rx[0]==9);
    assert(model.events[model.events.size()-2]=="drain" && model.events.back()=="high");assert(a.release(a.context,t));
  }
  for(Fault fault:{Fault::Direction,Fault::Queue}){
    Fixture f(root);auto& a=f.p.spis_[0].api;uint64_t t=0;uint8_t tx=1;
    assert(a.claim_three_wire(a.context,4,5,7,&t));assert(a.begin(a.context,t,2000000,0,1000));model.fault=fault;
    assert(!a.exchange(a.context,t,&tx,nullptr,1) && !model.level[7]);model.fault=Fault::None;
    assert(!a.exchange(a.context,t,&tx,nullptr,1));assert(a.end(a.context,t));assert(a.release(a.context,t));
  }
  for(Fault fault:{Fault::Remove,Fault::Free,Fault::ClosePin}){
    Fixture f(root);auto& a=f.p.spis_[0].api;uint64_t t=0;
    assert(a.claim_three_wire(a.context,4,5,7,&t));assert(a.begin(a.context,t,2000000,0,20));assert(a.end(a.context,t));
    model.fault=fault;assert(!a.release(a.context,t) && f.p.spis_[0].token==t && f.p.pins_[4].owner && f.p.pins_[7].owner);
    const unsigned calls=model.calls;assert(!a.begin(a.context,t,2000000,0,20) && model.calls==calls);
    uint64_t other=99;auto& b=f.p.spis_[1].api;
    assert(!b.claim(b.context,4,5,-1,11,&other) && !other && model.calls==calls);
    model.fault=Fault::None;assert(a.release(a.context,t));
  }
  for(bool shared:{false,true}){
    Fixture f(root);auto& a=f.p.spis_[0].api;auto& b=f.p.spis_[1].api;uint64_t t=0,u=0;
    assert(a.claim_three_wire(a.context,4,5,7,&t));if(shared)assert(b.claim(b.context,4,5,-1,11,&u));
    assert(a.begin(a.context,t,2000000,0,20));assert(a.end(a.context,t));
    model.fault=Fault::ClosePin;model.faultPin=7;assert(!a.release(a.context,t));
    const unsigned calls=model.calls;assert(!a.begin(a.context,t,2000000,0,20));
    if(shared){assert(!b.begin(b.context,u,2000000,0,20));assert(!b.release(b.context,u));}
    assert(model.calls==calls);model.fault=Fault::None;assert(a.release(a.context,t));
    if(shared){assert(b.begin(b.context,u,2000000,0,20));assert(b.end(b.context,u));assert(b.release(b.context,u));}
  }
  puts("Actual CPU + production native SPI: append-only ABI, typed scope, owner/tokens, 3-wire direction/matrix/CS, bounded phases, legacy duplex, begin failure cleanup, pending DMA drain, release retry PASS");
}
