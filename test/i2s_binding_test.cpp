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
 h.i2sOpen=[](uint8_t,uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2sWrite=[](uint8_t,const int16_t*,size_t,size_t*,uint32_t){++io;return true;};h.i2sClose=[](uint8_t){++io;return true;};h.i2sOpenRx=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2sRead=[](uint8_t,int16_t*,size_t,size_t*,uint32_t){++io;return true;};return h;
}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 auto save=[&](const char*path,const std::string&data){std::ofstream(root+"/"+path)<<data;};
 const std::string config=R"({"controller":1,"pdm_rx":false,"bclk":7,"ws":8,"data":9})";
 auto board=[&](const std::string&c){save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-test","revision":"unspecified","buses":[],"devices":[{"instance_id":12,"chip":{"vendor":"test","model":"tx","revision":"unspecified"},"compatible":"test,tx","config_type":"audio.i2s","config_version":1,"config":)"+c+"}]}");};
 auto boot=[&](bool selected){save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+(selected?R"({"manifest":"tx.json","instance_id":12})":"")+"]}");};
 save("tx.json",R"({"type":"driver","id":"generic-tx","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.i2s.controller","api":1}],"provides":[{"capability":"test.output","api":1}],"hardware_compatibility":[{"compatible":"test,tx","revisions":["unspecified"],"config_type":"audio.i2s","config_version":1}]})");
 auto prepared=[&](Hardware h,bool expected,size_t tables,bool rx=false){
  Port p(h);activePort=&p;RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
  assert(r.prepare(root.c_str())==expected);assert(p.i2sCount_==tables && !io);
  if(tables){assert(p.i2ss_[0].config.controller==(rx?0:1) && p.i2ss_[0].config.pdm_rx==rx && p.i2ss_[0].config.bclk==7 && p.i2ss_[0].config.ws==(rx?-1:8) && p.i2ss_[0].config.data==9);assert(p.i2ss_[0].api.read && p.quiescent());}
 };
 board(config);boot(true);prepared(hardware(),true,1);
 boot(false);prepared(hardware(),true,0);
 boot(true);auto missing=hardware();missing.i2sWrite=nullptr;prepared(missing,false,0);
 board(R"({"controller":0,"pdm_rx":true,"bclk":7,"ws":-1,"data":9})");prepared(hardware(),true,1,true);
 missing=hardware();missing.i2sRead=nullptr;prepared(missing,false,0);
 missing=hardware();missing.i2sOpenRx=nullptr;prepared(missing,false,0);
 missing=hardware();missing.i2sOpen=nullptr;missing.i2sWrite=nullptr;prepared(missing,true,1,true);
 board(R"({"controller":1,"pdm_rx":true,"bclk":7,"ws":-1,"data":9})");prepared(hardware(),false,0);
 board(R"({"controller":0,"pdm_rx":true,"bclk":7,"ws":8,"data":9})");prepared(hardware(),false,0);
 board(R"({"controller":1,"pdm_rx":false,"bclk":7,"ws":7,"data":9})");prepared(hardware(),false,0);
 puts("Actual JSON/manifest admission: selected typed I2S table, no unselected table, TX/PDM direction-specific backends, unsupported RX/bad pads reject before hardware I/O PASS");
}
