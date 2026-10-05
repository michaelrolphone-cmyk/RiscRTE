#include "bootstrap/Runtime.h"
#include "runtime/storage/AppDataFiles.h"
#include <cassert>
#include <fstream>
#include <string>
#include <sys/stat.h>
static bool owned=true,retained=false,fault_mode=false;static int phase=0;static risc_app_data_v1 saved{};
extern "C" void app_data_test_owner(int n){owned=n;}
extern "C" int app_data_test_phase(){return phase;}
extern "C" int app_data_test_fault_mode(){return fault_mode;}
extern "C" void app_data_test_fault(){retained=true;}
extern "C" void app_data_test_next(){phase++;}
extern "C" void app_data_test_keep(risc_app_data_v1 v){saved=v;}
extern "C" void app_data_test_revoked(){if(saved.stat){uint32_t s=0;uint64_t r=0;assert(saved.stat(saved.context,"state.json",&s,&r)==RISC_APP_DATA_CONTEXT);}}
static bool owner(){return owned;}static bool health(risc_runtime_health_v1*){return true;}static void delay(uint32_t){}static bool log(const char*){return true;}
static void write(const std::string&p,const std::string&s){std::ofstream f(p);f<<s;assert(f.good());}
int main(int argc,char**argv){assert(argc==2);std::string root=argv[1];
 RiscStorage::AppDataFiles files({nullptr,[](void*){return 0u;},[](void*){return true;},malloc,free});
 std::string data=root+"/appdata";assert(mkdir(data.c_str(),0700)==0 && files.configure(data.c_str()));
 RiscBoot::AppDataBackend backend{&files,
 [](void*c,uint32_t n,const char*p,uint32_t*s,uint64_t*r){return static_cast<RiscStorage::AppDataFiles*>(c)->stat(n,p,s,r);},
 [](void*c,uint32_t n,const char*p,uint64_t v,void*b,uint32_t z,uint32_t*s,uint64_t*r){return static_cast<RiscStorage::AppDataFiles*>(c)->read(n,p,v,b,z,s,r);},
 [](void*c,uint32_t n,const char*p,uint64_t v,const void*b,uint32_t z){return static_cast<RiscStorage::AppDataFiles*>(c)->replace(n,p,v,b,z);},
 [](void*c){return !retained && static_cast<RiscStorage::AppDataFiles*>(c)->exitSafe();}};
 RiscBoot::Port port{owner,health,delay,log};port.appData=&backend;
 port.bindPlatforms=[](RiscBoot::Runtime&r){static const uint32_t clockTable[2]={1,8};return r.registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,clockTable);};
 write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 const std::string manifest=R"({"type":"application","id":"files","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"storage.app-data","api":1},{"capability":"platform.clock","api":1}]})";
 write(root+"/app.json",manifest);
 auto boot=[&](unsigned api,unsigned id){write(root+"/boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"storage.app-data","api":)")+std::to_string(api)+",\"instance_id\":"+std::to_string(id)+"},{\"capability\":\"platform.clock\",\"api\":1,\"instance_id\":0}]}]}");};
 boot(1,0);{RiscBoot::Runtime r(port);assert(!r.prepare(root.c_str()));}
 boot(2,1);{RiscBoot::Runtime r(port);assert(!r.prepare(root.c_str()));}
 boot(1,1);{RiscBoot::Runtime r({owner,health,delay,log});assert(!r.prepare(root.c_str()));}
 for(unsigned repeat=0;repeat<2;repeat++){phase=0;RiscBoot::Runtime r(port);assert(r.prepare(root.c_str()));assert(r.run());assert(phase==2);app_data_test_revoked();}
 std::string child=manifest;child.replace(child.find("\"files\""),7,"\"child\"");child.replace(child.find("default.elf"),11,"child.elf");write(root+"/child.json",child);
 write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"storage.app-data","api":1,"instance_id":1},{"capability":"platform.clock","api":1,"instance_id":0}]},{"manifest":"child.json","grants":[{"capability":"storage.app-data","api":1,"instance_id":1}]}]})");
 {RiscBoot::Runtime r(port);assert(!r.prepare(root.c_str()) && strstr(r.error(),"already owned"));}
 boot(1,1);fault_mode=true;{RiscBoot::Runtime r(port);assert(r.prepare(root.c_str()));assert(!r.run() && r.retained());assert(strstr(r.error(),"retention barrier"));app_data_test_revoked();}
 puts("App-data Runtime/actual ELF authority, owner, namespace uniqueness, lifetime/reacquire, handoff and pre-fini retention PASS");
}
