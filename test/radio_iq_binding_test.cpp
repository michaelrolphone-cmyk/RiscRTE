#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
#include <filesystem>
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
 save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-test","revision":"unspecified","buses":[],"devices":[]})");
 auto boot=[&](bool selected){save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+(selected?R"({"manifest":"tx.json"})":"")+"]}");};
 save("tx.json",R"({"type":"driver","id":"generic-iq","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"platform.radio.iq.resource","api":1}],"provides":[{"capability":"test.output","api":1}]})");
 auto prepared=[&](Hardware h,bool expected,size_t tables){
  Port p(h);activePort=&p;RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
  assert(r.prepare(root.c_str())==expected);assert(p.iqCount_==tables && !io);
  if(tables){assert(p.iq_.api.claim && p.iq_.api.release && p.iq_.api.bank_base==0x3FCB0000u && p.iq_.api.bank_bytes==65536u && p.quiescent());}
 };
 boot(true);prepared(hardware(),true,1);
 // Native-first old graph has no selected IQ provider, but retains the table
 // for later side-effect-free full-cohort admission with identical board JSON.
 boot(false);prepared(hardware(),true,1);
 auto absent=hardware();absent.radioIqReady=nullptr;prepared(absent,true,0);
 boot(true);prepared(absent,false,0);
 auto missing=hardware();missing.radioIdle=nullptr;prepared(missing,false,0);
 missing=hardware();missing.hciIdle=nullptr;prepared(missing,false,0);
 missing=hardware();missing.hciSafe=nullptr;prepared(missing,false,0);
 save("tx.json",R"({"type":"driver","id":"bad","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"platform.radio.iq.resource","api":1},{"capability":"absent.dependency","api":1}],"provides":[{"capability":"test.output","api":1}]})");
 prepared(hardware(),false,1);
 // A raw Global0 table never becomes an ordinary app permission.
 save("app.json",R"({"type":"application","id":"raw-denied","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"platform.radio.iq.resource","api":1}]})");
 save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"platform.radio.iq.resource","api":1,"instance_id":0}]}]})");
 prepared(hardware(),false,1);
 // Real full-cohort admission copies the native-first CPU table without
 // rebinding the live port or running a provider/claim/native proof callback.
 namespace fs=std::filesystem;
 const auto old=fs::path(root)/"old-cohort",next=fs::path(root)/"next-cohort";
 for(const auto& dir:{old,next}){
  fs::create_directories(dir);
  std::ofstream(dir/"board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"generic-test","revision":"unspecified","buses":[],"devices":[]})";
  std::ofstream(dir/"cohort.json")<<"{}";
  std::ofstream(dir/"app.json")<<R"({"type":"application","id":"clock","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})";
  std::ofstream(dir/"default.elf")<<"not executed";
  std::ofstream(dir/"boot.json")<<std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+
    (dir==next?R"({"manifest":"tx.json"})":"")+R"(],"app_capabilities":[{"manifest":"app.json","grants":[]}]})";
 }
 std::ofstream(next/"tx.json")<<R"({"type":"driver","id":"generic-iq","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"platform.radio.iq.resource","api":1}],"provides":[{"capability":"test.output","api":1}]})";
 std::ofstream(next/"driver.elf")<<"not executed";
 Port p(hardware());activePort=&p;
 RiscBoot::Runtime current({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
 RiscBoot::Runtime candidate({});assert(current.prepare(old.c_str()));
 assert(current.validateCohort(candidate,next.c_str(),[](void*,const char*,bool){return true;},nullptr) && !io);
 puts("IQ global CPU resource: opt-in registration, no selected hardware or bind-time I/O, native-first graph continuity and unavailable backend refusal PASS");
}
