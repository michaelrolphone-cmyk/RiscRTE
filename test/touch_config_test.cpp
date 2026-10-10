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
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime&r){return activePort->bind(r);}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){++io;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++io;return true;};h.gpioWrite=[](uint8_t,bool){++io;return true;};
 h.gpioRead=[](uint8_t,bool*){++io;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++io;return true;};h.gpioClose=[](uint8_t){++io;return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++io;return true;};h.i2cClose=[](uint8_t){++io;return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++io;return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++io;return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++io;return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++io;return true;};h.spiClose=[](uint8_t){++io;return true;};
 h.hciOpen=[](){++io;return true;};h.hciClose=[](){++io;return true;};
 h.hciSend=[](uint8_t,const uint8_t*,size_t,uint32_t){++io;return true;};h.hciReceive=[](uint8_t*,uint8_t*,size_t,size_t*,uint32_t){++io;return true;};
 h.hciIdle=[](){return true;};h.hciSafe=[](){return true;};return h;
}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 static_assert(offsetof(risc_hw_i2c_touch_v2,base)==0);
 static_assert(offsetof(risc_hw_i2c_touch_v2,power)==sizeof(risc_hw_i2c_touch_v1));
 auto save=[&](const char*path,const std::string&data){std::ofstream(root+"/"+path)<<data;};
 const char* board=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-test","revision":"unspecified","buses":[{"instance_id":100,"kind":"i2c","controller_namespace":"esp32.peripheral","controller":0,"frequency_hz":400000,"mode":0,"pins":{"sda":39,"scl":38}}],"devices":[{"instance_id":1,"chip":{"vendor":"goodix","model":"gt911","revision":"unspecified"},"compatible":"goodix,gt911","config_type":"touch.i2c","config_version":1,"config":{"bus_instance_id":100,"width":480,"height":800,"address":93,"reset_active_high":false,"irq_active_high":false,"irq_pull_up":false,"reset":4,"irq":10,"reset_assert_ms":10,"reset_recovery_ms":10}}]})";
 const char* manifest=R"({"type":"driver","id":"touch-test","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.touch","api":1}],"hardware_compatibility":[{"compatible":"goodix,gt911","revisions":["unspecified"],"config_type":"touch.i2c","config_version":1}]})";
 save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"driver.json","instance_id":1}]})");
 for(unsigned scenario=0;scenario<15;++scenario){
  JsonDocument b,m;deserializeJson(b,board);deserializeJson(m,manifest);
  auto d=b["devices"][0];auto c=d["config"];
  if(scenario){d["config_version"]=2;m["hardware_compatibility"][0]["config_version"]=2;c["power"]=2;c["power_active_high"]=false;c["irq_output"]=true;c["alternate_address"]=20;}
  bool valid=scenario<=1 || scenario==11 || scenario==12;
  switch(scenario){
   case 2:c.remove("power");break;
   case 3:c["power"]=10;break;
   case 4:c["alternate_address"]=93;break;
   case 5:c["alternate_address"]=7;break;
   case 6:c["irq_output"]=false;break;
   case 7:c["power"]=-1;c["power_active_high"]=true;break;
   case 8:c["unknown"]=1;break;
   case 9:d["config_version"]=3;break;
   case 10:m["hardware_compatibility"][0]["config_version"]=1;break;
   case 11:c["power"]=-1;c["alternate_address"]=0;c["irq_output"]=false;break;
   case 12:c["irq_pull_up"]=true;break;
   case 13:{auto peer=b["devices"].add<JsonObject>();peer.set(d);peer["instance_id"]=2;peer["config"]["address"]=20;peer["config"]["alternate_address"]=21;peer["config"]["reset"]=5;peer["config"]["irq"]=6;peer["config"]["power"]=7;break;}
   case 14:d["config_version"]=1;m["hardware_compatibility"][0]["config_version"]=1;break;
  }
  std::string text;serializeJson(b,text);save("board.json",text);text.clear();serializeJson(m,text);save("driver.json",text);
  io=0;Port p(hardware());activePort=&p;RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
  bool ok=r.prepare(root.c_str());if(ok!=valid)fprintf(stderr,"scenario=%u error=%s\n",scenario,r.error());assert(ok==valid && !io);
  if(!valid)continue;
  const auto& device=*r.board().device(1);assert(r.board().deviceBus(1)==100);
  const auto& base=device.touch();assert(base.width==480 && base.height==800 && base.address==93);
  const auto bit=[](unsigned n){return uint64_t(1)<<n;};
  assert(p.gpios_[0].input==bit(10));assert(p.gpios_[0].pullup==(scenario==12?bit(10):0));
  const uint64_t outputs=bit(4)|((scenario && scenario!=11)?bit(2)|bit(10):0);assert(p.gpios_[0].output==outputs);
  if(scenario){const auto& v=device.config.touchPowered;assert(base.struct_size==sizeof(v) && device.hardware.config_size==sizeof(v));for(auto x:v.reserved)assert(!x);}
  else assert(base.struct_size==sizeof(risc_hw_i2c_touch_v1) && device.hardware.config_size==sizeof(risc_hw_i2c_touch_v1));
  uint64_t token=0;auto& api=p.gpios_[0].api.base;assert(!api.claim(api.context,39,true,false,false,&token) && !token && !io);
  assert(api.claim(api.context,10,false,false,scenario==12,&token));assert(api.release(api.context,token));
  if(outputs&bit(10)){assert(api.claim(api.context,10,true,false,false,&token));assert(api.release(api.context,token));}
  else assert(!api.claim(api.context,10,true,false,false,&token));
 }
 puts("Typed touch v2: unchanged v1 prefix/scope, explicit rail/IRQ authority, alternate address reservation, invalid configs and no preflight I/O PASS");
}
