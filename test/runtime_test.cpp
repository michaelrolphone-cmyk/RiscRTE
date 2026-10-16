#include "bootstrap/Runtime.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <new>
#include "RuntimeFileFixture.h"
#include "FixtureJson.h"
using namespace RiscBoot;
static unsigned generation=0, beats=0;
static bool heartbeatMode=false;
static std::vector<std::string> lines;
static bool ownerOk=true;
static std::vector<uint32_t> waits;
static unsigned polls=0;
static std::vector<bool> controlBegins;
static unsigned controlEnds=0;
static bool controlPending=false,controlRetain=false;
static void controlBegin(bool child){controlBegins.push_back(child);controlPending=child;}
static bool controlEnd(){++controlEnds;bool pending=controlPending;controlPending=false;return pending;}
static bool controlRequest(){return controlPending;}
static bool controlSafe(){return !(controlRetain&&controlPending);}
extern "C" void test_yield_owner(bool enabled){ownerOk=enabled;}
extern "C" void test_yield_poll(uint32_t ms){assert(ms>0 && ms<=8);++polls;}
static bool owner(){return ownerOk;}
static bool health(risc_runtime_health_v1* h){if(heartbeatMode){ if(beats==3)return false; h->uptime_ms=(++generation)*2000; h->free_heap=123456; h->app_address=0x10000; snprintf(h->target,sizeof(h->target),"host-test"); return true;} h->uptime_ms=++generation;return true;}
static void delay(uint32_t ms){waits.push_back(ms);}
static bool logLine(const char* s){
  // Stage diagnostics are independently asserted by startup_failure_test;
  // they must not impersonate app heartbeat events in this lifecycle model.
  if(!strncmp(s,"RTE_PROVIDER ",13) || !strncmp(s,"RTE_APP phase=",14))return true;
  lines.emplace_back(s);if(heartbeatMode)++beats;return true;
}
static void write(const std::string& p,const std::string& s){std::ofstream(p)<<s;}
static const char* board=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[5],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})";
static const char* manifest=R"({"type":"driver","id":"probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"probe.elf","requires":[{"capability":"hardware.device","api":1}],"provides":[{"capability":"test.probe","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})";
static void driverCapacity(const std::string& root) {
  JsonDocument config;config["board"]="board.json";config["default_app"]="default.elf";
  auto drivers=config["drivers"].to<JsonArray>();
  auto save=[&](const std::string& name,const JsonDocument& value){std::string bytes;serializeJson(value,bytes);write(root+"/"+name,bytes);};
  for(unsigned i=0;i<RuntimeProviders::GraphV2::kMaxModules;++i){
    JsonDocument provider;provider["type"]="driver";provider["id"]="selected-"+std::to_string(i);provider["version"]="1.0.0";
    provider["driver_abi"]=2;provider["architecture"]="xtensa-esp32s3";provider["file_name"]="probe.elf";
    provider["requires"].to<JsonArray>();auto cap=provider["provides"].to<JsonArray>().add<JsonObject>();cap["capability"]="test.selected-"+std::to_string(i);cap["api"]=1;
    auto name="selected-"+std::to_string(i)+".json";save(name,provider);drivers.add<JsonObject>()["manifest"]=name;
  }
  save("boot.json",config);
  {RiscTest::FileSources runtimeFiles0({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles0.bind(runtime));assert(runtime.prepare(root.c_str()));}
  drivers.add<JsonObject>()["manifest"]="selected-0.json";save("boot.json",config);
  {RiscTest::FileSources runtimeFiles1({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles1.bind(runtime));assert(!runtime.prepare(root.c_str()));assert(!strcmp(runtime.error(),"invalid driver list"));}
  drivers.remove(RuntimeProviders::GraphV2::kMaxModules);drivers[RuntimeProviders::GraphV2::kMaxModules-1]["manifest"]="selected-0.json";save("boot.json",config);
  {RiscTest::FileSources runtimeFiles2({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles2.bind(runtime));assert(!runtime.prepare(root.c_str()));assert(!strcmp(runtime.error(),"duplicate package singleton/hardware owner"));}
  puts("Runtime providers: full selected capacity accepted, overflow and duplicate final slot rejected before activation PASS");
}
static void appPolicyCapacity(const std::string& root,const char* grantBoot) {
  JsonDocument original, app;
  assert(parse(grantBoot,strlen(grantBoot),original));
  assert(RiscTest::readFixtureJson((root+"/app.json").c_str(),app));
  auto save=[&](const char* name,const JsonDocument& doc) {
    std::string encoded;serializeJson(doc,encoded);write(root+"/"+name,encoded);
  };
  // Put the executed app at the last policy slot, not in the original eight.
  for(unsigned i=0;i<Runtime::MaxAppPolicies;++i) {
    JsonDocument extra;extra.set(app);
    const std::string name="policy-"+std::to_string(i);
    extra["id"]=name;extra["file_name"]=name+".elf";
    save((name+".json").c_str(),extra);
  }
  JsonDocument config;
  auto policies=[&](unsigned count) {
    config.set(original);auto list=config["app_capabilities"].as<JsonArray>();list.clear();
    for(unsigned i=0;i+1<count;++i) {
      auto item=list.add<JsonObject>();item.set(original["app_capabilities"][0]);
      item["manifest"]="policy-"+std::to_string(i)+".json";
    }
    list.add(original["app_capabilities"][0]);
    save("boot.json",config);
  };
  auto rejected=[&](const char* reason) {
    save("boot.json",config);const size_t before=lines.size();
    RiscTest::FileSources runtimeFiles3({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles3.bind(runtime));
    assert(!runtime.prepare(root.c_str()));assert(!strcmp(runtime.error(),reason));
    assert(!runtime.run());assert(lines.size()==before);
  };
  for(unsigned count:{9u,16u,18u,unsigned(Runtime::MaxAppPolicies)}) {
    policies(count);generation=0;lines.clear();
    RiscTest::FileSources runtimeFiles4({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles4.bind(runtime));
    assert(runtime.prepare(root.c_str()));
    // Parser-owned JSON and source text are gone before app grant lookup.
    // Churn equivalent allocations, then prove the authoritative names survive
    // default/child/default reload and rejected prepare/registration attempts.
    std::vector<std::string> churn(256,std::string(4096,'Z'));
    assert(!runtime.prepare(root.c_str()));
    assert(!runtime.registerPlatform("platform.clock",1,Runtime::Scope::Global,0,&runtime));
    assert(runtime.run());
    assert((lines==std::vector<std::string>{"CAP granted","CAP child denied","CAP granted"}));
  }
  policies(Runtime::MaxAppPolicies+1);rejected("invalid app capability policy");
  config["app_capabilities"].to<JsonObject>();rejected("invalid app capability policy");
  policies(Runtime::MaxAppPolicies);config["app_capabilities"][Runtime::MaxAppPolicies-1]["manifest"]="../app.json";
  rejected("invalid app policy manifest path");
  policies(Runtime::MaxAppPolicies);config["app_capabilities"][Runtime::MaxAppPolicies-1]="invalid";
  rejected("invalid app policy manifest path");
  // Duplicate identity and duplicate ELF path still reject at the new last slot.
  for(const char* field:{"id","file_name"}) {
    JsonDocument duplicate;duplicate.set(app);
    duplicate[field]=!strcmp(field,"id")?"policy-0":"policy-0.elf";
    save("app.json",duplicate);policies(Runtime::MaxAppPolicies);
    rejected("duplicate app identity/path policy");
  }
  save("app.json",app);
  for(const char* field:{"api","instance_id"}) {
    policies(Runtime::MaxAppPolicies);config["app_capabilities"][Runtime::MaxAppPolicies-1]["grants"][0][field]=!strcmp(field,"api")?2:8;
    rejected(!strcmp(field,"api")?"app requirement not uniquely authorized":"app provider unavailable");
  }
  policies(Runtime::MaxAppPolicies);config["app_capabilities"][Runtime::MaxAppPolicies-1]["grants"].as<JsonArray>().clear();
  rejected("app requirement not uniquely authorized");
  policies(Runtime::MaxAppPolicies);auto grants=config["app_capabilities"][Runtime::MaxAppPolicies-1]["grants"].as<JsonArray>();
  grants.add(grants[0]);rejected("app requirement not uniquely authorized");
  policies(Runtime::MaxAppPolicies);grants=config["app_capabilities"][Runtime::MaxAppPolicies-1]["grants"].as<JsonArray>();
  grants.add(grants[0]);grants[1]["capability"]="test.extra";
  rejected("undeclared app grant");
  JsonDocument duplicate;duplicate.set(app);
  auto requirements=duplicate["requires"].as<JsonArray>();requirements.add(requirements[0]);
  save("app.json",duplicate);policies(Runtime::MaxAppPolicies);rejected("duplicate app requirement");
  // Declaration and grant namespace capacities remain independently bounded.
  while(requirements.size()<Runtime::MaxAppRequirements+1)requirements.add(requirements[0]);
  save("app.json",duplicate);rejected("invalid app identity/declarations");
  save("app.json",app);policies(Runtime::MaxAppPolicies);
  grants=config["app_capabilities"][Runtime::MaxAppPolicies-1]["grants"].as<JsonArray>();
  while(grants.size()<Runtime::MaxAppPolicyGrants+1)grants.add(grants[0]);
  rejected("invalid app identity/declarations");
  puts("App policy capacity: 9/16/18 and full bound accepted, overflow rejected; last-slot identity/path, exact grants and independent declaration/namespace bounds PASS");
}
static void optionalVolumeAuthority(const std::string& root) {
  auto save=[&](const char* name,const JsonDocument& value){std::string bytes;serializeJson(value,bytes);write(root+"/"+name,bytes);};
  JsonDocument hw,config,app;
  assert(parse(board,strlen(board),hw));
  auto second=hw["devices"].as<JsonArray>().add<JsonObject>();second.set(hw["devices"][0]);
  second["instance_id"]=8;second["config"]["pins"][0]=6;save("board.json",hw);
  config["board"]="board.json";config["default_app"]="optional-volume.elf";
  auto drivers=config["drivers"].to<JsonArray>();
  for(unsigned instance:{7u,8u}) {
    JsonDocument provider;assert(parse(manifest,strlen(manifest),provider));
    const std::string name="volume-"+std::to_string(instance);
    provider["id"]=name;provider["file_name"]=name+".elf";
    provider["provides"][0]["capability"]="storage.volume";
    save((name+".json").c_str(),provider);
    auto selected=drivers.add<JsonObject>();selected["manifest"]=name+".json";selected["instance_id"]=instance;
  }
  app["type"]="application";app["id"]="optional-volume";app["version"]="1.0.0";app["entry"]="app_main";
  app["architecture"]="xtensa-esp32s3";app["file_name"]="optional-volume.elf";
  app["requires"].to<JsonArray>();
  auto optional=app["optional"].to<JsonArray>().add<JsonObject>();optional["capability"]="storage.volume";optional["api"]=1;
  save("optional-volume.json",app);
  auto policy=config["app_capabilities"].to<JsonArray>().add<JsonObject>();policy["manifest"]="optional-volume.json";
  auto grants=policy["grants"].to<JsonArray>();
  auto check=[&](const char* expected,const char* error=nullptr){
    save("boot.json",config);lines.clear();RiscTest::FileSources files({root});Runtime runtime({owner,health,delay,logLine});
    assert(files.bind(runtime));const bool prepared=runtime.prepare(root.c_str());
    if(error){assert(!prepared);assert(!strcmp(runtime.error(),error));assert(!runtime.run());assert(lines.empty());}
    else {if(!prepared)fprintf(stderr,"optional volume rejected: %s\n",runtime.error());assert(prepared);assert(runtime.run());assert((lines==std::vector<std::string>{expected}));}
  };
  check("VOLUME absent"); // Providers alone never authorize optional acquisition.
  auto grant=grants.add<JsonObject>();grant["capability"]="storage.volume";grant["api"]=1;
  for(unsigned instance:{7u,8u}) {grant["instance_id"]=instance;check(instance==7?"VOLUME 7":"VOLUME 8");}
  grant["instance_id"]=0;check(nullptr,"ambiguous app provider");
  grant["instance_id"]=7;grants.add(grant);grants[1]["instance_id"]=8;
  check(nullptr,"app requirement not uniquely authorized");grants.clear();drivers.clear();
  check("VOLUME absent"); // Internal-only product need not supply an SD provider.
  write(root+"/board.json",board);
  puts("Optional storage.volume: absent provider/authority, unique explicit grant among two providers, exact-instance isolation and ambiguous policy rejection PASS");
}
static void softwareBindings(const std::string& root) {
  JsonDocument config,hw,sw,consumer;
  assert(parse(manifest,strlen(manifest),hw));sw.set(hw);
  sw["id"]="singleton";sw["requires"].to<JsonArray>();sw.remove("hardware_compatibility");
  consumer.set(sw);consumer["id"]="consumer";
  consumer["provides"][0]["capability"]="test.consumer";
  auto requirement=consumer["requires"].as<JsonArray>().add<JsonObject>();
  requirement["capability"]="test.probe";requirement["api"]=1;
  auto save=[&](const char* name,const JsonDocument& doc){std::string bytes;serializeJson(doc,bytes);write(root+"/"+name,bytes);};
  save("bound-hw.json",hw);save("bound-sw.json",sw);save("consumer.json",consumer);
  config["board"]="board.json";config["default_app"]="default.elf";
  auto drivers=config["drivers"].to<JsonArray>();
  auto h=drivers.add<JsonObject>();h["manifest"]="bound-hw.json";h["instance_id"]=7;
  drivers.add<JsonObject>()["manifest"]="bound-sw.json";
  auto c=drivers.add<JsonObject>();c["manifest"]="consumer.json";
  auto check=[&](const char* error){
    save("boot.json",config);const size_t before=lines.size();
    RiscTest::FileSources files({root});Runtime runtime({owner,health,delay,logLine});assert(files.bind(runtime));bool ok=runtime.prepare(root.c_str());
    if(error){if(ok || strcmp(runtime.error(),error))fprintf(stderr,"binding expected %s got %s\n",error,runtime.error());assert(!ok);assert(!strcmp(runtime.error(),error));}
    else {if(!ok)fprintf(stderr,"binding rejected %s\n",runtime.error());assert(ok);}
    assert(lines.size()==before);
  };
  check("ambiguous dependency");
  auto bindings=c["bindings"].to<JsonArray>();auto b=bindings.add<JsonObject>();
  b["capability"]="test.probe";b["api"]=1;b["instance_id"]=7;check(nullptr);
  b["instance_id"]=0;check(nullptr); // singleton zero is a selection, not wildcard
  b["instance_id"]=8;check("missing dependency");
  b["instance_id"]=-1;check("invalid software dependency binding");
  b["instance_id"]=7;b["api"]=2;check("undeclared/duplicate software dependency binding");
  b["api"]=1;b["capability"]="test.unknown";check("undeclared/duplicate software dependency binding");
  b["capability"]="platform.clock";check("invalid software dependency binding");
  b["capability"]="storage.app-data.bound";check("invalid software dependency binding");
  b["capability"]="test.probe";b["extra"]=true;check("invalid software dependency binding");b.remove("extra");
  // Two requirements allow the duplicate to reach semantic validation.
  auto second=consumer["requires"].as<JsonArray>().add<JsonObject>();second["capability"]="test.consumer";second["api"]=1;save("consumer.json",consumer);
  bindings.add(b);check("undeclared/duplicate software dependency binding");bindings.remove(1);
  b["instance_id"]=7;check("driver dependency cycle");
  consumer["requires"].as<JsonArray>().remove(1);save("consumer.json",consumer);
  h["bindings"].to<JsonArray>();check("invalid software dependency bindings");h.remove("bindings");
  c["bindings"].to<JsonObject>();check("invalid software dependency bindings");
  c.remove("bindings");drivers.remove(1);check(nullptr); // legacy unique match unchanged
  puts("Software dependency bindings: dual provider, exact zero/nonzero, absent target, malformed/duplicate/private/hardware and cycle admission PASS");
}

int main(int argc,char** argv){
  assert(argc==2);std::string root=argv[1];
  write(root+"/board.json",board);write(root+"/probe.json",manifest);
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"probe.json","instance_id":7}]})");
  {
    RiscTest::FileSources runtimeFiles5({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles5.bind(runtime));
    assert(!risc_runtime_get_api(1));
    assert(runtime.prepare(root.c_str()));
    assert(runtime.run());assert(!risc_runtime_get_api(1));
    assert((lines==std::vector<std::string>{"TEST default","TEST child","TEST default","RTE_APP child=failed action=reload-default","TEST default"}));
    assert(generation==3);
  }
  // A live hardware driver may have the same basename as default.elf.
  // Ownership is the returned mapping handle, never a global basename alias.
  std::filesystem::create_directory(root+"/drivers");
  std::filesystem::copy_file(root+"/probe.elf",root+"/drivers/default.elf");
  JsonDocument named;assert(parse(manifest,strlen(manifest),named));
  named["file_name"]="default.elf";std::string namedJson;serializeJson(named,namedJson);
  write(root+"/drivers/probe.json",namedJson);
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"drivers/probe.json","instance_id":7}]})");
  lines.clear();generation=0;
  {RiscTest::FileSources runtimeFiles6({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles6.bind(runtime));assert(runtime.prepare(root.c_str()));assert(runtime.run());}
  assert(generation==3 && lines.size()==5);
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"probe.json","instance_id":7}]})");
  // One package artifact serves independent physical instances. Preserve both
  // records; duplicate physical owners and inconsistent package versions fail.
  {
    JsonDocument dual;assert(parse(board,strlen(board),dual));
    auto extra=dual["devices"].as<JsonArray>().add<JsonObject>();
    extra.set(dual["devices"][0]);extra["instance_id"]=8;extra["config"]["pins"][0]=6;
    std::string encoded;serializeJson(dual,encoded);write(root+"/board.json",encoded);
    const char* two=R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"probe.json","instance_id":7},{"manifest":"probe.json","instance_id":8}]})";
    write(root+"/boot.json",two);
    {RiscTest::FileSources runtimeFiles7({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles7.bind(runtime));assert(runtime.prepare(root.c_str()));}
    JsonDocument config;assert(parse(two,strlen(two),config));
    config["drivers"][1]["instance_id"]=7;encoded.clear();serializeJson(config,encoded);write(root+"/boot.json",encoded);
    {RiscTest::FileSources runtimeFiles8({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles8.bind(runtime));assert(!runtime.prepare(root.c_str()));}
    config["drivers"][1]["instance_id"]=8;config["drivers"][1]["manifest"]="probe-other.json";
    encoded.clear();serializeJson(config,encoded);write(root+"/boot.json",encoded);
    JsonDocument other;assert(parse(manifest,strlen(manifest),other));other["version"]="2.0.0";
    encoded.clear();serializeJson(other,encoded);write(root+"/probe-other.json",encoded);
    {RiscTest::FileSources runtimeFiles9({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles9.bind(runtime));assert(!runtime.prepare(root.c_str()));}
    other["version"]="1.0.0";other["file_name"]="different.elf";
    encoded.clear();serializeJson(other,encoded);write(root+"/probe-other.json",encoded);
    {RiscTest::FileSources runtimeFiles10({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles10.bind(runtime));assert(!runtime.prepare(root.c_str()));}
    write(root+"/board.json",board);
    write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"probe.json","instance_id":7}]})");
  }
  // Invalid board always rejects before any driver entry/load or default app.
  JsonDocument doc;assert(parse(board,strlen(board),doc));
  doc["devices"][0]["config"]["pins"][0]=49;std::string bad;serializeJson(doc,bad);write(root+"/board.json",bad);
  {RiscTest::FileSources runtimeFiles11({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles11.bind(runtime));assert(!runtime.prepare(root.c_str()));assert(!runtime.run());}
  write(root+"/board.json",board);
  assert(parse(manifest,strlen(manifest),doc));doc["hardware_compatibility"][0]["compatible"]="wrong,chip";bad.clear();serializeJson(doc,bad);write(root+"/probe.json",bad);
  {RiscTest::FileSources runtimeFiles12({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles12.bind(runtime));assert(!runtime.prepare(root.c_str()));}
  write(root+"/probe.json",manifest);
  write(root+"/boot.json",R"({"board":"board.json","default_app":"../default.elf","drivers":[]})");
  {RiscTest::FileSources runtimeFiles13({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles13.bind(runtime));assert(!runtime.prepare(root.c_str()));}
  // Missing default fails once; never searches/reboots/retries recursively.
  write(root+"/boot.json",R"({"board":"board.json","default_app":"absent.elf","drivers":[]})");
  {RiscTest::FileSources runtimeFiles14({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles14.bind(runtime));assert(runtime.prepare(root.c_str()));assert(!runtime.run());}
  assert(!parse("{\"id\":1,\"id\":2}",15,doc));
  assert(parse(board,strlen(board),doc));
  doc["devices"].as<JsonArray>().add(doc["devices"][0]);
  {Board b;assert(!b.load(doc.as<JsonObjectConst>()));}
  assert(parse(board,strlen(board),doc));
  {Board b;assert(b.reservePin(5));assert(!b.load(doc.as<JsonObjectConst>()));}
  // Identity-bound declaration/grant policy never follows a child implicitly.
  write(root+"/app.json",R"({"type":"application","id":"cap-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"cap-app.elf","entry":"app_main","requires":[{"capability":"test.probe","api":1}]})");
  const char* grantBoot=R"({"board":"board.json","default_app":"cap-app.elf","drivers":[{"manifest":"probe.json","instance_id":7}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.probe","api":1,"instance_id":7}]}]})";
  write(root+"/boot.json",grantBoot);generation=0;lines.clear();
  {RiscTest::FileSources runtimeFiles15({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles15.bind(runtime));assert(runtime.prepare(root.c_str()));assert(runtime.run());}
  assert((lines==std::vector<std::string>{"CAP granted","CAP child denied","CAP granted"}));
  assert(parse(grantBoot,strlen(grantBoot),doc));doc["app_capabilities"][0]["grants"][0]["api"]=2;
  bad.clear();serializeJson(doc,bad);write(root+"/boot.json",bad);
  {RiscTest::FileSources runtimeFiles16({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles16.bind(runtime));assert(!runtime.prepare(root.c_str()));}
  assert(parse(grantBoot,strlen(grantBoot),doc));doc["app_capabilities"][0]["grants"][0]["instance_id"]=8;
  bad.clear();serializeJson(doc,bad);write(root+"/boot.json",bad);
  {RiscTest::FileSources runtimeFiles17({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles17.bind(runtime));assert(!runtime.prepare(root.c_str()));}
  puts("App grants: identity/declaration/exact instance, size/version checks, stale handles, child isolation and automatic revocation PASS");
  appPolicyCapacity(root,grantBoot);
  softwareBindings(root);
  optionalVolumeAuthority(root);
  driverCapacity(root);
  write(root+"/boot.json",R"({"board":"board.json","default_app":"heartbeat.elf","drivers":[]})");
  heartbeatMode=true;generation=0;lines.clear();
  {RiscTest::FileSources runtimeFiles18({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles18.bind(runtime));assert(runtime.prepare(root.c_str()));assert(runtime.run());}
  assert(beats==3 && lines.size()==3);
  for(size_t i=0;i<3;++i) {
    unsigned seq=0,uptime=0,heap=0,app=0;
    assert(sscanf(lines[i].c_str(),"RTE_HEARTBEAT version=1.0.0 target=host-test mac=00:00:00:00:00:00 sequence=%u uptime_ms=%u heap=%u app=0x%x",&seq,&uptime,&heap,&app)==4);
    assert(seq==i+1 && uptime==(i+1)*2000 && heap>0 && app==0x10000);
  }
  puts("Heartbeat default app: three exact-format monotonic health lines PASS");
  heartbeatMode=false;
  // Actual runtime + real pollable provider: no extra graph sleep, inactive and
  // wrong-owner yields do no work. An idle graph still cooperates exactly once.
  for(bool pollable:{false,true}) {
    JsonDocument pm;assert(parse(manifest,strlen(manifest),pm));
    pm["file_name"]="yield-probe.elf";std::string pmJson;serializeJson(pm,pmJson);
    write(root+"/yield-probe.json",pmJson);
    write(root+"/boot.json",pollable?
      R"({"board":"board.json","default_app":"yield.elf","drivers":[{"manifest":"yield-probe.json","instance_id":7}]})":
      R"({"board":"board.json","default_app":"yield.elf","drivers":[]})");
    waits.clear();polls=0;
    RiscTest::FileSources runtimeFiles19({root.c_str()});Runtime runtime({owner,health,delay,logLine});assert(runtimeFiles19.bind(runtime));runtime.yield(20);assert(waits.empty());
    assert(runtime.prepare(root.c_str()));assert(runtime.run());
    assert((waits==(pollable?std::vector<uint32_t>{1,1,1,20,50}:std::vector<uint32_t>{1,1,20,50})));
    assert(polls==(pollable?4u:0u));
    runtime.yield(20);assert(waits.size()==(pollable?5u:4u));
  }
  puts("Runtime cooperative yield: exactly one wait, active/idle, owner guard and 1..50ms bounds PASS");
  // The real runOne owns cancellation through load/init/fini/revoke/unload.
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[]})");
  generation=0;lines.clear();controlBegins.clear();controlEnds=0;
  Port cp{owner,health,delay,logLine};cp.controlBegin=controlBegin;cp.controlEnd=controlEnd;cp.controlExitRequested=controlRequest;cp.appExitSafe=controlSafe;
  {RiscTest::FileSources rFiles20({root.c_str()});Runtime r(cp);assert(rFiles20.bind(r));assert(r.prepare(root.c_str()));assert(r.run());}
  assert((controlBegins==std::vector<bool>{false,true,false,true,false}));
  assert(controlEnds==5&&!controlPending);
  assert(std::find(lines.begin(),lines.end(),"TEST native-exit-read")!=lines.end());
  assert(std::find(lines.begin(),lines.end(),"TEST child-relaunch-queued")!=lines.end());
  assert(std::find(lines.begin(),lines.end(),"RTE_CONTROL crown=exit cleanup=complete action=home")!=lines.end());
  generation=0;controlBegins.clear();controlEnds=0;controlRetain=true;
  // A retained image remains owned until native reset. Give the fixture itself
  // process-lifetime storage; destructing it would violate the custody contract.
  alignas(Runtime) static unsigned char retainedStorage[sizeof(Runtime)];
  static RiscTest::FileSources retainedRuntimeFiles21({root.c_str()});auto *retainedRuntime=new (retainedStorage) Runtime(cp);assert(retainedRuntimeFiles21.bind(*retainedRuntime));assert(retainedRuntime->prepare(root.c_str()));
  assert(!retainedRuntime->run()&&retainedRuntime->retained());
  assert(controlPending&&controlEnds==1&&controlBegins.size()==2);
  puts("Native host control: persistent child request, competing launch overridden after cleanup, failed custody keeps recovery armed PASS");
  puts("Runtime integration: mapped provider, default/child/default/missing/default, manifest/path rejection PASS");
}
