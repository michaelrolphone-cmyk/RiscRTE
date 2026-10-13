// The same source supplies a tiny host app so authority/update assertions run
// at the real app-entry boundary, rather than by fabricating active Runtime state.
#ifdef OPTIONAL_APP_TEST_MODULE
extern "C" void test_optional_entry();
extern "C" __attribute__((visibility("default"))) void app_main() { test_optional_entry(); }
#else
#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#define private public
#include "bootstrap/Runtime.h"
#undef private
#include <RiscPlatformClockV1.h>

using RiscBoot::Runtime;
namespace {
std::string root;
std::function<void()> entry;
unsigned entries=0, cases=0, backendCalls=0;
bool clockPresent=true;
const risc_installation_v1* installer=nullptr;
constexpr const char* EmptyBoard=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
constexpr const char* VolumeBoard=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":9,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[5],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})";
constexpr const char* VolumeManifest=R"({"type":"driver","id":"catalog-volume","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"sd.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.installation","api":1}],"provides":[{"capability":"storage.volume","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})";
void write(const char* file,const std::string& bytes) {
  std::ofstream out(root+"/"+file,std::ios::binary);out<<bytes;out.close();assert(out);
}
std::string row(const std::string& capability,unsigned api=1) {
  return "{\"capability\":\""+capability+"\",\"api\":"+std::to_string(api)+"}";
}
std::string grant(const std::string& capability,unsigned api=1,unsigned instance=0) {
  auto value=row(capability,api);value.pop_back();
  return value+",\"instance_id\":"+std::to_string(instance)+"}";
}
std::string array(const std::string& rows) { return "["+rows+"]"; }
std::string manifest(const std::string& required="[]",const std::string& optional="",
                     const char* version="1.0.0",const char* id="test") {
  std::string result="{\"type\":\"application\",\"id\":\""+std::string(id)+
    "\",\"version\":\""+version+"\",\"architecture\":\"xtensa-esp32s3\","
    "\"file_name\":\"default.elf\",\"entry\":\"app_main\",\"requires\":"+required;
  if(!optional.empty())result+=",\"optional\":"+optional;
  return result+"}";
}
const RiscBoot::KeyValueBackend kv{nullptr,
  [](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{++backendCalls;return RISC_KEY_VALUE_NOT_FOUND;},
  [](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{++backendCalls;return RISC_KEY_VALUE_OK;},
  RISC_KEY_VALUE_V2_BLOB_MAX};
bool bind(Runtime& runtime) {
  static const risc_platform_clock_api_v1 clock{1,sizeof(clock),nullptr,
    [](void*)->uint64_t{return 0;},[](void*,uint32_t){}};
  return !clockPresent || runtime.registerPlatform("platform.clock",1,Runtime::Scope::Global,0,&clock);
}
RiscBoot::Port port() {
  return {[](){return true;},[](risc_runtime_health_v1*){return true;},
    [](uint32_t){},[](const char*){return true;},bind,&kv};
}
void configure(const std::string& app,const std::string& grants="[]",
               const std::string& drivers="[]",const std::string& installation="") {
  write("board.json",installation.empty()?EmptyBoard:VolumeBoard);
  write("app.json",app);
  std::string boot="{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":"+drivers+
    ",\"app_capabilities\":[{\"manifest\":\"app.json\",\"grants\":"+grants+"}]";
  if(!installation.empty())boot+=",\"installation\":"+installation;
  write("boot.json",boot+"}");
}
void expectPrepare(const char* name,const std::string& app,const std::string& grants,bool expected,
                   const std::string& drivers="[]") {
  configure(app,grants,drivers);auto runtime=std::make_unique<Runtime>(port());
  const bool actual=runtime->prepare(root.c_str());
  if(actual!=expected) {
    fprintf(stderr,"%s: prepare expected %d, got %d (%s)\n",name,int(expected),int(actual),runtime->error());abort();
  }
  if(actual)assert(runtime->policies_[0].count<=Runtime::MaxAppPolicyGrants);
  assert(backendCalls==0);++cases;
}
void acquire(Runtime& runtime,const char* capability,unsigned api,unsigned instance,bool expected) {
  risc_runtime_capability_v1 result{};result.struct_size=sizeof(result);
  assert(runtime.acquire(capability,api,instance,&result)==expected);
  if(expected){assert(result.api && result.slot && result.generation);assert(runtime.release(&result));}
  else assert(!result.api && !result.slot && !result.generation);
}
void run(Runtime& runtime,const std::function<void()>& action) {
  entry=action;const unsigned before=entries;
  if(!runtime.run()){fprintf(stderr,"run failed: %s\n",runtime.error());abort();}
  assert(entries==before+1);entry={};assert(!backendCalls);
}
void schemaAndAuthority() {
  const auto clock=row("platform.clock"),clockGrant=grant("platform.clock");
  const auto missing=row("test.absent");
  expectPrepare("optional omitted",manifest(),"[]",true);
  expectPrepare("optional empty",manifest("[]","[]"),"[]",true);
  expectPrepare("ungranted absent optional",manifest("[]",array(missing)),"[]",true);
  expectPrepare("required absent owner grant",manifest(array(missing)),"[]",false);
  expectPrepare("undeclared owner grant",manifest(),array(clockGrant),false);
  expectPrepare("undeclared owner grant alongside optional",manifest("[]",array(missing)),array(clockGrant),false);
  for(const char* invalid:{"null","{}","1","true","\"optional\"","[null]","[false]","[\"clock\"]","[[]]","[{}]"})
    expectPrepare("malformed optional list/row",manifest("[]",invalid),"[]",false);
  const std::vector<std::string> malformed={
    R"({"capability":"test.absent"})",R"({"api":1})",R"({"capability":null,"api":1})",
    R"({"capability":7,"api":1})",R"({"capability":"","api":1})",
    R"({"capability":"test.absent","api":null})",R"({"capability":"test.absent","api":0})",
    R"({"capability":"test.absent","api":-1})",R"({"capability":"test.absent","api":1.5})",
    R"({"capability":"test.absent","api":true})",R"({"capability":"test.absent","api":"1"})",
    R"({"capability":"test.absent","api":4294967296})",
    R"({"capability":"test.absent","api":1,"instance_id":0})",
    R"({"capability":"test.absent","api":1,"note":"ignored?"})",
    R"({"capability":"test\u0000absent","api":1})",row(std::string(96,'x'))};
  for(const auto& invalid:malformed)
    expectPrepare("malformed optional declaration",manifest("[]",array(invalid)),"[]",false);
  expectPrepare("optional max API integer",manifest("[]",R"([{"capability":"test.absent","api":4294967295}])"),"[]",true);
  expectPrepare("duplicate optional absent grants",manifest("[]",array(missing+","+missing)),"[]",false);
  expectPrepare("duplicate required/optional",manifest(array(clock),array(clock)),array(clockGrant),false);
  expectPrepare("duplicate mandatory",manifest(array(clock+","+clock)),array(clockGrant),false);
  expectPrepare("non-KV different API duplicate",manifest("[]",array(missing+","+row("test.absent",2))),"[]",false);
  expectPrepare("provider-bound optional KV denied",manifest("[]",array(row(RISC_BOUND_KEY_VALUE_CAPABILITY))),"[]",false);
  expectPrepare("provider-bound optional app-data denied",manifest("[]",array(row(RISC_BOUND_APP_DATA_CAPABILITY))),"[]",false);
  expectPrepare("optional explicit grant",manifest("[]",array(clock)),array(clockGrant),true);
  expectPrepare("required explicit grant",manifest(array(clock)),array(clockGrant),true);
  expectPrepare("wrong optional grant API",manifest("[]",array(clock)),array(grant("platform.clock",2)),false);
  expectPrepare("wrong optional grant instance",manifest("[]",array(clock)),array(grant("platform.clock",1,9)),false);
  expectPrepare("duplicate optional owner grant",manifest("[]",array(clock)),array(clockGrant+","+clockGrant),false);
  clockPresent=false;
  expectPrepare("optional no provider and no grant",manifest("[]",array(clock)),"[]",true);
  expectPrepare("optional granted missing provider",manifest("[]",array(clock)),array(clockGrant),false);
  clockPresent=true;
  expectPrepare("optional granted missing ordinary provider",manifest("[]",array(missing)),array(grant("test.absent")),false);

  const auto key1=row(RISC_KEY_VALUE_CAPABILITY),key2=row(RISC_KEY_VALUE_CAPABILITY,2);
  const auto keyGrant1=grant(RISC_KEY_VALUE_CAPABILITY,1,5),keyGrant2=grant(RISC_KEY_VALUE_CAPABILITY,2,8);
  expectPrepare("KV distinct optional APIs without grants",manifest("[]",array(key1+","+key2)),"[]",true);
  expectPrepare("KV repeated same optional API",manifest("[]",array(key1+","+key1)),"[]",false);
  expectPrepare("KV required/optional repeated API",manifest(array(key1),array(key1)),array(keyGrant1),false);
  expectPrepare("KV distinct APIs with owner grants",manifest(array(key1),array(key2)),array(keyGrant1+","+keyGrant2),true);
  expectPrepare("optional KV multiple namespaces",manifest("[]",array(key1)),array(keyGrant1+","+grant(RISC_KEY_VALUE_CAPABILITY,1,8)),true);
  expectPrepare("optional KV namespace zero",manifest("[]",array(key1)),array(grant(RISC_KEY_VALUE_CAPABILITY)),false);

  configure(manifest("[]",array(clock+","+key1+","+missing)));
  {auto runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(root.c_str()));
    run(*runtime,[&](){assert(runtime->policies_[0].count==0);acquire(*runtime,"platform.clock",1,0,false);
      acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,1,5,false);acquire(*runtime,"test.absent",1,0,false);});}
  configure(manifest("[]",array(clock+","+key1+","+key2)),array(clockGrant+","+keyGrant1));
  {auto runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(root.c_str()));
    run(*runtime,[&](){assert(runtime->policies_[0].count==2);acquire(*runtime,"platform.clock",1,0,true);
      acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,1,5,true);acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,1,8,false);
      acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,2,5,false);});}
}
void declarationBounds() {
  std::string rows;
  for(size_t i=0;i<Runtime::MaxAppRequirements;++i){if(i)rows+=",";rows+=row("test.optional"+std::to_string(i));}
  expectPrepare("exact combined bound all optional",manifest("[]",array(rows)),"[]",true);
  expectPrepare("optional overflow",manifest("[]",array(rows+","+row("test.extra"))),"[]",false);
  expectPrepare("combined required/optional overflow",manifest(array(row("platform.clock")),array(rows)),array(grant("platform.clock")),false);
  rows.erase(rows.rfind(",{"));
  expectPrepare("exact combined bound mixed",manifest(array(row("platform.clock")),array(rows)),array(grant("platform.clock")),true);
}
void ownerGrantBounds() {
  std::string grants;
  for(size_t i=1;i<=Runtime::MaxAppPolicyGrants;++i){if(i>1)grants+=",";grants+=grant(RISC_KEY_VALUE_CAPABILITY,1,unsigned(i));}
  const auto key=row(RISC_KEY_VALUE_CAPABILITY);
  expectPrepare("exact optional owner-grant bound",manifest("[]",array(key)),array(grants),true);
  expectPrepare("optional owner-grant overflow",manifest("[]",array(key)),array(grants+","+grant(RISC_KEY_VALUE_CAPABILITY,1,unsigned(Runtime::MaxAppPolicyGrants+1))),false);
  grants.erase(grants.rfind(",{"));grants+=","+grant("platform.clock");
  const auto optional=array(key+","+row("platform.clock"));
  configure(manifest("[]",optional),array(grants));
  auto runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(root.c_str()));
  run(*runtime,[&](){
    assert(runtime->policies_[0].count==Runtime::MaxAppPolicyGrants);
    Runtime::UpdateApp out{};
    auto candidate=manifest("[]",optional,"1.0.1");
    assert(runtime->appUpdate("test",candidate.data(),candidate.size(),out));
    candidate=manifest("[]",array(key),"1.0.1");
    assert(!runtime->appUpdate("test",candidate.data(),candidate.size(),out));
    acquire(*runtime,"platform.clock",1,0,true);
    for(size_t i=1;i<Runtime::MaxAppPolicyGrants;++i)acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,1,unsigned(i),true);
    acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,1,unsigned(Runtime::MaxAppPolicyGrants),false);
    cases+=2;
  });
}
void ordinaryProviderValidation() {
  const auto request=array(row("test.provider")),allowed=array(grant("test.provider"));
  const std::string provider=R"({"type":"driver","id":"test-provider","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","requires":[],"provides":[{"capability":"test.provider","api":1}]})";
  const std::string selected=R"([{"manifest":"provider.json"}])";
  write("provider.json",provider);
  expectPrepare("optional owner-selected provider",manifest("[]",request),allowed,true,selected);
  expectPrepare("optional wrong provider instance",manifest("[]",request),array(grant("test.provider",1,9)),false,selected);
  expectPrepare("optional wrong provider API",manifest("[]",array(row("test.provider",2))),array(grant("test.provider",2)),false,selected);
  expectPrepare("selected provider without owner grant",manifest("[]",request),"[]",true,selected);
  auto malformed=provider;malformed.replace(malformed.find("\"requires\":[]"),13,"\"requires\":null");write("provider.json",malformed);
  expectPrepare("malformed provider still rejects optional",manifest("[]",request),"[]",false,selected);
  std::remove((root+"/provider.json").c_str());
  expectPrepare("missing selected provider still rejects optional",manifest("[]",request),"[]",false,selected);
}
void updates() {
  const auto clock=row("platform.clock"),missing=row("test.absent"),key1=row(RISC_KEY_VALUE_CAPABILITY),key2=row(RISC_KEY_VALUE_CAPABILITY,2);
  for(bool granted:{false,true}) {
    std::string grants=granted?array(grant("platform.clock")+","+grant(RISC_KEY_VALUE_CAPABILITY,1,5)+","+grant(RISC_KEY_VALUE_CAPABILITY,1,8)):"[]";
    configure(manifest("[]",array(clock+","+key1)),grants);
    auto runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(root.c_str()));
    run(*runtime,[&](){
      const auto before=runtime->policies_[0];
      auto check=[&](const char* name,const std::string& candidate,bool expected){
        Runtime::UpdateApp out{};bool actual=runtime->appUpdate("test",candidate.data(),candidate.size(),out);
        if(actual!=expected){fprintf(stderr,"update %s (granted=%d): expected %d, got %d\n",name,int(granted),int(expected),int(actual));abort();}
        if(expected)assert(!strcmp(out.elf,"default.elf")&&!strcmp(out.manifest,"app.json"));
        assert(runtime->policies_[0].count==before.count);
        for(size_t i=0;i<before.count;++i){const auto&a=before.grants[i];const auto&b=runtime->policies_[0].grants[i];
          assert(a.api==b.api&&a.instance==b.instance&&a.driver==b.driver&&a.platform==b.platform&&!strcmp(a.capability,b.capability));}
        ++cases;
      };
      check("preserve optional declarations",manifest("[]",array(clock+","+key1),"1.0.1"),true);
      check("new ungranted optional",manifest("[]",array(clock+","+key1+","+missing),"1.0.1"),true);
      check("new ungranted KV API",manifest("[]",array(clock+","+key1+","+key2),"1.0.1"),true);
      check("required new grant denied",manifest(array(missing),array(clock+","+key1),"1.0.1"),false);
      check("required existing declaration",manifest(array(clock+","+key1),"[]","1.0.1"),granted);
      check("remove owner declarations",manifest("[]","[]","1.0.1"),!granted);
      check("drop one owner declaration",manifest("[]",array(key1),"1.0.1"),!granted);
      check("required KV wrong API",manifest(array(key2),array(clock+","+key1),"1.0.1"),false);
      check("duplicate optional without grant",manifest("[]",array(clock+","+key1+","+missing+","+missing),"1.0.1"),false);
      check("duplicate across lists",manifest(array(clock),array(clock+","+key1),"1.0.1"),false);
      check("null optional",manifest("[]","null","1.0.1"),false);
      check("malformed optional row",manifest("[]",array(clock+","+key1+",null"),"1.0.1"),false);
      check("extra optional field",manifest("[]",array(clock+","+key1+",{\"capability\":\"test.absent\",\"api\":1,\"instance_id\":0}"),"1.0.1"),false);
      check("version not advanced",manifest("[]",array(clock+","+key1)),false);
      std::string rows=clock+","+key1;
      for(size_t i=2;i<Runtime::MaxAppRequirements;++i)rows+=","+row("test.optional"+std::to_string(i));
      check("exact declaration bound",manifest("[]",array(rows),"1.0.1"),true);
      check("combined declaration overflow",manifest("[]",array(rows+","+missing),"1.0.1"),false);
      auto injected=manifest("[]",array(clock+","+key1),"1.0.1");injected.pop_back();injected+=",\"grants\":"+grants+"}";
      check("manifest cannot supply owner grants",injected,false);
      acquire(*runtime,"platform.clock",1,0,granted);acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,1,5,granted);
      acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,1,8,granted);acquire(*runtime,RISC_KEY_VALUE_CAPABILITY,2,5,false);
      acquire(*runtime,"test.absent",1,0,false);
    });
  }
}
void discovery() {
  write("sd.json",VolumeManifest);
  for(bool envelopeGrant:{false,true}) {
    const std::string envelope=envelopeGrant?array(grant("storage.volume",1,9)):"[]";
    configure(manifest(),"[]",R"([{"manifest":"sd.json","instance_id":9}])",
      "{\"volume_instance\":9,\"grants\":"+envelope+"}");
    installer=nullptr;auto runtime=std::make_unique<Runtime>(port());assert(runtime->prepare(root.c_str()));
    run(*runtime,[&](){
      assert(installer);char launch[193]{};
      auto admit=[&](const std::string& app){return installer->admit(installer->context,"Apps/discovered/manifest.json",app.c_str(),launch,sizeof(launch));};
      const auto volume=row("storage.volume");
      const auto initial=manifest("[]",array(volume+","+row("test.absent")),"1.0.0","discovered");
      assert(admit(initial)==RISC_INSTALL_OK);assert(!strcmp(launch,"Apps/discovered/default.elf"));
      assert(runtime->policyCount_==2);const auto& policy=runtime->policies_[1];
      assert(policy.count==(envelopeGrant?1u:0u));
      if(envelopeGrant)assert(!strcmp(policy.grants[0].capability,"storage.volume")&&policy.grants[0].instance==9&&policy.grants[0].api==1);
      // A selected/loaded volume without an envelope grant must not become app authority.
      for(size_t g=0;g<policy.count;++g)assert(strcmp(policy.grants[g].capability,"test.absent"));
      const auto before=runtime->policyCount_;
      for(const std::string& optional:{std::string("null"),array(row("test.absent")+","+row("test.absent")),array("null")}) {
        assert(admit(manifest("[]",optional,"1.0.1","discovered"))!=RISC_INSTALL_OK);assert(runtime->policyCount_==before);
      }
      assert(admit(manifest(array(row("test.absent")),array(volume),"1.0.1","discovered"))==RISC_INSTALL_DENIED);
      assert(admit(manifest(array(volume),"[]","1.0.1","discovered"))==(envelopeGrant?RISC_INSTALL_OK:RISC_INSTALL_DENIED));
      assert(runtime->policies_[1].count==(envelopeGrant?1u:0u));
      // The trusted direct admission boundary must still reject ambiguous JSON,
      // including duplicate keys which permissive DOM parsing would overwrite.
      const std::string directPolicy=R"({"manifest":"Apps/direct/manifest.json","grants":[],"image_source":{"volume_instance":9,"path":"Apps/direct/default.elf"}})";
      const auto directManifest=manifest("[]",array(row("test.absent")),"1.0.0","direct");
      auto duplicateOptional=directManifest;duplicateOptional.pop_back();duplicateOptional+=",\"optional\":[]}";
      auto duplicatePolicy=directPolicy;duplicatePolicy.pop_back();duplicatePolicy+=",\"grants\":[]}";
      const auto directBefore=runtime->policyCount_;
      const std::vector<std::string> ambiguous={duplicateOptional,
        manifest("[]",R"([{"capability":"test.absent","capability":"test.other","api":1}])","1.0.0","direct"),
        manifest("[]",R"([{"capability":"test.absent","api":1,"api":2}])","1.0.0","direct")};
      for(const auto& invalid:ambiguous){
        assert(!runtime->admitInstalledApp(directPolicy.c_str(),invalid.c_str()));
        assert(runtime->policyCount_==directBefore);++cases;
      }
      assert(!runtime->admitInstalledApp(duplicatePolicy.c_str(),directManifest.c_str()));
      assert(runtime->policyCount_==directBefore);
      assert(runtime->admitInstalledApp(directPolicy.c_str(),directManifest.c_str()));
      assert(runtime->policyCount_==directBefore+1 && runtime->policies_[directBefore].count==0);
      cases+=8;
    });
  }
 }
