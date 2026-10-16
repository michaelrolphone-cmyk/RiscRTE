// Reuse the inert cohort fixtures; validation must never invoke storage/code.
#define main cohortFixtureUnusedMain
#include "cohort_runtime_test.cpp"
#undef main
#include "runtime/update/NativeAppDataExportPolicyV2.h"
static void coordinate(const fs::path& root,bool target) {
 JsonDocument d;d["schema"]="riscrte.cohort";d["schema_version"]=1;
 d["product"]="fixture";d["version"]=target?"1.0.2":"1.0.1";
 d["source_repo"]="fixture/product";d["source_revision"]=std::string(40,target?'b':'a');
 d["runtime_version"]="0.2.13";d["layout"]=RiscUpdate::Layout;d["store_abi"]=RiscUpdate::StoreAbi;
 d["firmware_size"]=8192;d["firmware_sha256"]=std::string(64,'c');save(root/"cohort.json",d);
}
static void exportStore(const fs::path& root,unsigned files,bool kvAccess,bool target) {
 store(root,2,0);coordinate(root,target);
 auto b=read(root/"boot.json"),m=read(root/"app0.json");
 b["app_capabilities"][0]["grants"].as<JsonArray>().remove(0);
 m["requires"].as<JsonArray>().remove(0);
 auto grants=b["app_capabilities"][0]["grants"].as<JsonArray>();auto reqs=m["requires"].as<JsonArray>();
 auto grant=[&](const char* cap,unsigned instance) {auto g=grants.add<JsonObject>();g["capability"]=cap;g["api"]=1;g["instance_id"]=instance;
 auto q=reqs.add<JsonObject>();q["capability"]=cap;q["api"]=1;};
 if(kvAccess)grant("storage.key-value",2);
 if(files) {
  grant(RISC_APP_DATA_EXPORT_CAPABILITY,0);
  auto map=b["app_capabilities"][0]["app_data_export"].to<JsonObject>();map["label"]="User files";
  auto entries=map["files"].to<JsonArray>();
  for(unsigned i=0;i<files;++i){auto e=entries.add<JsonObject>();e["owner"]="app1";e["namespace"]=2;
   e["name"]=i?"second.bin":"first.bin";e["path"]=i?"/second.bin":"/first.bin";e["access"]="read-write";}
 }
 save(root/"boot.json",b);save(root/"app0.json",m);
}
int main(int argc,char** argv) {
 assert(argc==2);fs::path base=argv[1],old=base/"migration-old",next=base/"migration-next";
 for(unsigned initial:{0u,1u}) {
  exportStore(old,initial,false,false);exportStore(next,2,true,true);
  const auto oldBoot=read(old/"boot.json"),nextBoot=read(next/"boot.json");
  std::string before,after;serializeJson(oldBoot["app_capabilities"],before);serializeJson(nextBoot["app_capabilities"],after);
  const char* owners[]={"app1"};RiscUpdate::NativeSharedKeyValueV2 kvTransition{"app0",1,2,owners,1};
  RiscUpdate::NativeAppDataExportPolicyV2 policy{2,"fixture-export-v2",
   {"fixture","1.0.1","fixture/product","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","0.2.13"},
   {"fixture","1.0.2","fixture/product","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","0.2.13"},
   before.c_str(),after.c_str(),&kvTransition,1};
  RiscTest::FileSources sources({old.string(),next.string()});auto current=std::make_unique<Runtime>(port());assert(sources.bind(*current));assert(current->prepare(old.c_str()));
  auto check=[&](bool expected,const RiscUpdate::NativeAppDataExportPolicyV2& selected) {
   auto candidate=std::make_unique<Runtime>(Port{});apps=drivers=0;
   bool result=current->validateNativeCohort(*candidate,next.c_str(),admit,nullptr,selected);
   if(result!=expected)std::cerr<<"migration initial="<<initial<<" error="<<candidate->error()<<"\n";
   assert(result==expected);if(!expected)assert(!apps&&!drivers);
  };
  check(true,policy);check(true,policy); // Interrupted pre-selection admission is restartable.
  check(false,{}); // No implicit authority.
  for(unsigned bad=0;bad<10;++bad) {
   auto changed=policy;auto transition=kvTransition;const char* wrongOwners[]={"app0"};
   if(bad==0)changed.schemaVersion=3;
   if(bad==1)changed.from.sourceRevision="cccccccccccccccccccccccccccccccccccccccc";
   if(bad==2)changed.to.version="1.0.3";
   if(bad==3)changed.before="[]";
   if(bad==4)changed.after="[]";
   if(bad==5)changed.sharedKeyValueCount=0;
   if(bad==6){transition.previousOwners=wrongOwners;changed.sharedKeyValue=&transition;}
   if(bad==7){transition.nameSpace=3;changed.sharedKeyValue=&transition;}
   if(bad==8){transition.consumer="app1";changed.sharedKeyValue=&transition;}
   if(bad==9)changed.migrationId="";
   check(false,changed);
  }
  auto tampered=nextBoot;tampered["app_capabilities"][0]["app_data_export"]["files"][0]["path"]="/unselected.bin";
  save(next/"boot.json",tampered);check(false,policy);save(next/"boot.json",nextBoot);
  // Confirmed target can validate itself and later ordinary updates; the
  // installed native migration record grants no second expansion.
  current=std::make_unique<Runtime>(port());assert(sources.bind(*current));assert(current->prepare(next.c_str()));
  check(true,policy);auto future=read(next/"cohort.json");future["version"]="1.0.3";save(next/"cohort.json",future);check(true,policy);
  // Rollback uses the old bank intact, without applying a reverse map rewrite.
  auto rolledBack=std::make_unique<Runtime>(port());assert(sources.bind(*rolledBack));assert(rolledBack->prepare(old.c_str()));
  assert(read(old/"boot.json")==oldBoot);
 }
 std::cout<<"Native export V2 first/expanded map, explicit KV ownership, exact binding, retry, unchanged and old-bank rollback PASS\n";
}
