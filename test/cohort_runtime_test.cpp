#include "bootstrap/Runtime.h"
#include "runtime/update/Cohort.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
namespace fs=std::filesystem;
using namespace RiscBoot;
static unsigned bindings=0,apps=0,drivers=0;static bool safe=true,elfGood=true;
static bool owner(){return true;}static bool health(risc_runtime_health_v1*){return true;}
static bool log(const char*){return true;}static void delay(uint32_t){}
static bool bind(Runtime& r){++bindings;static uint32_t table[]={1,8};static const risc_realtime_control_api_v1 realtime{1,sizeof(realtime),nullptr,
 [](void*,risc_realtime_snapshot_v1*)->int32_t{assert(false);return -1;},
 [](void*,int64_t,uint32_t)->int32_t{assert(false);return -1;}};
 return r.registerRealtime(&realtime) && r.registerPlatform("platform.clock",1,Runtime::Scope::Global,0,table);}
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
  for(const char* cap:{"storage.key-value","storage.app-data","platform.clock",RISC_REALTIME_CONTROL_CAPABILITY}){
   auto req=requirements.add<JsonObject>();req["capability"]=cap;req["api"]=1;
   auto grant=grants.add<JsonObject>();grant["capability"]=cap;grant["api"]=1;grant["instance_id"]=(!strcmp(cap,"platform.clock") || !strcmp(cap,RISC_REALTIME_CONTROL_CAPABILITY))?0:i+1;
  }
  save(path/(id+".json"),manifest);save(path/(id+".elf"),std::string("app"));
 }
 for(unsigned i=0;i<driverCount;++i){
  std::string id="driver"+std::to_string(i);JsonDocument manifest;
  manifest["type"]="driver";manifest["id"]=id;manifest["version"]="1.0.0";manifest["driver_abi"]=2;manifest["architecture"]="xtensa-esp32s3";manifest["file_name"]="driver.elf";
  auto req=manifest["requires"].to<JsonArray>().add<JsonObject>();req["capability"]="storage.key-value.bound";req["api"]=1;
  auto realtime=manifest["requires"].as<JsonArray>().add<JsonObject>();realtime["capability"]=RISC_PLATFORM_REALTIME_CAPABILITY;realtime["api"]=1;
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
int main(int argc,char** argv){
 assert(argc==2);fs::path root=argv[1],old=root/"old",next=root/"next";store(old,19,17);store(next,20,18);
 auto runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(old.c_str()) && bindings==1);
 auto check=[&](bool expected){apps=drivers=0;auto candidate=std::make_unique<Runtime>(Port{});
  const bool accepted=runtime->validateCohort(*candidate,next.c_str(),admit,nullptr);
  if(accepted!=expected)std::cerr<<"candidate error: "<<candidate->error()<<"\n";
  assert(accepted==expected && bindings==1);
 };
 check(true);assert(apps==20 && drivers==18);
 store(next,24,24);check(true);assert(apps==24 && drivers==24);
 store(next,25,24);check(false);store(next,24,25);check(false);
 store(next,20,18);auto original=read(next/"boot.json");
 for(unsigned bad=0;bad<8;++bad){auto boot=original;
  if(bad==0)boot["app_capabilities"][0]["grants"][1]["instance_id"]=50;
  if(bad==1)boot["app_capabilities"][19]["grants"][0]["instance_id"]=1;
  if(bad==2)boot["drivers"][17]["key_value"][0]["namespace"]=100;
  if(bad==3)boot["drivers"][0]["key_value"][0]["namespace"]=500;
  if(bad==4)boot["drivers"][0]["key_value"][0]["access"]="read";
  if(bad==5)boot["default_app"]="unlisted.elf";
  if(bad==6)boot["app_capabilities"].as<JsonArray>().remove(0);
  if(bad==7)boot["port"].to<JsonObject>()["unrecognized"]=1;
  save(next/"boot.json",boot);check(false);
 }
 save(next/"boot.json",original);check(true);
 auto board=read(next/"board.json");board["revision"]="rev2";save(next/"board.json",board);check(false);board["revision"]="rev1";save(next/"board.json",board);
 save(next/"unselected.elf",std::string("app"));check(false);fs::remove(next/"unselected.elf");
 fs::remove(next/"app19.elf");check(false);save(next/"app19.elf",std::string("app"));
 fs::create_symlink(next/"app0.elf",next/"unexpected");check(false);fs::remove(next/"unexpected");
 auto manifest=read(next/"driver17/manifest.json");manifest["requires"][0]["capability"]="missing.provider";save(next/"driver17/manifest.json",manifest);check(false);
 store(next,20,18);manifest=read(next/"app19.json");manifest["requires"][2]["capability"]="platform.bank-store";save(next/"app19.json",manifest);check(false);
 store(next,20,18);elfGood=false;check(false);elfGood=true;safe=false;check(false);safe=true;check(true);
 // Metadata uses exact source/digest/layout, rejects unknown/duplicate fields.
 JsonDocument identity;identity["schema"]="riscrte.cohort";identity["schema_version"]=1;identity["product"]="twatch-s3";identity["version"]="1.0.2";
 identity["runtime_version"]="0.1.33";identity["source_repo"]="owner/repository";identity["source_revision"]=std::string(40,'a');identity["layout"]=RiscUpdate::Layout;
 identity["store_abi"]=RiscUpdate::StoreAbi;identity["firmware_size"]=123456;identity["firmware_sha256"]=std::string(64,'b');
 save(next/"cohort.json",identity);RiscUpdate::CohortIdentity parsed{};assert(RiscUpdate::readCohort(next.c_str(),parsed));
 for(const char* key:{"source_revision","firmware_sha256","runtime_version","layout","source_repo"}){auto bad=identity;bad[key]="invalid";save(next/"cohort.json",bad);assert(!RiscUpdate::readCohort(next.c_str(),parsed));}
 auto bad=identity;bad["extra"]=1;save(next/"cohort.json",bad);assert(!RiscUpdate::readCohort(next.c_str(),parsed));
 save(next/"cohort.json",std::string("{\"schema\":\"riscrte.cohort\",\"schema\":\"riscrte.cohort\"}"));assert(!RiscUpdate::readCohort(next.c_str(),parsed));
 std::cout<<"Cohort 20/18 and24/24 complete graph, namespace ownership, hardware, metadata, exact inventory and no native bind/provider execution PASS\n";
}
