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
static unsigned io=0,prepareCalls=0,asyncBegins=0;static bool prepareOk=true;static size_t expectedAsync=0;
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime&r){return activePort->bind(r);}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){++io;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++io;return true;};h.gpioWrite=[](uint8_t,bool){++io;return true;};
 h.gpioRead=[](uint8_t,bool*){++io;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++io;return true;};h.gpioClose=[](uint8_t){++io;return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++io;return true;};h.i2cClose=[](uint8_t){++io;return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++io;return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++io;return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++io;return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++io;return true;};h.spiClose=[](uint8_t){++io;return true;};
 h.radioJoin=[](const char*,const char*){++io;return true;};h.radioState=[](uint8_t*,int8_t*){++io;return true;};h.radioLeave=[](){++io;return true;};
 h.radioAddresses=[](uint8_t*,uint8_t*){++io;return true;};h.radioScanStart=[](){++io;return true;};h.radioScanPoll=[](garden_radio_scan_result_v1*){++io;return true;};h.radioScanCancel=[](){++io;return true;};h.radioIdle=[](){++io;return true;};return h;
}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 auto save=[&](const char*path,const std::string&data){std::ofstream(root+"/"+path)<<data;};
 const std::string config=R"({"unit":0,"features":3})";
 auto board=[&](const std::string&c){save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-test","revision":"unspecified","buses":[],"devices":[{"instance_id":12,"chip":{"vendor":"test","model":"tx","revision":"unspecified"},"compatible":"espressif,esp32s3-wifi","config_type":"radio.integrated","config_version":1,"config":)"+c+"}]}");};
 auto boot=[&](bool selected){save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+(selected?R"({"manifest":"tx.json","instance_id":12})":"")+"]}");};
 save("tx.json",R"({"type":"driver","id":"generic-tx","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.radio","api":1}],"provides":[{"capability":"test.output","api":1}],"hardware_compatibility":[{"compatible":"espressif,esp32s3-wifi","revisions":["unspecified"],"config_type":"radio.integrated","config_version":1}]})");
 auto prepared=[&](Hardware h,bool expected,size_t tables){
  Port p(h);activePort=&p;RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
  assert(r.prepare(root.c_str())==expected);assert(p.radioCount_==tables && !io);
  if(tables && expectedAsync){
   auto& radio=p.radios_[0];assert(radio.asyncUnavailable==!prepareOk);radio.token=71;
   risc_radio_request_v1 request{};request.struct_size=sizeof(request);request.kind=RISC_RADIO_REQUEST_SCAN;
   uint32_t id=99;const unsigned before=asyncBegins;
   assert(radio.api.begin(radio.api.base.context,radio.token,&request,&id)==RISC_RADIO_UNAVAILABLE && !id);
   assert(asyncBegins==before+unsigned(prepareOk) && !io && !radio.active && !radio.closing);
   radio.token=0;
  }
  if(tables){assert(p.radios_[0].config.unit==0 && p.radios_[0].config.features==3);assert(p.radios_[0].api.base.scan_poll && p.quiescent());assert(p.radios_[0].api.base.struct_size==(expectedAsync?sizeof(garden_radio_async_v1):sizeof(garden_radio_v1)));}
 };
 board(config);boot(true);prepared(hardware(),true,1);
 risc_native_radio_async_v1 async{sizeof(async),RISC_RADIO_ASYNC_TAG,1,
   [](const risc_radio_request_v1*,uint32_t*)->int32_t{++asyncBegins;return RISC_RADIO_UNAVAILABLE;},
   [](uint32_t,risc_radio_progress_v1*)->int32_t{return RISC_RADIO_UNAVAILABLE;},
   [](uint32_t)->int32_t{return RISC_RADIO_UNAVAILABLE;},[](){return true;},[](){return true;},[](){return true;},[](){}};
 auto extended=hardware();extended.radioAsync=&async;extended.radioAsyncPrepare=[](){++prepareCalls;return prepareOk;};
 expectedAsync=1;prepared(extended,true,1);assert(prepareCalls==1);
 prepareOk=false;prepared(extended,true,1);assert(prepareCalls==2);prepareOk=true;
 expectedAsync=0;async.struct_size=sizeof(async)-1;prepared(extended,true,1);async.struct_size=sizeof(async);
 async.tag=0;prepared(extended,true,1);async.tag=RISC_RADIO_ASYNC_TAG;
 async.cancel=nullptr;prepared(extended,true,1);assert(prepareCalls==2);
 boot(false);const unsigned beforePrepare=prepareCalls;prepared(extended,true,0);assert(prepareCalls==beforePrepare);
 boot(false);prepared(hardware(),true,0);
 boot(true);auto missing=hardware();missing.radioScanPoll=nullptr;prepared(missing,false,0);
 board(R"({"unit":1,"features":3})");prepared(hardware(),false,0);
 board(R"({"unit":0,"features":2})");prepared(hardware(),false,0);
 board(R"({"unit":0,"features":4})");prepared(hardware(),false,0);
 board(R"({"unit":0,"features":3,"extra":true})");prepared(hardware(),false,0);
 board(config);save("tx.json",R"({"type":"driver","id":"bad","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.radio","api":1},{"capability":"absent.dependency","api":1}],"provides":[{"capability":"test.output","api":1}],"hardware_compatibility":[{"compatible":"espressif,esp32s3-wifi","revisions":["unspecified"],"config_type":"radio.integrated","config_version":1}]})");
 // Binding may build an inert table before graph validation rejects the absent dependency.
 prepared(hardware(),false,1);
 puts("Actual JSON/manifest radio admission: exact selected config, no unselected table, unavailable backend and malformed graph reject before hardware I/O PASS");
}
