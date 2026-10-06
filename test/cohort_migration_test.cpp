#include "bootstrap/Runtime.h"
#include "runtime/update/Cohort.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
namespace fs=std::filesystem;
using namespace RiscBoot;
static unsigned bindings=0,apps=0,drivers=0;static bool safe=true,elfGood=true;
static bool owner(){return true;}static bool health(risc_runtime_health_v1*){return true;}
static bool log(const char*){return true;}static void delay(uint32_t){}
static bool bind(Runtime& r){++bindings;static uint32_t table[]={1,8};return r.registerPlatform("platform.clock",1,Runtime::Scope::Global,0,table);}
static int32_t get(void*,uint32_t,const char*,void*,uint32_t,uint32_t*){assert(false);return -1;}
static int32_t put(void*,uint32_t,const char*,const void*,uint32_t){assert(false);return -1;}
static KeyValueBackend kv{nullptr,get,put,4096};
static AppDataBackend data{nullptr,
 [](void*,uint32_t,const char*,uint32_t*,uint64_t*){assert(false);return -1;},
 [](void*,uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*){assert(false);return -1;},
 [](void*,uint32_t,const char*,uint64_t,const void*,uint32_t){assert(false);return -1;},
 [](void*){return safe;}};
static Port port(){Port p{owner,health,delay,log,bind,&kv};p.appData=&data;return p;}
static void save(const fs::path& path,const std::string& value){fs::create_directories(path.parent_path());std::ofstream file(path);file<<value;assert(file.good());}
static void save(const fs::path& path,const JsonDocument& doc){std::string s;serializeJson(doc,s);save(path,s);}
static JsonDocument read(const fs::path& path){JsonDocument result;assert(readJson(path.c_str(),result));return result;}
static void store(const fs::path& path,unsigned appCount,unsigned driverCount){
 fs::remove_all(path);fs::create_directories(path);
 save(path/"board.json",std::string(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"watch","revision":"rev1","buses":[],"devices":[]})"));
 save(path/"cohort.json",std::string("{}"));JsonDocument boot;
 boot["board"]="board.json";boot["default_app"]="app0.elf";
 auto ap=boot["app_capabilities"].to<JsonArray>();auto dr=boot["drivers"].to<JsonArray>();
 for(unsigned i=0;i<appCount;++i){
  std::string id="app"+std::to_string(i);JsonDocument manifest;
  manifest["type"]="application";manifest["id"]=id;manifest["version"]="1.0.0";manifest["architecture"]="xtensa-esp32s3";
  manifest["file_name"]=id+".elf";manifest["entry"]="app_main";
  auto requirements=manifest["requires"].to<JsonArray>();auto selection=ap.add<JsonObject>();selection["manifest"]=id+".json";auto grants=selection["grants"].to<JsonArray>();
  for(const char* cap:{"storage.key-value","storage.app-data","platform.clock"}){
   auto req=requirements.add<JsonObject>();req["capability"]=cap;req["api"]=1;
   auto grant=grants.add<JsonObject>();grant["capability"]=cap;grant["api"]=1;grant["instance_id"]=!strcmp(cap,"platform.clock")?0:i+1;
  }
  save(path/(id+".json"),manifest);save(path/(id+".elf"),std::string("app"));
 }
 for(unsigned i=0;i<driverCount;++i){
  std::string id="driver"+std::to_string(i);JsonDocument manifest;
  manifest["type"]="driver";manifest["id"]=id;manifest["version"]="1.0.0";manifest["driver_abi"]=2;manifest["architecture"]="xtensa-esp32s3";manifest["file_name"]="driver.elf";
  auto req=manifest["requires"].to<JsonArray>().add<JsonObject>();req["capability"]="storage.key-value.bound";req["api"]=1;
  auto provides=manifest["provides"].to<JsonArray>().add<JsonObject>();provides["capability"]="test."+id;provides["api"]=1;
  auto selection=dr.add<JsonObject>();selection["manifest"]=id+"/manifest.json";
  auto key=selection["key_value"].to<JsonArray>().add<JsonObject>();key["key"]="state";key["namespace"]=100+i;key["access"]="read-write";
  save(path/id/"manifest.json",manifest);save(path/id/"driver.elf",std::string("provider"));
 }
 save(path/"boot.json",boot);
}
static bool admit(void*,const char* path,bool provider){
 std::ifstream file(path);std::string value;if(!(file>>value))return false;
 if(provider){++drivers;assert(value=="provider");}else{++apps;assert(value=="app");}
 return elfGood;
}
static JsonDocument identity(const char* version,char revision){
 JsonDocument c;c["schema"]="riscrte.cohort";c["schema_version"]=1;c["product"]="twatch-s3";c["version"]=version;
 c["runtime_version"]="0.1.34";c["source_repo"]="owner/watch";c["source_revision"]=std::string(40,revision);
 c["layout"]=RiscUpdate::Layout;c["store_abi"]=RiscUpdate::StoreAbi;c["firmware_size"]=123456;c["firmware_sha256"]=std::string(64,'c');return c;
}
static void universal(const fs::path& path,unsigned count){
 auto boot=read(path/"boot.json");
 for(unsigned i=0;i<count;++i){auto grant=boot["app_capabilities"][i]["grants"].as<JsonArray>().add<JsonObject>();
  grant["capability"]="storage.key-value";grant["api"]=1;grant["instance_id"]=50;}
 save(path/"boot.json",boot);
}
static void migration(JsonDocument& boot,const char* id="app2",uint32_t api=1,uint32_t ns=50){
 auto m=boot["cohort_migration"].to<JsonObject>();m["schema"]=1;
 m["from"]["product"]="twatch-s3";m["from"]["version"]="1.0.2";m["from"]["source_revision"]=std::string(40,'a');
 m["to"]["product"]="twatch-s3";m["to"]["version"]="1.0.3";
 auto entry=m["shared_key_value"].to<JsonArray>().add<JsonObject>();entry["application_id"]=id;entry["api"]=api;entry["namespace"]=ns;
}
static std::string snapshot(const fs::path& root){
 std::vector<fs::path> paths;for(const auto& p:fs::recursive_directory_iterator(root))if(p.is_regular_file())paths.push_back(p.path());
 std::sort(paths.begin(),paths.end());std::string result;
 for(const auto& p:paths){std::ifstream f(p);result+=p.string()+std::string(std::istreambuf_iterator<char>(f),{});}
 return result;
}
int main(int argc,char** argv){
 assert(argc==2);fs::path root=argv[1],old=root/"migration-old",next=root/"migration-next";
 std::unique_ptr<Runtime> runtime;
 auto reset=[&](unsigned oldCount=2,unsigned newCount=3){
  store(old,oldCount,1);store(next,newCount,1);universal(old,oldCount);universal(next,newCount);
  save(old/"cohort.json",identity("1.0.2",'a'));save(next/"cohort.json",identity("1.0.3",'b'));
  auto boot=read(next/"boot.json");migration(boot,oldCount==1?"app1":"app2");save(next/"boot.json",boot);
  bindings=0;runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(old.c_str()) && bindings==1);
 };
 auto check=[&](bool expected){
  apps=drivers=0;const auto before=snapshot(root);auto candidate=std::make_unique<Runtime>(Port{});
  const bool accepted=runtime->validateCohort(*candidate,next.c_str(),admit,nullptr);
  if(accepted!=expected)std::cerr<<"migration expected="<<expected<<" error="<<candidate->error()<<"\n";
  assert(accepted==expected && bindings==1 && before==snapshot(root));
  if(accepted)assert(apps && drivers);
 };
 reset();check(true);
 auto good=read(next/"boot.json");auto b=good;b.remove("cohort_migration");save(next/"boot.json",b);check(false);
 for(unsigned bad=0;bad<19;++bad){b=good;auto m=b["cohort_migration"];
  if(bad==0)m["schema"]=2;
  if(bad==1)m["from"]["product"]="wrong";
  if(bad==2)m["from"]["version"]="1.0.1";
  if(bad==3)m["from"]["source_revision"]=std::string(40,'d');
  if(bad==4)m["to"]["product"]="wrong";
  if(bad==5)m["to"]["version"]="1.0.4";
  if(bad==6)m["shared_key_value"][0]["application_id"]="unlisted";
  if(bad==7)m["shared_key_value"][0]["namespace"]=51;
  if(bad==8)m["shared_key_value"][0]["api"]=2;
  if(bad==9)m["shared_key_value"].as<JsonArray>().add(m["shared_key_value"][0]);
  if(bad==10)m["extra"]=true;
  if(bad==11)m["shared_key_value"][0]["extra"]=1;
  if(bad==12)m["schema"]=true;
  if(bad==13)m["shared_key_value"][0]["namespace"]=0;
  if(bad==14)m["from"]["source_revision"]="bad";
  if(bad==15)m["shared_key_value"].to<JsonArray>();
  if(bad==16)m["shared_key_value"][0]["application_id"]="bad/id";
  if(bad==17)m["to"]["source_revision"]=std::string(40,'b');
  if(bad==18)b["cohort_migration"]=nullptr;
  save(next/"boot.json",b);check(false);
 }
 // Removed / malformed and duplicate JSON fields never become authority.
 save(next/"boot.json",std::string("{\"cohort_migration\":{},\"cohort_migration\":{}}"));check(false);
 reset(1,2);check(false);reset(2,2);check(false); // <2 origins and unused entry.
 reset(2,4);check(false);b=read(next/"boot.json");auto extra=b["cohort_migration"]["shared_key_value"].as<JsonArray>().add<JsonObject>();
 extra["application_id"]="app3";extra["api"]=1;extra["namespace"]=50;save(next/"boot.json",b);check(true);
 reset();b=read(old/"boot.json");b["app_capabilities"][1]["grants"].as<JsonArray>().remove(3);save(old/"boot.json",b);
 b=read(next/"boot.json");b["app_capabilities"][1]["grants"].as<JsonArray>().remove(3);save(next/"boot.json",b);
 bindings=0;runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(old.c_str()));check(false); // Non-universal.
 reset();b=read(next/"boot.json");b["app_capabilities"][2]["grants"][3]["instance_id"]=1;migration(b,"app2",1,1);save(next/"boot.json",b);check(false); // Private owner.
 reset();b=read(next/"boot.json");b["app_capabilities"][2]["grants"][3]["api"]=2;migration(b,"app2",2,50);save(next/"boot.json",b);
 auto manifest=read(next/"app2.json");auto req=manifest["requires"].as<JsonArray>().add<JsonObject>();req["capability"]="storage.key-value";req["api"]=2;save(next/"app2.json",manifest);check(false);
 reset();b=read(next/"boot.json");auto grant=b["app_capabilities"][0]["grants"].as<JsonArray>().add<JsonObject>();grant["capability"]="storage.key-value";grant["api"]=2;grant["instance_id"]=50;save(next/"boot.json",b);
 manifest=read(next/"app0.json");req=manifest["requires"].as<JsonArray>().add<JsonObject>();req["capability"]="storage.key-value";req["api"]=2;save(next/"app0.json",manifest);check(false); // Existing owner API expansion.
 reset();b=read(next/"boot.json");b["app_capabilities"][2]["grants"][1]["instance_id"]=1;save(next/"boot.json",b);check(false); // App-data reassignment.
 reset();b=read(next/"boot.json");b["drivers"][0]["key_value"][0]["namespace"]=50;save(next/"boot.json",b);check(false); // Provider binding change.
 reset();auto board=read(next/"board.json");board["revision"]="changed";save(next/"board.json",board);check(false);
 reset();b=read(next/"cohort.json");b["version"]="1.0.4";save(next/"cohort.json",b);check(false);
 reset();b=read(old/"cohort.json");b["source_revision"]=std::string(40,'f');save(old/"cohort.json",b);check(false);
 // Stale record may validate its identical installed target, never regrant.
 reset();fs::remove_all(old);fs::copy(next,old,fs::copy_options::recursive);bindings=0;runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(old.c_str()));check(true);
 b=read(next/"cohort.json");b["firmware_sha256"]=std::string(64,'e');save(next/"cohort.json",b);check(false);
 save(next/"cohort.json",read(old/"cohort.json"));store(next,4,1);universal(next,4);save(next/"cohort.json",read(old/"cohort.json"));b=read(next/"boot.json");migration(b,"app3");save(next/"boot.json",b);check(false);
 std::cout<<"Explicit cohort shared-KV migration: exact origin/target/API/new identity, universal >=2 owners, consumed entries, no private/appdata/provider expansion, identical-board and self-validation, no writes/binds/ELF execution PASS\n";
}