void reservedDiscovery() {
  const RiscBoot::AppDataBackend data{nullptr,
    [](void*,uint32_t,const char*,uint32_t*,uint64_t*)->int32_t{++backendCalls;return RISC_APP_DATA_UNAVAILABLE;},
    [](void*,uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*)->int32_t{++backendCalls;return RISC_APP_DATA_UNAVAILABLE;},
    [](void*,uint32_t,const char*,uint64_t,const void*,uint32_t)->int32_t{++backendCalls;return RISC_APP_DATA_UNAVAILABLE;},
    [](void*){return true;}};
  auto app=manifest("[]",array(row(RISC_KEY_VALUE_CAPABILITY)+","+row("storage.shared-data")+","+
    row("storage.volume")+","+row("app.catalog")+","+row("test.absent")),"1.0.0","reserved");
  app.replace(app.find("default.elf"),strlen("default.elf"),"reserved.elf");
  write("reserved.json",app);write("sd.json",VolumeManifest);
  auto shared=[](unsigned instance,const char* file){auto g=grant("storage.shared-data",1,instance);g.pop_back();return g+",\"file\":\""+file+"\"}";};
  const auto grants=array(grant(RISC_KEY_VALUE_CAPABILITY,1,5)+","+grant(RISC_KEY_VALUE_CAPABILITY,1,8)+","+
    shared(20,"first.json")+","+shared(21,"second.json")+","+grant("storage.volume",1,9));
  for(unsigned volume:{9u,10u}) {
    configure(manifest(),"[]",R"([{"manifest":"sd.json","instance_id":9},{"manifest":"sd.json","instance_id":10}])",
      "{\"volume_instance\":"+std::to_string(volume)+",\"grants\":"+array(grant("app.catalog"))+"}");
    JsonDocument board;assert(RiscBoot::parse(VolumeBoard,strlen(VolumeBoard),board));
    auto second=board["devices"].as<JsonArray>().add<JsonObject>();second.set(board["devices"][0]);
    second["instance_id"]=10;second["config"]["pins"][0]=6;
    std::string encoded;serializeJson(board,encoded);write("board.json",encoded);
    JsonDocument boot,policy;assert(RiscBoot::readJson((root+"/boot.json").c_str(),boot));
    const std::string reserved="{\"manifest\":\"reserved.json\",\"grants\":"+grants+
      ",\"catalog_authority\":true,\"image_source\":{\"volume_instance\":9,\"path\":\"Apps/reserved/reserved.elf\"}}";
    assert(RiscBoot::parse(reserved.data(),reserved.size(),policy));boot["app_capabilities"].as<JsonArray>().add(policy.as<JsonVariantConst>());
    encoded.clear();serializeJson(boot,encoded);write("boot.json",encoded);
    auto callbacks=port();callbacks.appData=&data;auto runtime=std::make_unique<Runtime>(callbacks);
    assert(runtime->prepare(root.c_str()));assert(runtime->policies_[1].catalogAuthority&&!runtime->policies_[1].enabled);
    installer=nullptr;run(*runtime,[&](){
      assert(installer);char launch[193]{};const auto before=runtime->policyCount_;
      assert(installer->admit(installer->context,"Apps/wrong/manifest.json",app.c_str(),launch,sizeof(launch))==RISC_INSTALL_DENIED);
      const auto result=installer->admit(installer->context,"Apps/reserved/manifest.json",app.c_str(),launch,sizeof(launch));
      assert(result==(volume==9?RISC_INSTALL_OK:RISC_INSTALL_DENIED));
      assert(runtime->policyCount_==before+(volume==9?1u:0u));
      if(volume==9){
        const auto& original=runtime->policies_[1];const auto& installed=runtime->policies_[before];
        assert(installed.count==5&&installed.count==original.count);
        for(size_t i=0;i<original.count;++i){const auto& a=original.grants[i];const auto& b=installed.grants[i];
          assert(!strcmp(a.capability,b.capability)&&a.api==b.api&&a.instance==b.instance&&a.driver==b.driver&&!strcmp(a.sharedFile,b.sharedFile));
          assert(strcmp(b.capability,"app.catalog")&&strcmp(b.capability,"test.absent"));
        }
        assert(!strcmp(launch,"Apps/reserved/reserved.elf"));
      }
      cases+=3;
    });
  }
}
}
extern "C" void test_optional_entry() { ++entries;assert(entry);entry(); }
extern "C" void app_catalog_test_installer(const void* api) { installer=static_cast<const risc_installation_v1*>(api); }
int main(int argc,char** argv) {
  assert(argc==2);root=argv[1];
  static_assert(Runtime::MaxAppRequirements==16||Runtime::MaxAppRequirements==24||Runtime::MaxAppRequirements==32,"Run baseline and expanded admission bounds");
  static_assert(sizeof(((Runtime::Invocation*)nullptr)->appGrants_)/sizeof(Runtime::AppGrant)==16,"Live invocation handles unchanged");
  static_assert(RiscLimits::Grants==RiscLimits::Providers+16,"Graph live reserve unchanged");
  printf("MEMORY rows=%zu policy=%zu grant_row=%zu runtime=%zu invocation=%zu graph_grants=%zu live_handles=16\n",Runtime::MaxAppPolicyGrants,sizeof(Runtime::AppPolicy),sizeof(Runtime::AppGrantPolicy),sizeof(Runtime),sizeof(Runtime::Invocation),RiscLimits::Grants);
  schemaAndAuthority();declarationBounds();ownerGrantBounds();ordinaryProviderValidation();updates();discovery();reservedDiscovery();
  printf("Optional app declarations: %u checks, %u real app entries, prepare/update/discovery and unchanged authority PASS (declaration/grant bounds %zu/%zu)\n",cases,entries,Runtime::MaxAppRequirements,Runtime::MaxAppPolicyGrants);
}
#endif
