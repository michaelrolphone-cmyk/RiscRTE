#define private public
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
#include "RuntimeFileFixture.h"
using namespace RiscCpu;
static Port* activePort;
static unsigned io=0;
static bool owner(){return true;}
static bool backendAvailable=true;static unsigned written=0;
static risc_psram_cache_v1 cache{1,sizeof(cache),32,nullptr,[](void*,uintptr_t p,size_t n)->int32_t{++written;return p==0x3c000000 && n==4096?0:-1;}};
static bool bind(RiscBoot::Runtime&r){return activePort->bind(r);}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){++io;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++io;return true;};h.gpioWrite=[](uint8_t,bool){++io;return true;};
 h.gpioRead=[](uint8_t,bool*){++io;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++io;return true;};h.gpioClose=[](uint8_t){++io;return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++io;return true;};h.i2cClose=[](uint8_t){++io;return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++io;return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++io;return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++io;return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++io;return true;};h.spiClose=[](uint8_t){++io;return true;};
 h.hciOpen=[](){++io;return true;};h.hciClose=[](){++io;return true;};
 h.hciSend=[](uint8_t,const uint8_t*,size_t,uint32_t){++io;return true;};h.hciReceive=[](uint8_t*,uint8_t*,size_t,size_t*,uint32_t){++io;return true;};
 h.hciIdle=[](){return true;};h.hciSafe=[](){return true;};h.psramCache=backendAvailable?&cache:nullptr;return h;
}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 auto save=[&](const char*path,const std::string&data){std::ofstream(root+"/"+path)<<data;};
 save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-test","revision":"unspecified","buses":[],"devices":[{"instance_id":1,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[8],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}},{"instance_id":2,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[9],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
 const std::string manifest=R"({"type":"driver","id":"cache-test","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.psram-cache","api":1}],"provides":[{"capability":"test.output","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})";
 save("driver.json",manifest);
 auto prepare=[&](const std::string&drivers,size_t expected,bool valid=true){
  save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+drivers+"]}");
  Port p(hardware());activePort=&p;
  RiscTest::FileSources rFiles0({root.c_str()});RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});assert(rFiles0.bind(r));
  assert(r.prepare(root.c_str())==valid);assert(!io);
  size_t seen=0;
  for(size_t n=0;n<r.platformCount_;++n){auto& entry=r.platforms_[n];
   if(std::string(entry.capability)!=RISC_PSRAM_CACHE_CAPABILITY)continue;
   ++seen;assert(entry.scope==RiscBoot::Runtime::Scope::Device && entry.id>=1 && entry.id<=2);
   const auto* api=static_cast<const risc_psram_cache_v1*>(entry.table);
   assert(api==&cache && api->writeback(api->context,0x3c000000,4096)==0);
  }
  assert(seen==expected);

 };
 prepare("",0);
 backendAvailable=false;prepare(R"({"manifest":"driver.json","instance_id":1})",0,false);backendAvailable=true;
 prepare(R"({"manifest":"driver.json","instance_id":1})",1);
 prepare(R"({"manifest":"driver.json","instance_id":1},{"manifest":"driver.json","instance_id":2})",2);
 auto bad=manifest;auto pos=bad.find("platform.psram-cache\",\"api\":1");assert(pos!=std::string::npos);
 bad.replace(pos,std::string("platform.psram-cache\",\"api\":1").size(),"platform.psram-cache\",\"api\":2");save("driver.json",bad);
 prepare(R"({"manifest":"driver.json","instance_id":1})",0,false);
 save("driver.json",manifest);
 save("app.json",R"({"type":"application","id":"raw-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"platform.psram-cache","api":1}]})");
 save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"driver.json","instance_id":1}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"platform.psram-cache","api":1,"instance_id":1}]}]})");
 Port forbidden(hardware());activePort=&forbidden;
 RiscTest::FileSources appRuntimeFiles1({root.c_str()});RiscBoot::Runtime appRuntime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});assert(appRuntimeFiles1.bind(appRuntime));
 assert(!appRuntime.prepare(root.c_str()) && !io);
 assert(std::string(appRuntime.error())=="raw platform capability denied to app");
 cache.api_version=2;prepare(R"({"manifest":"driver.json","instance_id":1})",0,false);cache.api_version=1;
 cache.struct_size=4;prepare(R"({"manifest":"driver.json","instance_id":1})",0,false);cache.struct_size=sizeof(cache);
 auto callback=cache.writeback;cache.writeback=nullptr;prepare(R"({"manifest":"driver.json","instance_id":1})",0,false);cache.writeback=callback;
 cache.cache_line_bytes=0;prepare(R"({"manifest":"driver.json","instance_id":1})",0,false);
 cache.cache_line_bytes=3;prepare(R"({"manifest":"driver.json","instance_id":1})",0,false);cache.cache_line_bytes=32;
 assert(written==3);
 puts("PSRAM actual Runtime/CpuPort selected graph binding, malformed backend and raw-app refusals PASS");
}
