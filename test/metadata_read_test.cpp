#include "bootstrap/Runtime.h"
#include "runtime/update/Cohort.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs=std::filesystem;
using namespace RiscBoot;
namespace {
struct Fault {
  std::string path;
  FILE* stream=nullptr;
  unsigned occurrence=1,matches=0,opens=0,closes=0,reads=0,allocations=0;
  bool close=false,read=false,allocation=false;
} fault;
unsigned allOpens=0,inspections=0,unloads=0;
Runtime* liveRuntime=nullptr;
bool liveClose=false;
unsigned liveCalls=0;
void arm(const fs::path& path,bool close=false,bool read=false,bool allocation=false,unsigned occurrence=1){
  assert(!fault.stream);fault={};fault.path=path.string();fault.close=close;
  fault.read=read;fault.allocation=allocation;fault.occurrence=occurrence;
}
void consumed(){assert(!fault.stream && fault.opens==1 && fault.closes==1);}
void save(const fs::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;assert(file.good());}
void save(const fs::path& path,const JsonDocument& doc){std::string bytes;serializeJson(doc,bytes);save(path,bytes);}
bool owner(){return true;}
bool health(risc_runtime_health_v1*){return true;}
void delay(uint32_t){}
bool log(const char*){return true;}
Port port(){return {owner,health,delay,log};}
bool inspect(void*,const char*,bool){++inspections;return true;}
const char* board=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"rev1","buses":[],"devices":[]})";
const char* boot=R"({"board":"board.json","default_app":"app.elf","drivers":[{"manifest":"probe.json"}],"app_capabilities":[{"manifest":"app.json","grants":[]}]})";
const char* driver=R"({"type":"driver","id":"probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"probe.elf","requires":[],"provides":[{"capability":"test.probe","api":1}]})";
const char* app=R"({"type":"application","id":"app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"app.elf","entry":"app_main","requires":[]})";
void store(const fs::path& root){
  fs::create_directories(root);save(root/"boot.json",boot);save(root/"board.json",board);
  save(root/"probe.json",driver);save(root/"app.json",app);
  save(root/"cohort.json","{}");save(root/"app.elf","app");save(root/"probe.elf","provider");
}
struct NoJsonMemory final : ArduinoJson::Allocator {
  void* allocate(size_t) override{return nullptr;}
  void deallocate(void*) override{}
  void* reallocate(void*,size_t) override{return nullptr;}
};
}
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" int __real_fclose(FILE*);
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
extern "C" int __real_feof(FILE*);
extern "C" int __real_ferror(FILE*);
extern "C" void* __real_malloc(size_t);
extern "C" int __real_dlclose(void*);
extern "C" int __wrap_dlclose(void* module){++unloads;return __real_dlclose(module);}
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){
  FILE* stream=__real_fopen(path,mode);if(stream)++allOpens;
  if(stream && fault.path==path && ++fault.matches==fault.occurrence){
    assert(!fault.stream);fault.stream=stream;++fault.opens;
  }
  return stream;
}
extern "C" int __wrap_fclose(FILE* stream){
  const bool selected=stream==fault.stream;
  if(selected){++fault.closes;fault.stream=nullptr;}
  const int result=__real_fclose(stream);
  // Real host resources are released once; the injected error models native
  // uncertainty without intentionally leaking descriptors into the host test.
  if(selected && fault.close){errno=EIO;return EOF;}
  return result;
}
extern "C" size_t __wrap_fread(void* data,size_t size,size_t count,FILE* stream){
  if(stream==fault.stream){++fault.reads;if(fault.read){errno=EIO;return 0;}}
  return __real_fread(data,size,count,stream);
}
extern "C" int __wrap_feof(FILE* stream){
  return stream==fault.stream && fault.read?1:__real_feof(stream);
}
extern "C" int __wrap_ferror(FILE* stream){
  return stream==fault.stream && fault.read?1:__real_ferror(stream);
}
extern "C" void* __wrap_malloc(size_t bytes){
  if(fault.stream && bytes==65537){++fault.allocations;if(fault.allocation)return nullptr;}
  return __real_malloc(bytes);
}
extern "C" void test_cohort_update(){
  assert(liveRuntime && liveRuntime->active());++liveCalls;
  char bytes[4096]{};uint32_t actual=0;
  assert(liveRuntime->appInventory(0,bytes,sizeof(bytes),&actual)==!liveClose);
  consumed();assert(liveRuntime->metadataCloseRetained()==liveClose);
  if(!liveClose){assert(actual && strstr(bytes,"application"));return;}
  assert(!actual);const unsigned opened=allOpens;
  Runtime::UpdateApp update;
  assert(!liveRuntime->appInventory(0,bytes,sizeof(bytes),&actual));
  assert(!liveRuntime->appUpdate("app",app,strlen(app),update));
  assert(!liveRuntime->launch("app.elf") && !liveRuntime->confirmBoot());
  assert(allOpens==opened && fault.closes==1);
}
static void jsonReads(const fs::path& root){
  const fs::path input=root/"read.json";save(input,"{\"valid\":true}");
  for(bool close:{false,true})for(bool read:{false,true})for(bool allocation:{false,true}){
    arm(input,close,read,allocation);bool retained=false;JsonDocument doc;
    assert(readJson(input.c_str(),doc,&retained)==(!close && !read && !allocation));
    consumed();assert(retained==close && fault.allocations==1);
    assert(allocation?fault.reads==0:fault.reads>0);
  }
  arm(input,true);JsonDocument defaultApi;assert(!readJson(input.c_str(),defaultApi));consumed();
  bool retained=true;arm(input);JsonDocument good;assert(readJson(input.c_str(),good,&retained));
  consumed();assert(retained && good["valid"].as<bool>());
  arm(root/"absent.json");assert(!readJson(fault.path.c_str(),good,&retained));
  assert(retained && !fault.opens && !fault.closes);
  retained=false;assert(!readJson(fault.path.c_str(),good,&retained));assert(!retained);
  for(const std::string& invalid:{std::string("{"),std::string(65537,' ')})for(bool close:{false,true}){
    save(input,invalid);arm(input,close);retained=false;JsonDocument doc;
    assert(!readJson(input.c_str(),doc,&retained));consumed();assert(retained==close);
  }
  save(input,"{\"valid\":true}");NoJsonMemory allocator;
  for(bool close:{false,true}){
    arm(input,close);retained=false;JsonDocument doc(&allocator);
    assert(!readJson(input.c_str(),doc,&retained));consumed();assert(retained==close);
  }
  puts("JSON metadata: normal/default API, open/read/parse/size errors, buffer/parser OOM, sticky close retention and single-close cleanup PASS");
}
static void preparation(const fs::path& root){
  store(root);
  for(const char* name:{"boot.json","board.json","probe.json","app.json"}){
    for(bool close:{false,true})for(unsigned failure=0;failure<3;++failure){
      arm(root/name,close,failure==1,failure==2);
      auto runtime=std::make_unique<Runtime>(port());inspections=0;
      const bool accepted=runtime->prepare(root.c_str());
      assert(accepted==(!close && failure==0));consumed();
      assert(runtime->metadataCloseRetained()==close && !runtime->retained() && !runtime->active());
      if(accepted){assert(runtime->inspectImages(inspect,nullptr) && inspections==2);continue;}
      const unsigned opened=allOpens;
      assert(!runtime->prepare(root.c_str()));
      assert(!runtime->inspectImages(inspect,nullptr) && !inspections);
      assert(!runtime->run() && !runtime->launch("app.elf"));
      assert(allOpens==opened && runtime->metadataCloseRetained()==close);
      // The failed candidate has no executed providers. Its metadata may be
      // destroyed once the store owner has latched the independent close flag.
      runtime.reset();assert(allOpens==opened && fault.closes==1);
    }
  }
  arm(root/"unused");auto fresh=std::make_unique<Runtime>(port());
  assert(fresh->prepare(root.c_str()) && !fresh->metadataCloseRetained());
  puts("Runtime preparation: boot/board/driver/app normal, read/OOM and retained-close matrix; no retry, inspection, launch or cross-instance state PASS");
}
static void cohortReads(const fs::path& old,const fs::path& next){
  store(old);store(next);arm(old/"unused");auto current=std::make_unique<Runtime>(port());
  assert(current->prepare(old.c_str()));
  // Both the initial comparison reads and prepare's second candidate reads
  // belong to the same admission attempt, including source metadata failures.
  for(const fs::path& path:{old/"boot.json",old/"board.json",next/"boot.json",next/"board.json",next/"probe.json",next/"app.json"}){
    const unsigned count=(path==next/"boot.json" || path==next/"board.json")?2:1;
    for(unsigned occurrence=1;occurrence<=count;++occurrence){
      arm(path,true,false,false,occurrence);auto candidate=std::make_unique<Runtime>(Port{});inspections=0;
      assert(!current->validateCohort(*candidate,next.c_str(),inspect,nullptr));consumed();
      assert(candidate->metadataCloseRetained() && !current->metadataCloseRetained() && !inspections);
      const unsigned opened=allOpens;
      assert(!current->validateCohort(*candidate,next.c_str(),inspect,nullptr));
      assert(!candidate->prepare(next.c_str()) && !candidate->run());
      assert(allOpens==opened && fault.closes==1);
    }
  }
  arm(next/"unused");auto candidate=std::make_unique<Runtime>(Port{});inspections=0;
  assert(current->validateCohort(*candidate,next.c_str(),inspect,nullptr));
  assert(!candidate->metadataCloseRetained() && inspections==2);
  for(bool close:{false,true}){
    arm(next/"cohort.json",close);bool retained=false;RiscUpdate::CohortIdentity identity;
    assert(!RiscUpdate::readCohort(next.c_str(),identity,&retained));consumed();assert(retained==close);
  }
  JsonDocument identity;identity["schema"]="riscrte.cohort";identity["schema_version"]=1;
  identity["product"]="test-product";identity["version"]="1.0.0";identity["runtime_version"]="1.0.0";
  identity["source_repo"]="owner/product";identity["source_revision"]=std::string(40,'a');
  identity["layout"]=RiscUpdate::Layout;identity["store_abi"]=RiscUpdate::StoreAbi;
  identity["firmware_size"]=32;identity["firmware_sha256"]=std::string(64,'0');
  save(old/"cohort.json",identity);identity["version"]="2.0.0";save(next/"cohort.json",identity);
  for(bool close:{false,true}){
    arm(next/"cohort.json",close);bool retained=false;RiscUpdate::CohortIdentity parsed;
    assert(RiscUpdate::readCohort(next.c_str(),parsed,&retained)==!close);consumed();assert(retained==close);
  }
  JsonDocument migrationBoot;assert(parse(boot,strlen(boot),migrationBoot));
  auto migration=migrationBoot["cohort_migration"].to<JsonObject>();migration["schema"]=1;
  migration["from"]["product"]="test-product";migration["from"]["version"]="1.0.0";
  migration["from"]["source_revision"]=std::string(40,'a');
  migration["to"]["product"]="test-product";migration["to"]["version"]="2.0.0";
  auto shared=migration["shared_key_value"].to<JsonArray>().add<JsonObject>();
  shared["application_id"]="new-app";shared["api"]=1;shared["namespace"]=1;save(next/"boot.json",migrationBoot);
  for(const fs::path& path:{old/"cohort.json",next/"cohort.json"}){
    arm(path,true);candidate=std::make_unique<Runtime>(Port{});inspections=0;
    assert(!current->validateCohort(*candidate,next.c_str(),inspect,nullptr));consumed();
    assert(candidate->metadataCloseRetained() && !current->metadataCloseRetained() && !inspections);
  }
  puts("Cohort metadata: source/candidate comparison, repeated preparation reads and identity helper retain on candidate before native cleanup PASS");
}
static void liveReads(const fs::path& root,const fs::path& fixture){
  store(root);JsonDocument config;assert(parse(boot,strlen(boot),config));
  config["drivers"].to<JsonArray>();save(root/"boot.json",config);
  fs::copy_file(fixture,root/"app.elf",fs::copy_options::overwrite_existing);
  for(const char* name:{"boot.json","app.json"})for(bool close:{false,true}){
    arm(root/"unused");auto runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(root.c_str()));
    arm(root/name,close);liveRuntime=runtime.get();liveClose=close;liveCalls=0;
    const unsigned previousUnloads=unloads;
    assert(runtime->run()==!close && liveCalls==1);
    assert(unloads==previousUnloads+(close?0:1));
    assert(runtime->metadataCloseRetained()==close && runtime->retained()==close && !runtime->active());
    if(close){const unsigned opened=allOpens;assert(!runtime->run() && allOpens==opened);}
    liveRuntime=nullptr;
  }
  puts("Live metadata: inventory boot/manifest close failures block repeat reads, updates, launch, confirmation and invocation teardown PASS");
}
int main(int argc,char** argv){
  assert(argc==2);const fs::path root=argv[1];jsonReads(root);
  preparation(root/"prepare");cohortReads(root/"old",root/"next");
  liveReads(root/"live",root/"metadata-app.elf");
}
