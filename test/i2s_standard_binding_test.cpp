#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
using namespace RiscCpu;
static Port *activePort;
static bool ownerOK=true,openOK=true,closeOK=true,ioOK=true;
static unsigned calls=0;
static uint64_t now=0;
static size_t accepted=256;
static bool owner(){return ownerOK;}
static bool bind(RiscBoot::Runtime& r){return activePort->bind(r);}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[](){return now;};h.sleep=[](uint32_t ms){now+=ms;++calls;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++calls;return true;};h.gpioWrite=[](uint8_t,bool){++calls;return true;};h.gpioRead=[](uint8_t,bool*){++calls;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++calls;return true;};h.gpioClose=[](uint8_t){++calls;return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++calls;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++calls;return true;};h.i2cClose=[](uint8_t){++calls;return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++calls;return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++calls;return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++calls;return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++calls;return true;};h.spiClose=[](uint8_t){++calls;return true;};
 h.i2sStandardOpen=[](const risc_hw_i2s_standard_v1& c,uint8_t mode,uint32_t rate){assert(c.controller==0&&c.mclk==16&&c.bclk==41&&c.ws==45&&c.data_out==40&&c.data_in==42&&mode==3&&rate==16000);++calls;return openOK;};
 h.i2sWrite=[](uint8_t unit,const int16_t*,size_t n,size_t* done,uint32_t ms){assert(unit==0&&n==256&&ms==40);++calls;*done=accepted;return ioOK;};
 h.i2sRead=[](uint8_t unit,int16_t*,size_t n,size_t* done,uint32_t ms){assert(unit==0&&n==256&&ms==40);++calls;*done=accepted;return ioOK;};
 h.i2sClose=[](uint8_t unit){assert(unit==0);++calls;return closeOK;};return h;
}
int main(int argc,char** argv){
 assert(argc==2);const std::string root=argv[1];
 auto save=[&](const char* path,const std::string& data){std::ofstream(root+"/"+path)<<data;};
 const std::string config=R"({"bus_instance_id":100,"controller":0,"mclk":16,"bclk":41,"ws":45,"data_out":40,"data_in":42,"enable":46,"enable_active_high":true,"output_address":24,"input_address":64})";
 auto board=[&](const std::string& c,const std::string& extra=""){
  save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-codec-test","revision":"unspecified","buses":[{"instance_id":100,"kind":"i2c","controller_namespace":"riscrte.logical","controller":0,"physical_controller":0,"frequency_hz":400000,"mode":0,"pins":{"sda":15,"scl":14}}],"devices":[{"instance_id":12,"chip":{"vendor":"test","model":"codec","revision":"unspecified"},"compatible":"test,codec","config_type":"audio.i2s-standard","config_version":1,"config":)"+c+"}"+extra+"]}");
 };
 auto boot=[&](bool selected){save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+(selected?R"({"manifest":"codec.json","instance_id":12})":"")+"]}");};
 save("codec.json",R"({"type":"driver","id":"codec-test","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.i2s.standard","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"audio.pcm","api":1}],"hardware_compatibility":[{"compatible":"test,codec","revisions":["unspecified"],"config_type":"audio.i2s-standard","config_version":1}]})");
 auto prepare=[&](Hardware h,bool ok,size_t count){Port p(h);activePort=&p;calls=0;RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});bool result=r.prepare(root.c_str());if(result!=ok)fprintf(stderr,"prepare error: %s\n",r.error());assert(result==ok&&p.i2sCount_==count&&!calls);};
 board(config);boot(true);prepare(hardware(),true,1);
 boot(false);prepare(hardware(),true,0);boot(true);
 auto h=hardware();h.i2sStandardOpen=nullptr;prepare(h,false,0);
 auto bad=config;bad.replace(bad.find("\"mclk\":16"),9,"\"mclk\":15");board(bad);prepare(hardware(),false,0);
 const std::string legacy=R"(,{"instance_id":13,"chip":{"vendor":"test","model":"legacy","revision":"unspecified"},"compatible":"test,legacy","config_type":"audio.i2s","config_version":1,"config":{"controller":0,"pdm_rx":true,"bclk":10,"ws":-1,"data":9}})";
 board(config,legacy);prepare(hardware(),false,0);
 board(config);Port p(hardware());activePort=&p;RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});assert(r.prepare(root.c_str()));
 auto& state=p.i2ss_[0];auto& api=state.standardApi;uint64_t token=55;
 assert(api.api_version==1&&api.struct_size==sizeof(api));
 assert(!api.open(api.context,4,16000,&token)&&!token);assert(!api.open(api.context,3,12345,&token));
 auto& gpio=p.gpios_[0];assert((gpio.input|gpio.output)==(uint64_t(1)<<46));
 p.pins_[16].owner=&p;assert(!api.open(api.context,3,16000,&token));p.pins_[16]={};
 openOK=false;assert(!api.open(api.context,3,16000,&token)&&!token&&p.quiescent());openOK=true;
 assert(api.open(api.context,3,16000,&token)&&token);assert(!p.quiescent()&&!p.appExitSafe());
 for(unsigned pin:{16,41,45,40,42})assert(p.pins_[pin].owner==&state);
 uint64_t other=0;assert(!api.open(api.context,3,16000,&other));
 int16_t data[512]{};size_t done=55;
 ownerOK=false;unsigned before=calls;assert(!api.write(api.context,token,data,256,&done,40)&&!done&&!api.close(api.context,token));assert(calls==before);ownerOK=true;
 assert(!api.read(api.context,token+1,data,256,&done,40)&&!done);
 assert(!api.write(api.context,token,data,257,&done,40));
 accepted=32;assert(api.read(api.context,token,data,256,&done,40)&&done==32);
 assert(!api.write(api.context,token,data,256,&done,40)&&done==32&&state.closing);
 closeOK=false;assert(!api.close(api.context,token)&&state.token==token);closeOK=true;assert(api.close(api.context,token));
 for(unsigned pin:{16,41,45,40,42})assert(!p.pins_[pin].owner);
 assert(p.quiescent());
 openOK=closeOK=false;assert(!api.open(api.context,3,16000,&token)&&token&&state.closing&&!p.quiescent());
 closeOK=true;assert(api.close(api.context,token)&&p.quiescent());openOK=true;
 assert(api.open(api.context,3,16000,&token));accepted=256;ioOK=false;
 assert(!api.read(api.context,token,data,256,&done,40)&&state.closing);assert(api.close(api.context,token));ioOK=true;
 puts("Actual Board/Runtime/CPU standard I2S: typed binding, no pin leakage, clock collisions, owner/token checks, partial duplex I/O and retained cleanup PASS");
}
