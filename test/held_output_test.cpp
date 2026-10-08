#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
using namespace RiscCpu;
static Port* activePort;
static unsigned io=0;
static bool owned=true,openOk=true;
static Port* checking=nullptr;
static bool owner(){return owned;}
static bool bind(RiscBoot::Runtime&r){return activePort->bind(r);}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){++io;};
 h.gpioOpen=[](uint8_t pin,bool,bool,bool){++io;if(checking)assert(checking->pins_[pin].held && checking->pins_[pin].retiredHeld);return openOk;};
 h.deepHold=[](uint8_t,bool){++io;return true;};h.gpioWrite=[](uint8_t,bool){++io;return true;};
 h.gpioRead=[](uint8_t,bool*){++io;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++io;return true;};h.gpioClose=[](uint8_t){++io;return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++io;return true;};h.i2cClose=[](uint8_t){++io;return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++io;return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++io;return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++io;return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++io;return true;};h.spiClose=[](uint8_t){++io;return true;};
 h.hciOpen=[](){++io;return true;};h.hciClose=[](){++io;return true;};
 h.hciSend=[](uint8_t,const uint8_t*,size_t,uint32_t){++io;return true;};h.hciReceive=[](uint8_t*,uint8_t*,size_t,size_t*,uint32_t){++io;return true;};
 h.hciIdle=[](){return true;};h.hciSafe=[](){return true;};return h;
}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 static_assert(GARDEN_GPIO_DEEP_SLEEP_SET_V1_SIZE==offsetof(garden_gpio_v1,retire_held_output));
 auto save=[&](const char*path,const std::string&data){std::ofstream(root+"/"+path)<<data;};
 const std::string board=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-test","revision":"unspecified","buses":[{"instance_id":100,"kind":"spi","controller_namespace":"esp32.peripheral","controller":2,"frequency_hz":2000000,"mode":0,"pins":{"sclk":12,"mosi":11,"miso":-1}}],"devices":[{"instance_id":1,"chip":{"vendor":"test","model":"display","revision":"unspecified"},"compatible":"test,display","config_type":"display.spi","config_version":1,"config":{"bus_instance_id":100,"cs":13,"dc":18,"reset":14,"backlight":-1,"busy":6,"width":800,"height":480,"offset_x":0,"offset_y":0,"rotation":0,"reset_active_high":false,"busy_active_high":true,"backlight_active_high":true,"power_pins":[],"power_active_high":[],"reset_assert_ms":10,"reset_recovery_ms":10}},{"instance_id":2,"chip":{"vendor":"test","model":"sd","revision":"unspecified"},"compatible":"test,sd","config_type":"storage.sd-spi","config_version":1,"config":{"bus_instance_id":100,"cs":7,"detect":-1,"write_protect":-1,"detect_active_high":true,"write_protect_active_high":false}}]})";
 const std::string manifest=R"({"type":"driver","id":"gpio-test","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.output","api":1}],"hardware_compatibility":[{"compatible":"test,display","revisions":["unspecified"],"config_type":"display.spi","config_version":1}]})";
 JsonDocument b;deserializeJson(b,board);b["devices"][1].set(b["devices"][0]);b["devices"][1]["instance_id"]=2;
 b["devices"][1]["config"]["cs"]=7;b["devices"][1]["config"]["dc"]=8;b["devices"][1]["config"]["reset"]=-1;b["devices"][1]["config"]["busy"]=-1;b["devices"][1]["config"]["reset_assert_ms"]=0;b["devices"][1]["config"]["reset_recovery_ms"]=0;
 std::string boardBytes;serializeJson(b,boardBytes);save("board.json",boardBytes);save("driver.json",manifest);
 for(bool ordinary:{false,true})for(bool peer:{false,true}){
  JsonDocument m;deserializeJson(m,manifest);if(ordinary){auto r=m["requires"].add<JsonObject>();r["capability"]="spi.bus";r["api"]=1;}
  std::string bytes;serializeJson(m,bytes);save("driver.json",bytes);
  m["id"]="peer";m["provides"][0]["capability"]="test.peer";
  bytes.clear();serializeJson(m,bytes);save("peer.json",bytes);
  save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"driver.json","instance_id":1})")+(peer?R"(,{"manifest":"peer.json","instance_id":2})":"")+"]}");
  io=0;Port p(hardware());activePort=&p;
  RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
  const bool valid=r.prepare(root.c_str());if(valid!=(ordinary || !peer))fprintf(stderr,"ordinary=%d peer=%d error=%s\n",ordinary,peer,r.error());assert(valid==(ordinary || !peer));assert(!io);
  if(!ordinary && peer)continue;
  auto& a=p.gpios_[0].api;const auto bit=[](unsigned n){return uint64_t(1)<<n;};
  assert(a.struct_size>=GARDEN_GPIO_READ_RETIRED_OUTPUT_V1_SIZE && a.read_retired_output);
  assert(p.gpios_[0].output==(bit(18)|bit(14)|(ordinary?0:bit(12)|bit(11)|bit(13))));
  assert(p.gpios_[0].input==(bit(6)|(ordinary?0:bit(11))));
  assert(p.gpios_[0].pullup==(ordinary?0:bit(11)));
  uint64_t t=0;assert(!a.claim(a.context,21,true,false,false,&t) && !io);
  if(ordinary){assert(!a.claim(a.context,11,true,false,false,&t) && !io);continue;}
  assert(a.claim(a.context,11,false,false,true,&t));assert(a.release(a.context,t));
  assert(!a.claim(a.context,6,false,false,true,&t));
  assert(a.claim(a.context,11,true,false,false,&t));assert(a.release(a.context,t));
  assert(a.claim(a.context,14,true,false,false,&t));assert(!a.retire_held_output(a.context,t));
  assert(a.pwm(a.context,t,1000,40,100));assert(!a.retire_held_output(a.context,t));
  assert(a.write(a.context,t,false));assert(a.deep_sleep_hold(a.context,t,true)==0);
  assert(!p.providerStorageSafe() && !p.quiescent());
  const auto before=io;owned=false;assert(!a.retire_held_output(a.context,t));owned=true;
  p.transferring_=true;assert(!a.retire_held_output(a.context,t));p.transferring_=false;
  p.sleepRetained_=true;assert(!a.retire_held_output(a.context,t));p.sleepRetained_=false;
  p.pins_[14].wakeModes=1;assert(!a.retire_held_output(a.context,t));p.pins_[14].wakeModes=0;
  Port::Gpio other=p.gpios_[0];assert(!Port::gpioRetireHeldOutput(&other,t));
  assert(!a.retire_held_output(nullptr,t));assert(!a.retire_held_output(a.context,0));
  assert(a.retire_held_output(a.context,t));assert(io==before && p.quiescent() && p.providerStorageSafe());
  bool level=true;assert(a.read_retired_output(a.context,14,&level) && !level);const auto readIo=io;
  assert(!a.retire_held_output(a.context,t) && !a.release(a.context,t) && !a.write(a.context,t,true));
  assert(!Port::gpioClaim(&other,14,true,true,false,&t));assert(io==readIo);
  uint64_t fresh=0;checking=&p;assert(a.claim(a.context,14,true,false,false,&fresh));checking=nullptr;
  assert(fresh && !p.pins_[14].held && !p.pins_[14].retiredHeld && !p.quiescent());
  assert(a.deep_sleep_hold(a.context,fresh,true)==0 && a.retire_held_output(a.context,fresh));
  const auto saved=p.pins_[14];openOk=false;checking=&p;assert(!a.claim(a.context,14,true,true,false,&fresh));checking=nullptr;openOk=true;
  assert(!fresh && p.poisoned_ && !p.quiescent() && p.pins_[14].owner==saved.owner && p.pins_[14].held && p.pins_[14].retiredHeld);
 }
 puts("Scoped bitbang GPIO and held-output retirement: exact pins, peer rejection, legacy SPI, ownership, fresh claim and retained failure PASS");
}
