#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
using namespace RiscCpu;
static Port* current;
static bool owned=true,openOk=true,beginOk=true,transferOk=true,endOk=true,closeOk=true,gpioCloseOk=true;
static unsigned calls=0,opens=0,closes=0;
static uint64_t tick=0;
static bool owner(){return owned;}
static bool bind(RiscBoot::Runtime&r){return current->bind(r);}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[](){return tick;};h.sleep=[](uint32_t ms){++calls;tick+=ms;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++calls;return true;};h.gpioWrite=[](uint8_t,bool){++calls;return true;};
 h.gpioRead=[](uint8_t,bool*out){++calls;*out=false;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++calls;return true;};h.gpioClose=[](uint8_t){++calls;return gpioCloseOk;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++calls;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++calls;return true;};h.i2cClose=[](uint8_t){++calls;return true;};
 h.spiOpen=[](uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso){++calls;++opens;assert((physical==2 || physical==3) && sclk==4 && mosi==5 && miso==6);return openOk;};
 h.spiBegin=[](uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,uint32_t ms){++calls;assert((physical==2 || physical==3) && (cs==7 || cs==11 || cs==15) && hz==2000000 && mode==0 && ms==20);return beginOk;};
 h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*rx,size_t count,uint32_t ms){++calls;assert(count<=512 && ms<=20);if(rx)for(size_t i=0;i<count;++i)rx[i]=uint8_t(i);return transferOk;};
 h.spiEnd=[](uint8_t,uint8_t,uint32_t){++calls;return endOk;};h.spiClose=[](uint8_t){++calls;++closes;return closeOk;};return h;
}
static void reset(){owned=openOk=beginOk=transferOk=endOk=closeOk=gpioCloseOk=true;calls=opens=closes=0;tick=0;}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 auto save=[&](const char*path,const std::string&data){std::ofstream(root+"/"+path)<<data;};
 auto stage=[&](const char*model,unsigned physical=2,bool selected=true,bool unsupported=false){
  const std::string chip=model;
  const std::string band=chip=="sx1280"?"\"minimum_hz\":2400000000,\"maximum_hz\":2500000000":"\"minimum_hz\":902000000,\"maximum_hz\":928000000";
  auto radio=[&](int id,int cs,int rst,int busy,int irq){return std::string(R"({"instance_id":)")+std::to_string(id)+R"(,"chip":{"vendor":"test","model":")"+chip+R"(","revision":"unspecified"},"compatible":"test,radio","config_type":"radio.lora","config_version":1,"config":{"bus_instance_id":100,"cs":)"+std::to_string(cs)+",\"reset\":"+std::to_string(rst)+",\"busy\":"+std::to_string(busy)+",\"irq\":"+std::to_string(irq)+","+band+R"(,"tcxo_voltage":0,"reset_active_high":false,"busy_active_high":true,"irq_active_high":true}})";};
  std::string devices=radio(7,7,8,9,10)+","+radio(8,11,12,13,14)+R"(,{"instance_id":9,"chip":{"vendor":"test","model":"display","revision":"unspecified"},"compatible":"test,display","config_type":"display.spi","config_version":1,"config":{"bus_instance_id":100,"cs":15,"dc":16,"reset":-1,"backlight":-1,"busy":-1,"width":240,"height":240,"offset_x":0,"offset_y":0,"rotation":0,"reset_active_high":false,"busy_active_high":true,"backlight_active_high":true,"power_pins":[],"power_active_high":[],"reset_assert_ms":0,"reset_recovery_ms":0}})";
  if(unsupported)devices=R"({"instance_id":7,"chip":{"vendor":"test","model":"sd","revision":"unspecified"},"compatible":"test,sd","config_type":"storage.sd-spi","config_version":1,"config":{"bus_instance_id":100,"cs":7,"detect":-1,"write_protect":-1,"detect_active_high":true,"write_protect_active_high":false}})";
  save("board.json",std::string(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-spi-test","revision":"unspecified","buses":[{"instance_id":100,"kind":"spi","controller_namespace":"riscrte.logical","controller":0,"physical_controller":)")+std::to_string(physical)+R"(,"frequency_hz":2000000,"mode":0,"pins":{"sclk":4,"mosi":5,"miso":6}}],"devices":[)"+devices+"]}");
  save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+(selected?(unsupported?R"({"manifest":"sd.json","instance_id":7})":R"({"manifest":"radio.json","instance_id":7},{"manifest":"radio.json","instance_id":8},{"manifest":"display.json","instance_id":9})"):"")+"]}");
  for(const std::string name:{"radio","display","sd"}){
   const std::string type=name=="radio"?"radio.lora":name=="display"?"display.spi":"storage.sd-spi";
   save((name+".json").c_str(),std::string(R"({"type":"driver","id":"generic-)")+name+R"(","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":")"+name+R"(.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1},{"capability":"spi.bus","api":1},{"capability":"platform.clock","api":1}],"provides":[{"capability":"test.)"+name+R"(","api":1}],"hardware_compatibility":[{"compatible":"test,)"+name+R"(","revisions":["unspecified"],"config_type":")"+type+R"(","config_version":1}]})");
  }
 };
 auto runtime=[&](){return RiscBoot::Port{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind};};
 for(const char* model:{"sx1262","sx1280"})for(unsigned physical:{2u,3u}){
  stage(model,physical);reset();Port p(hardware());current=&p;RiscBoot::Runtime r(runtime());
  if(!r.prepare(root.c_str())){fprintf(stderr,"prepare: %s\n",r.error());return 1;}
  assert(!calls && p.spiCount_==3 && p.gpioCount_==3 && p.quiescent());
  auto& a=p.spis_[0];auto& b=p.spis_[1];auto& display=p.spis_[2];auto& g=p.gpios_[0];auto& other=p.gpios_[1];
  assert(a.physical==physical && a.bus.instance_id==100 && a.cs==7 && b.cs==11 && display.cs==15);
  assert(g.output==(uint64_t(1)<<8) && g.input==((uint64_t(1)<<9)|(uint64_t(1)<<10)) && !g.pullup);
  uint64_t rst=0,busy=0,irq=0,token=0,two=0,screen=0;
  for(uint8_t pin:{4,5,6,7,12,16})assert(!g.api.claim(g.api.context,pin,true,false,false,&token) && !token && !calls);
  assert(!g.api.claim(g.api.context,9,false,false,true,&token) && !token && !calls);
  assert(!g.api.claim(g.api.context,9,true,false,false,&token) && !token && !calls);
  assert(g.api.claim(g.api.context,8,true,true,false,&rst));assert(g.api.claim(g.api.context,9,false,false,false,&busy));assert(g.api.claim(g.api.context,10,false,false,false,&irq));
  bool value=true;assert(g.api.read(g.api.context,busy,&value) && !value);assert(!g.api.write(g.api.context,busy,true));assert(!other.api.write(other.api.context,rst,true));
  assert(g.api.write(g.api.context,rst,false));unsigned before=calls;
  assert(!a.api.claim(a.api.context,4,5,6,11,&token) && !token && calls==before);
  assert(!a.api.claim(a.api.context,4,5,-1,7,&token) && !token && calls==before);
  assert(a.api.claim(a.api.context,4,5,6,7,&token) && token);uint64_t first=token;
  assert(b.api.claim(b.api.context,4,5,6,11,&two) && two);assert(display.api.claim(display.api.context,4,5,6,15,&screen));assert(opens==1);
  assert(!a.api.begin(a.api.context,two,2000000,0,20));assert(!a.api.begin(a.api.context,token,2000001,0,20));assert(!a.api.begin(a.api.context,token,2000000,1,20));
  owned=false;assert(!a.api.begin(a.api.context,token,2000000,0,20));owned=true;
  beginOk=false;assert(!a.api.begin(a.api.context,token,2000000,0,20) && !p.spiBuses_[physical-2].held);beginOk=true;
  assert(a.api.begin(a.api.context,token,2000000,0,20));assert(!b.api.begin(b.api.context,two,2000000,0,20));assert(!display.api.begin(display.api.context,screen,2000000,0,20));
  risc_light_sleep_result_v1 wake{sizeof(wake),0};assert(g.api.light_sleep(g.api.context,irq,true,&wake)==RISC_LIGHT_SLEEP_BUSY);assert(g.api.deep_sleep(g.api.context,irq,true)==RISC_DEEP_SLEEP_BUSY);
  uint8_t tx[513]{},rx[513]{};assert(a.api.exchange(a.api.context,token,tx,rx,255) && rx[254]==254);assert(!a.api.exchange(a.api.context,token,tx,rx,513));
  transferOk=false;assert(!a.api.exchange(a.api.context,token,tx,rx,1));transferOk=true;
  endOk=false;assert(!a.api.end(a.api.context,token) && p.spiBuses_[physical-2].held==&a);assert(!a.api.release(a.api.context,token));endOk=true;
  tick=21;assert(!a.api.exchange(a.api.context,token,tx,rx,1));assert(a.api.end(a.api.context,token));
  assert(g.api.light_sleep(g.api.context,irq,true,&wake)==RISC_LIGHT_SLEEP_UNSUPPORTED);
  assert(display.api.begin(display.api.context,screen,2000000,0,20));assert(display.api.end(display.api.context,screen));
  assert(a.api.release(a.api.context,token));assert(!a.api.begin(a.api.context,first,2000000,0,20));assert(a.api.claim(a.api.context,4,5,6,7,&token) && token!=first && opens==1);
  assert(a.api.release(a.api.context,token));assert(b.api.release(b.api.context,two));assert(!closes);
  closeOk=false;assert(!display.api.release(display.api.context,screen) && display.token==screen);closeOk=true;
  gpioCloseOk=false;assert(!display.api.release(display.api.context,screen) && display.token==screen);gpioCloseOk=true;
  assert(display.api.release(display.api.context,screen));assert(g.api.release(g.api.context,rst));assert(g.api.release(g.api.context,busy));assert(g.api.release(g.api.context,irq));assert(p.quiescent());
 }
 // Config v2 selects only an explicit allowed mask; binding remains inert.
 static_assert(offsetof(tw_hw_lora_v2,base)==0,"v1 prefix moved");
 static_assert(offsetof(tw_hw_lora_v2,allowed_profiles)==sizeof(tw_hw_lora_v1),"selector suffix moved");
 for(int mask=0;mask<=16;++mask){
  stage("sx1262");JsonDocument board,manifest;
  assert(RiscBoot::readJson((root+"/board.json").c_str(),board));
  for(unsigned i=0;i<2;++i){board["devices"][i]["config_version"]=2;board["devices"][i]["config"]["allowed_profiles"]=mask;}
  std::string bytes;serializeJson(board,bytes);save("board.json",bytes);
  assert(RiscBoot::readJson((root+"/radio.json").c_str(),manifest));manifest["hardware_compatibility"][0]["config_version"]=2;
  bytes.clear();serializeJson(manifest,bytes);save("radio.json",bytes);
  reset();Port p(hardware());current=&p;RiscBoot::Runtime r(runtime());
  assert(r.prepare(root.c_str())==(mask>=1 && mask<=15));assert(!calls);
  if(mask>=1 && mask<=15){
   const auto*d=r.board().device(7);assert(d && d->hardware.config_version==2 && d->hardware.config_size==sizeof(tw_hw_lora_v2));
   const auto*c=static_cast<const tw_hw_lora_v2*>(d->hardware.config);assert(c->allowed_profiles==unsigned(mask) && c->base.struct_size==sizeof(*c));
   assert(&d->lora()==&c->base && d->lora().cs==7 && p.spis_[0].cs==7 && !p.gpios_[0].pullup);
  }
 }
 // A v1-only provider cannot accept the v2 envelope, and other config types
 // must not inherit the radio's additive version allowance.
 for(bool wrongDriver:{false,true}){
  stage("sx1262");JsonDocument board;assert(RiscBoot::readJson((root+"/board.json").c_str(),board));
  if(wrongDriver){board["devices"][0]["config_version"]=2;board["devices"][0]["config"]["allowed_profiles"]=15;}
  else board["devices"][2]["config_version"]=2;
  std::string bytes;serializeJson(board,bytes);save("board.json",bytes);reset();Port p(hardware());current=&p;RiscBoot::Runtime r(runtime());
  assert(!r.prepare(root.c_str()) && !calls);
 }
 stage("sx1262",2,false);reset();{Port p(hardware());current=&p;RiscBoot::Runtime r(runtime());assert(r.prepare(root.c_str()) && !calls && !p.spiCount_ && !p.gpioCount_);}
 stage("sx1262",2,true,true);reset();{Port p(hardware());current=&p;RiscBoot::Runtime r(runtime());assert(!r.prepare(root.c_str()) && !calls);}
 for(bool cleanup:{true,false}){
  stage("sx1262");reset();Port p(hardware());current=&p;RiscBoot::Runtime r(runtime());assert(r.prepare(root.c_str()));auto& a=p.spis_[0];uint64_t token=99;
  openOk=false;closeOk=cleanup;assert(!a.api.claim(a.api.context,4,5,6,7,&token) && !token);assert(p.quiescent()==cleanup);
  if(cleanup){openOk=true;assert(a.api.claim(a.api.context,4,5,6,7,&token));assert(a.api.release(a.api.context,token) && p.quiescent());}
  else {openOk=closeOk=true;assert(!a.api.claim(a.api.context,4,5,6,7,&token) && p.poisoned_ && !p.quiescent());}
 }
 puts("Typed radio SPI/GPIO: real JSON/graph admission, no preflight I/O, both controller mappings, shared display arbitration, scoped pins/tokens, bounds, transfer/end faults, release retry, poison retention and sleep coexistence PASS");
}
