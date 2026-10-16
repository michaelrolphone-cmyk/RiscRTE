#include "bootstrap/Runtime.h"
#include "runtime/storage/AppDataFiles.h"
#include <cassert>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include "RuntimeFileFixture.h"
static unsigned steps=0,healthCalls=0,clockCalls=0;static bool owner=true;
static const risc_runtime_api_v1* savedRuntime;static risc_app_data_v1 saved{};
extern "C" void shared_test_clock(const risc_runtime_api_v1* rt){
 savedRuntime=rt;uint32_t now=0;
 unsigned clocks=clockCalls,health=healthCalls;
 assert(rt->struct_size>=RISC_RUNTIME_MONOTONIC_V1_SIZE && rt->monotonic_ms);
 for(unsigned i=0;i<1000;i++)assert(rt->monotonic_ms(&now)&&now==321);
 assert(clockCalls==clocks+1000 && healthCalls==health);
 assert(!rt->monotonic_ms(nullptr));owner=false;assert(!rt->monotonic_ms(&now));owner=true;
 assert(clockCalls==clocks+1000 && healthCalls==health);
}
extern "C" void shared_test_keep(risc_app_data_v1 v){saved=v;}
extern "C" void shared_test_step(){++steps;}
extern "C" unsigned shared_test_steps(){return steps;}
static void write(const std::string&p,const std::string&s){std::ofstream f(p);f<<s;assert(f.good());}
int main(int argc,char**argv){assert(argc==2);std::string root=argv[1];
 RiscStorage::AppDataFiles files({nullptr,[](void*){return 0u;},[](void*){return true;},malloc,free});
 std::string data=root+"/shared-files";assert(!mkdir(data.c_str(),0700)&&files.configure(data.c_str()));
 RiscBoot::AppDataBackend backend{&files,
 [](void*c,uint32_t n,const char*p,uint32_t*s,uint64_t*r){return static_cast<RiscStorage::AppDataFiles*>(c)->stat(n,p,s,r);},
 [](void*c,uint32_t n,const char*p,uint64_t v,void*b,uint32_t z,uint32_t*s,uint64_t*r){return static_cast<RiscStorage::AppDataFiles*>(c)->read(n,p,v,b,z,s,r);},
 [](void*c,uint32_t n,const char*p,uint64_t v,const void*b,uint32_t z){return static_cast<RiscStorage::AppDataFiles*>(c)->replace(n,p,v,b,z);},
 [](void*c){return static_cast<RiscStorage::AppDataFiles*>(c)->exitSafe();}};
 RiscBoot::Port port{[](){return owner;},[](risc_runtime_health_v1*){++healthCalls;return true;},[](uint32_t){},[](const char*){return true;}};port.appData=&backend;port.monotonicMs=[](uint32_t*out){++clockCalls;*out=321;return true;};
 write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 auto manifest=[](const char*id){return std::string(R"({"type":"application","version":"1.0.0","architecture":"xtensa-esp32s3","entry":"app_main","requires":[{"capability":"storage.shared-data","api":1},{"capability":"storage.app-data","api":1}],"id":")")+id+"\",\"file_name\":\""+id+".elf\"}";};
 write(root+"/default.json",manifest("default"));write(root+"/child.json",manifest("child"));
 auto policy=[](const char*id,unsigned ns,const std::string&file){return std::string("{\"manifest\":\"")+id+".json\",\"grants\":[{\"capability\":\"storage.shared-data\",\"api\":1,\"instance_id\":3"+file+"},{\"capability\":\"storage.app-data\",\"api\":1,\"instance_id\":"+std::to_string(ns)+"}]}";};
 auto boot=[&](const std::string&file){write(root+"/boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[)")+policy("default",63,file)+","+policy("child",64,file)+"]}");};
 for(const char*bad:{"",",\"file\":\"\"",",\"file\":\"../escape\"",",\"file\":\"a/b\""}){boot(bad);RiscTest::FileSources rFiles0({root.c_str()});RiscBoot::Runtime r(port);assert(rFiles0.bind(r));assert(!r.prepare(root.c_str()));}
 boot(",\"file\":\"context-fingerprints.cfp\"");
 // The child has its own private namespace, while the exact shared file is common.
 {RiscTest::FileSources rFiles1({root.c_str()});RiscBoot::Runtime r(port);assert(rFiles1.bind(r));assert(r.prepare(root.c_str()));assert(r.run());assert(steps==2);}
 uint32_t now=0;assert(savedRuntime&&!savedRuntime->monotonic_ms(&now));
 uint32_t size=0;uint64_t revision=0;assert(saved.stat(saved.context,"context-fingerprints.cfp",&size,&revision)==RISC_APP_DATA_CONTEXT);
 puts("Shared-data actual ELF: exact file grants, invalid path rejection, namespace isolation, cross-app persistence, stale revisions and revocation; inexpensive owner clock, null/nonowner/inactive rejection PASS");
}
