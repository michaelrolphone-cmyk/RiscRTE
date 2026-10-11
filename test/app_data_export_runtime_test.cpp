#include "bootstrap/Runtime.h"
#include "runtime/storage/AppDataFiles.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using namespace RiscBoot;
namespace fs=std::filesystem;
static bool owned=true,backendSafe=true,allocationFail=false;
static bool brokerAllocationFail=false;
static unsigned brokerAllocationFailures=0,backendCallsBeforeAllocation=0;
namespace RiscBoot {void* metadataTestAllocate(size_t size){
 if(brokerAllocationFail){assert(size==sizeof(RiscStorage::AppDataExport));++brokerAllocationFailures;return nullptr;}
 return allocationFail?nullptr:std::malloc(size);
}}
static int mode=0,phase=0;
static unsigned finished[2]{},calls=0;
static Runtime* running=nullptr;
static Runtime* volatile retainedRuntime=nullptr;
static risc_app_data_export_v1 saved[2]{};
static fs::path source;
extern "C" int export_test_mode(){return mode;}
extern "C" int export_test_phase(){return phase;}
extern "C" unsigned export_test_backend_calls(){return calls;}
extern "C" void export_test_next(){++phase;}
extern "C" void export_test_owner(int value){owned=value;}
extern "C" void export_test_alloc_fail(int value){allocationFail=value;}
extern "C" void export_test_broker_alloc_fail(int value){
 if(value){brokerAllocationFailures=0;backendCallsBeforeAllocation=calls;}
 else assert(brokerAllocationFailures==1 && calls==backendCallsBeforeAllocation);
 brokerAllocationFail=value;
}
extern "C" void export_test_fault(){backendSafe=false;}
extern "C" void export_test_reset_safe(int expected){assert(running && running->residentResetSafe()==bool(expected));}
extern "C" void export_test_keep(int role,risc_app_data_export_v1 table){saved[role]=table;}
extern "C" void export_test_probe(int role,int live){
 assert(saved[role].stat_revision);uint32_t size=0;uint64_t revision=0;
 const auto status=saved[role].stat_revision(saved[role].volume.terminal.power.volume.base.context,"/saved/state.bin",&size,&revision);
 assert(live?status==RISC_APP_DATA_OK:status==RISC_APP_DATA_CONTEXT);
 risc_app_data_export_entry_v1 entry{};const auto before=calls;
 const auto copied=saved[role].entry(saved[role].volume.terminal.power.volume.base.context,0,&entry);
 assert(live?copied==RISC_APP_DATA_OK:copied==RISC_APP_DATA_CONTEXT);
 assert(calls==before);
}
extern "C" void export_test_finished(int role){assert(mode!=1 && mode!=3 && mode!=4);++finished[role];}
static void save(const fs::path& path,const JsonDocument& doc){std::ofstream file(path);serializeJson(doc,file);assert(file.good());}
static JsonDocument read(const fs::path& path){JsonDocument doc;assert(readJson(path.c_str(),doc));return doc;}
static void textFile(const fs::path& path,const char* text){std::ofstream(path)<<text;}
static JsonDocument manifest(const char* id,const char* elf,bool exports,bool owner){
 JsonDocument doc;doc["type"]="application";doc["id"]=id;doc["version"]="1.0.0";doc["architecture"]="xtensa-esp32s3";doc["file_name"]=elf;doc["entry"]="app_main";
 auto req=doc["requires"].to<JsonArray>();
 if(exports || owner){auto r=req.add<JsonObject>();r["capability"]=owner?RISC_APP_DATA_CAPABILITY:RISC_APP_DATA_EXPORT_CAPABILITY;r["api"]=1;}
 return doc;
}
static void map(JsonObject policy){
 auto m=policy["app_data_export"].to<JsonObject>();m["label"]="Saved files";auto entries=m["files"].to<JsonArray>();
 for(unsigned i=0;i<2;++i){auto e=entries.add<JsonObject>();e["owner"]="owner";e["namespace"]=41;e["name"]=i?"readonly.bin":"state.bin";
  e["path"]=i?"/readonly/state.bin":"/saved/state.bin";e["access"]=i?"read":"read-write";}
}
static void store(const fs::path& root,bool exports=true,bool resident=false){
 fs::create_directories(root);
 textFile(root/"board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 textFile(root/"cohort.json","{}");
 JsonDocument boot;boot["board"]="board.json";boot["default_app"]="default.elf";boot["drivers"].to<JsonArray>();auto policies=boot["app_capabilities"].to<JsonArray>();
 for(unsigned i=0;i<3;++i){const char* ids[]={"browser","child","owner"};const char* names[]={"default","child","owner"};
  const std::string elf=std::string(names[i])+".elf",json=std::string(names[i])+".json";bool exported=exports && (i==0 || (resident && i==1));
  auto app=manifest(ids[i],elf.c_str(),exported,i==2);save(root/json,app);
  auto policy=policies.add<JsonObject>();policy["manifest"]=json;auto grants=policy["grants"].to<JsonArray>();
  if(exported || i==2){auto g=grants.add<JsonObject>();g["capability"]=i==2?RISC_APP_DATA_CAPABILITY:RISC_APP_DATA_EXPORT_CAPABILITY;g["api"]=1;g["instance_id"]=i==2?41:0;}
  if(exported)map(policy);
  if(root!=source)fs::copy_file(source/elf,root/elf,fs::copy_options::overwrite_existing);
 }
 if(resident){auto shell=boot["resident_shell"].to<JsonObject>();shell["api"]=1;shell["host"]="default.elf";shell["foreground"].to<JsonArray>().add("child.elf");}
 save(root/"boot.json",boot);
}
extern "C" void export_test_update(){
 auto app=read(source/"default.json");app["version"]="1.0.1";std::string bytes;serializeJson(app,bytes);Runtime::UpdateApp result{};
 assert(running->appUpdate("browser",bytes.data(),bytes.size(),result));assert(!strcmp(result.elf,"default.elf"));
 app["requires"][0]["capability"]=RISC_APP_DATA_CAPABILITY;bytes.clear();serializeJson(app,bytes);
 assert(!running->appUpdate("browser",bytes.data(),bytes.size(),result));
}
static void policies(const Port& port){
 store(source);const auto original=read(source/"boot.json");
 for(unsigned bad=0;bad<20;++bad){auto boot=original;auto item=boot["app_capabilities"][0];auto e=item["app_data_export"]["files"][0];
  if(bad==0)item.remove("app_data_export");
  if(bad==1)item["app_data_export"]=nullptr;
  if(bad==2)e["owner"]="missing";
  if(bad==3)e["owner"]="browser";
  if(bad==4)e["namespace"]=42;
  if(bad==5)e["namespace"]="41";
  if(bad==6)e["path"]="/saved/../state.bin";
  if(bad==7)e["path"]="relative.bin";
  if(bad==8)e["name"]="../state.bin";
  if(bad==9)e["access"]="write";
  if(bad==10)item["app_data_export"]["files"][1]["path"]="/saved/state.bin";
  if(bad==11)item["app_data_export"]["files"][1]["name"]="state.bin";
  if(bad==12)item["app_data_export"]["files"][1]["path"]="/saved/state.bin/child";
  if(bad==13)item["app_data_export"]["label"]="";
  if(bad==14)item["app_data_export"]["unexpected"]=1;
  if(bad==15)e["unexpected"]=1;
  if(bad==16)item["app_data_export"]["files"].to<JsonArray>();
  if(bad==17){for(unsigned i=2;i<17;++i)item["app_data_export"]["files"].as<JsonArray>().add(e);}
  if(bad==18)item["grants"][0]["instance_id"]=41;
  if(bad==19)item["grants"][0]["api"]=2;
  save(source/"boot.json",boot);Runtime runtime(port);if(runtime.prepare(source.c_str())){std::cerr<<"accepted bad policy "<<bad<<"\n";assert(false);}
 }
 save(source/"boot.json",original);{Runtime runtime(port);assert(runtime.prepare(source.c_str()));}
 // Unused map and missing native backend reject before any backend operation.
 store(source,false);auto boot=read(source/"boot.json");map(boot["app_capabilities"][0].as<JsonObject>());save(source/"boot.json",boot);
 {Runtime runtime(port);assert(!runtime.prepare(source.c_str()));}
 store(source);{auto missing=port;missing.appData=nullptr;Runtime runtime(missing);assert(!runtime.prepare(source.c_str()));}
 assert(!calls);puts("App-data export strict maps, exact owner, no aliases and no implicit namespace authority PASS");
}
static void watchPolicy(const Port& port,const fs::path& fixtures){
 // Reduce only unrelated hardware capabilities for this storage-only host test.
 // Identity, version, executable and namespace grants come from pinned Watch25.
 store(source,false);auto boot=read(source/"boot.json");auto selections=boot["app_capabilities"].to<JsonArray>();
 auto consumer=manifest("export-test-browser","default.elf",true,false);save(source/"default.json",consumer);
 auto target=selections.add<JsonObject>();target["manifest"]="default.json";
 auto grant=target["grants"].to<JsonArray>().add<JsonObject>();grant["capability"]=RISC_APP_DATA_EXPORT_CAPABILITY;grant["api"]=1;grant["instance_id"]=0;
 const auto exports=read(fixtures/"exports.json");target["app_data_export"].set(exports.as<JsonVariantConst>());
 const auto installed=read(fixtures/"watch25-boot.json");unsigned owners=0;
 for(JsonObjectConst selected:installed["app_capabilities"].as<JsonArrayConst>()) {
  for(JsonObjectConst g:selected["grants"].as<JsonArrayConst>())if(eq(g["capability"],RISC_APP_DATA_CAPABILITY)) {
   const char* name=selected["manifest"];auto app=read(fixtures/name);
   auto requirements=app["requires"].to<JsonArray>();auto req=requirements.add<JsonObject>();req["capability"]=RISC_APP_DATA_CAPABILITY;req["api"]=g["api"];
   save(source/name,app);auto policy=selections.add<JsonObject>();policy["manifest"]=name;policy["grants"].to<JsonArray>().add(g);++owners;
  }
 }
 assert(owners==4);save(source/"boot.json",boot);
 {Runtime runtime(port);assert(runtime.prepare(source.c_str()));}
 for(size_t i=0;i<exports["files"].size();++i) {
  auto bad=boot;auto file=bad["app_capabilities"][0]["app_data_export"]["files"][i];file["namespace"]=file["namespace"].as<unsigned>()+100;
  save(source/"boot.json",bad);Runtime runtime(port);assert(!runtime.prepare(source.c_str()));
 }
 save(source/"boot.json",boot);assert(!calls);
 puts("App-data export pinned Watch25 owner IDs/namespaces and exact persisted-file projection admission PASS");
}
static void cohort(const Port& port){
 const auto old=source/"old",next=source/"next";store(old,false);store(next,true);
 Runtime previous(port);assert(previous.prepare(old.c_str()));
 unsigned admitted=0;auto inspect=[](void* c,const char*,bool){++*static_cast<unsigned*>(c);return true;};
 auto check=[&](const Runtime& current,bool expected,const Runtime::AppDataExportDelegation* d=nullptr,size_t count=0){
  Runtime candidate(Port{});const bool actual=current.validateCohort(candidate,next.c_str(),inspect,&admitted,d,count);
  if(actual!=expected)std::cerr<<"cohort unexpected "<<actual<<": "<<candidate.error()<<"\n";
  assert(actual==expected);
 };
 using Entry=RiscStorage::AppDataExport::Entry;
 Entry entries[2]{};for(unsigned i=0;i<2;++i){strcpy(entries[i].owner,"owner");entries[i].nameSpace=41;strcpy(entries[i].name,i?"readonly.bin":"state.bin");strcpy(entries[i].path,i?"/readonly/state.bin":"/saved/state.bin");entries[i].writable=!i;}
 Runtime::AppDataExportDelegation exact{"browser","Saved files",entries,2};
 check(previous,false);assert(!admitted);check(previous,true,&exact,1);assert(admitted==3);
 auto wrong=exact;wrong.label="Wrong";check(previous,false,&wrong,1);wrong=exact;wrong.consumer="child";check(previous,false,&wrong,1);
 Runtime::AppDataExportDelegation duplicate[]={exact,exact};check(previous,false,duplicate,2);check(previous,false,nullptr,1);check(previous,false,&exact,Runtime::MaxAppPolicies+1);
 std::swap(entries[0],entries[1]);check(previous,true,&exact,1);std::swap(entries[0],entries[1]);
 auto changed=entries[0];entries[0].writable=false;check(previous,false,&exact,1);entries[0]=changed;
 store(old,true);Runtime existing(port);assert(existing.prepare(old.c_str()));check(existing,true);check(existing,false,&exact,1);
 const auto original=read(next/"boot.json");
 auto reordered=original;reordered["app_capabilities"][0]["app_data_export"]["files"][0].set(original["app_capabilities"][0]["app_data_export"]["files"][1]);reordered["app_capabilities"][0]["app_data_export"]["files"][1].set(original["app_capabilities"][0]["app_data_export"]["files"][0]);save(next/"boot.json",reordered);check(existing,true);save(next/"boot.json",original);
 for(unsigned bad=0;bad<8;++bad){auto boot=original;auto m=boot["app_capabilities"][0]["app_data_export"];auto e=m["files"][0];
  if(bad==0)e["path"]="/other/state.bin";
  if(bad==1)e["name"]="new.bin";
  if(bad==2)e["access"]="read";
  if(bad==3)m["label"]="Renamed";
  if(bad==4)m["files"].as<JsonArray>().remove(1);
  if(bad==5){auto extra=m["files"].as<JsonArray>().add<JsonObject>();extra.set(e);extra["name"]="extra.bin";extra["path"]="/saved/extra.bin";}
  if(bad==6)boot["app_capabilities"][2]["grants"][0]["instance_id"]=42;
  if(bad==7){auto child=manifest("child","child.elf",true,false);save(next/"child.json",child);auto p=boot["app_capabilities"][1];auto g=p["grants"].as<JsonArray>().add<JsonObject>();g["capability"]=RISC_APP_DATA_EXPORT_CAPABILITY;g["api"]=1;g["instance_id"]=0;map(p.as<JsonObject>());}
  save(next/"boot.json",boot);check(existing,false);check(existing,false,&exact,1);store(next,true);
 }
 // Removing export declarations is not a revocation path through an update.
 store(next,false);check(existing,false);store(next,true);check(existing,true);
 auto installedGrant=[&](const fs::path& root) {
  auto app=read(root/"default.json");auto req=app["requires"].as<JsonArray>().add<JsonObject>();req["capability"]="storage.installed-files";req["api"]=1;save(root/"default.json",app);
  auto boot=read(root/"boot.json");auto g=boot["app_capabilities"][0]["grants"].as<JsonArray>().add<JsonObject>();g["capability"]="storage.installed-files";g["api"]=1;g["instance_id"]=0;save(root/"boot.json",boot);
 };
 store(old,false);installedGrant(old);Runtime previousGrants(port);assert(previousGrants.prepare(old.c_str()));
 store(next,true);check(previousGrants,false,&exact,1);installedGrant(next);check(previousGrants,true,&exact,1);
 store(old,true);installedGrant(old);Runtime existingGrants(port);assert(existingGrants.prepare(old.c_str()));
 check(existingGrants,true);store(next,true);check(existingGrants,false);
 assert(!calls);puts("App-data export cohort preserves owner/grants/maps and consumes exact native-only initial delegation PASS");
}
int main(int argc,char** argv){
 assert(argc==4);source=argv[1];const std::string scenario=argv[2];mode=scenario=="retain"?1:scenario=="resident"?2:scenario=="resident-retain"?3:scenario=="admission-retain"?4:scenario=="unavailable"?5:0;
 RiscStorage::AppDataFiles files({nullptr,[](void*){return 0u;},[](void*){return true;},malloc,free});
 const auto data=source/"appdata";fs::create_directories(data);assert(files.configure(data.c_str()));
 AppDataBackend backend{&files,
  [](void* c,uint32_t ns,const char* name,uint32_t* size,uint64_t* revision){++calls;assert(ns==41 && running && !running->residentResetSafe());if(mode==4){backendSafe=false;return RISC_APP_DATA_RETAINED;}if(mode==5 && calls==1)return RISC_APP_DATA_UNAVAILABLE;return static_cast<RiscStorage::AppDataFiles*>(c)->stat(ns,name,size,revision);},
  [](void* c,uint32_t ns,const char* name,uint64_t revision,void* out,uint32_t capacity,uint32_t* size,uint64_t* fresh){++calls;assert(ns==41 && running && !running->residentResetSafe());return static_cast<RiscStorage::AppDataFiles*>(c)->read(ns,name,revision,out,capacity,size,fresh);},
  [](void* c,uint32_t ns,const char* name,uint64_t revision,const void* bytes,uint32_t size){++calls;assert(ns==41 && running && !running->residentResetSafe());return static_cast<RiscStorage::AppDataFiles*>(c)->replace(ns,name,revision,bytes,size);},
  [](void* c){return backendSafe && static_cast<RiscStorage::AppDataFiles*>(c)->exitSafe();}};
 Port port{[](){return owned;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};port.appData=&backend;
 if(scenario=="policy"){policies(port);return 0;}if(scenario=="watch-policy"){watchPolicy(port,argv[3]);return 0;}if(scenario=="cohort"){cohort(port);return 0;}
 assert(files.replace(41,"state.bin",0,"init",4)==RISC_APP_DATA_OK);assert(files.replace(41,"readonly.bin",0,"read",4)==RISC_APP_DATA_OK);
 store(source,true,mode==2 || mode==3);auto runtime=std::make_unique<Runtime>(port);running=runtime.get();assert(runtime->prepare(source.c_str()));
 const bool clean=runtime->run();assert(clean==(mode!=1 && mode!=3 && mode!=4));assert(runtime->retained()==!clean);
 if(mode!=4)export_test_probe(0,0);
 if(mode==2 || mode==3)export_test_probe(1,0);
 if(clean){assert(finished[0]==((mode==0 || mode==5)?2u:1u) && finished[1]==1);if(mode==0 || mode==5)assert(phase==2);}
 else{assert(!finished[0] && !finished[1]);if(mode==3)assert(!runtime->residentResetSafe());retainedRuntime=runtime.release();}
 std::cout<<"App-data export actual ELF "<<scenario<<": acquisition, revisions, revocation and retention PASS\n";
}
