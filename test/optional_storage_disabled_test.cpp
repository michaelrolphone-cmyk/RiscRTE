#include "bootstrap/Runtime.h"
#include <cassert>
#include <fstream>
#include <string>
#include "RuntimeFileFixture.h"
static_assert(RISC_RUNTIME_PREFERENCE_STORE==0,"Preferences must be explicit opt-in");
static_assert(RISC_RUNTIME_STORAGE_FILES==0,"Files must be explicit opt-in");
static bool owner(){return true;}
static bool health(risc_runtime_health_v1*){return true;}
static bool logLine(const char*){return true;}
static void delay(uint32_t){}
int main(int argc,char** argv){
 assert(argc==2);std::string root=argv[1];
 auto file=[&](const char* name,const std::string& value){std::ofstream(root+"/"+name)<<value;};
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 for(const char* capability:{"","storage.preferences","storage.files","storage.files.catalog"}){
  std::string request=*capability?"{\"capability\":\""+std::string(capability)+"\",\"api\":1}":"";
  file("app.json","{\"type\":\"application\",\"id\":\"minimal\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\"default.elf\",\"entry\":\"app_main\",\"requires\":["+request+"]}");
  if(*capability)request="{\"capability\":\""+std::string(capability)+"\",\"api\":1,\"instance_id\":1}";
  file("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":[],\"app_capabilities\":[{\"manifest\":\"app.json\",\"grants\":["+request+"]}]}");
  RiscTest::FileSources rFiles0({root.c_str()});RiscBoot::Runtime r({owner,health,delay,logLine});assert(rFiles0.bind(r));assert(r.prepare(root.c_str())==!*capability);
 }
 puts("Minimal Runtime: no storage provider required; disabled preferences/files/catalog denied PASS");
}
