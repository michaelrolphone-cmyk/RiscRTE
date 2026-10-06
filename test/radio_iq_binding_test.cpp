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
 h.hciIdle=[](){return true;};h.hciSafe=[](){return true;};h.radioIdle=[](){return true;};h.radioIqReady=[](){++io;return true;};return h;
}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 auto save=[&](const char*path,const std::string&data){std::ofstream(root+"/"+path)<<data;};
 const std::string config=R"({"unit":0,"features":1})";
 auto board=[&](const std::string&c){save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-test","revision":"unspecified","buses":[],"devices":[{"instance_id":12,"chip":{"vendor":"test","model":"tx","revision":"unspecified"},"compatible":"espressif,esp32s3-iq","config_type":"radio.integrated","config_version":1,"config":)"+c+"}]}");};
 auto boot=[&](bool selected){save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+(selected?R"({"manifest":"tx.json","instance_id":12})":"")+"]}");};
 save("tx.json",R"({"type":"driver","id":"generic-tx","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.radio.iq.resource","api":1}],"provides":[{"capability":"test.output","api":1}],"hardware_compatibility":[{"compatible":"espressif,esp32s3-iq","revisions":["unspecified"],"config_type":"radio.integrated","config_version":1}]})");
 auto prepared=[&](Hardware h,bool expected,size_t tables){
  Port p(h);activePort=&p;RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
  assert(r.prepare(root.c_str())==expected);assert(p.iqCount_==tables && !io);
  if(tables){assert(p.iq_.api.claim && p.iq_.api.release && p.iq_.api.bank_base==0x3FCB0000u && p.iq_.api.bank_bytes==65536u && p.quiescent());}
 };
 board(config);boot(true);prepared(hardware(),true,1);
 boot(false);prepared(hardware(),true,0);
 boot(true);auto missing=hardware();missing.radioIqReady=nullptr;prepared(missing,false,0);
 missing=hardware();missing.radioIdle=nullptr;prepared(missing,false,0);
 missing=hardware();missing.hciIdle=nullptr;prepared(missing,false,0);
 missing=hardware();missing.hciSafe=nullptr;prepared(missing,false,0);
 board(R"({"unit":1,"features":1})");prepared(hardware(),false,0);
 board(R"({"unit":0,"features":2})");prepared(hardware(),false,0);
 board(R"({"unit":0,"features":4})");prepared(hardware(),false,0);
 board(R"({"unit":0,"features":1,"extra":true})");prepared(hardware(),false,0);
 board(config);save("tx.json",R"({"type":"driver","id":"bad","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.radio.iq.resource","api":1},{"capability":"absent.dependency","api":1}],"provides":[{"capability":"test.output","api":1}],"hardware_compatibility":[{"compatible":"espressif,esp32s3-iq","revisions":["unspecified"],"config_type":"radio.integrated","config_version":1}]})");
 // Binding may build an inert table before graph validation rejects the absent dependency.
 prepared(hardware(),false,1);
 puts("Actual JSON/manifest IQ resource admission: exact selected config, no unselected table, unavailable backend and malformed graph reject before hardware I/O PASS");
}
