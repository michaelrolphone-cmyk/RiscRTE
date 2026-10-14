#define NATIVE_ATOMIC_ENDPOINT_FIXTURE_ONLY
#include "native_atomic_key_value_test.cpp"
#include "bootstrap/Runtime.h"
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
using R=RiscBoot::Runtime;
static std::string directory,scenario;
static std::atomic<bool> sharedRetained{false};
static std::thread::id ownerThread=std::this_thread::get_id();
static A* endpoint;static A* otherEndpoint;static Gate* endpointGate;static Gate* otherGate;
static C* core;static C* otherCore;static R* runtime;
static risc_bound_key_value_v1 readers[2]{};
static unsigned legacyReads=0,legacyWrites=0;
static const RiscBoot::KeyValueBackend legacy{nullptr,
 [](void*,uint32_t ns,const char*,void* out,uint32_t,uint32_t*size)->int32_t{assert(ns==7);++legacyReads;memcpy(out,"other",5);*size=5;return 0;},
 [](void*,uint32_t ns,const char*,const void*,uint32_t)->int32_t{assert(ns==7);++legacyWrites;return 0;},2048};
static void readCheck(const risc_key_value_v1& kv,const char* expected,bool bound=false){
 char out[128];memset(out,'x',sizeof out);uint32_t n=99;
 assert(!kv.get(kv.context,"sample",out,sizeof out,&n)&&n==strlen(expected)&&!memcmp(out,expected,n));
 assert(kv.get(kv.context,"sample",nullptr,0,&n)==RISC_KEY_VALUE_BUFFER_SMALL&&n==strlen(expected));
 memset(out,'x',sizeof out);assert(kv.get(kv.context,"sample",out,1,&n)==RISC_KEY_VALUE_BUFFER_SMALL&&n==strlen(expected)&&out[0]=='x');
 assert(kv.get(kv.context,"missing",out,sizeof out,&n)==RISC_KEY_VALUE_NOT_FOUND&&!n&&out[0]=='x');
 const int big=kv.get(kv.context,"large",out,sizeof out,&n);
 if(kv.api_version==1)assert(big==RISC_KEY_VALUE_IO&&!n&&out[0]=='x');else assert(!big&&n==80&&out[0]=='b');
 assert(kv.put(kv.context,"sample","denied",6)==RISC_KEY_VALUE_CONTEXT);
 if(bound)assert(kv.get(kv.context,"unmapped",out,sizeof out,&n)==RISC_KEY_VALUE_CONTEXT&&!n);
}
static risc_key_value_v1 readerView(const risc_bound_key_value_v1& b){return {b.api_version,b.struct_size,b.context,b.get,b.put};}
extern "C" void test_atomic_bound_started(unsigned api,risc_bound_key_value_v1 table){readers[api-1]=table;readCheck(readerView(table),"native",true);}
extern "C" void test_atomic_bound_stopped(risc_bound_key_value_v1 table){char out='x';uint32_t n=1;assert(table.get(table.context,"sample",&out,1,&n)==RISC_BOUND_KEY_VALUE_CONTEXT&&!n&&out=='x');assert(table.put(table.context,"sample","x",1)==RISC_BOUND_KEY_VALUE_CONTEXT);}
static unsigned entries=0,finishes=0,phase=0;
static risc_key_value_atomic_v1 saved{},hostCopy{};
static bool owner(){return std::this_thread::get_id()==ownerThread;}
static bool health(risc_runtime_health_v1*){return true;}
static bool logLine(const char*){return true;}
static void delay(uint32_t){}
static risc_runtime_capability_v1 acquire(const risc_runtime_api_v1* rt,unsigned ns){
 risc_runtime_capability_v1 g{};g.struct_size=sizeof(g);assert(rt->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,ns,&g));return g;
}
static risc_key_value_atomic_state_v1 state(const risc_key_value_atomic_v1&a){risc_key_value_atomic_state_v1 s{sizeof(s)};assert(!a.state(a.context,&s));return s;}
static void invalid(const risc_key_value_atomic_v1&a){
 risc_key_value_atomic_state_v1 s{sizeof(s)};risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};
 char out='x';uint32_t n=42;uint64_t token=42;
 assert(a.state(a.context,&s)==RISC_ATOMIC_INVALID);
 assert(a.get(a.context,1,"sample",&out,1,&n)==RISC_ATOMIC_INVALID&&!n&&out=='x');
 assert(a.submit(a.context,1,nullptr,0,"intent",6,&token)==RISC_ATOMIC_INVALID&&!token);
 assert(a.collect(a.context,1,&receipt)==RISC_ATOMIC_INVALID);
 assert(a.cancel(a.context,1)==RISC_ATOMIC_INVALID);
}
extern "C" int runtime_atomic_worker_step(){
 bool ran=false;std::thread worker([&]{endpointGate->workerThread=std::this_thread::get_id();ran=endpoint->workerStep();});worker.join();endpointGate->workerThread={};return ran;
}
static uint64_t begin(const risc_key_value_atomic_v1&a,const char*value="copied"){
 auto s=state(a);char key[]="sample",intent[]="explicit-save";char bytes[32]{};strcpy(bytes,value);
 risc_key_value_atomic_mutation_v1 m{key,1,bytes,uint32_t(strlen(bytes))};uint64_t token=0;
 assert(!a.submit(a.context,s.revision,&m,1,intent,13,&token)&&token);
 memset(bytes,'x',sizeof bytes);memset(key,'x',sizeof key);memset(intent,'x',sizeof intent);return token;
}
static risc_key_value_atomic_receipt_v1 finish(const risc_key_value_atomic_v1&a,uint64_t token){risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};assert(!a.collect(a.context,token,&receipt));return receipt;}
extern "C" void runtime_atomic_wifi_controller(const risc_runtime_api_v1*,unsigned) __attribute__((weak));
extern "C" void test_atomic_app(unsigned hook,unsigned child){
 const auto* rt=risc_runtime_get_api(1);assert(rt);
 if(hook!=1){
  risc_runtime_capability_v1 g{sizeof(g)};assert(!rt->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,child?7:6,&g));
  if(hook==2){++finishes;if(saved.context)invalid(saved);}return;
 }
 ++entries;
 if(scenario=="controller"){assert(runtime_atomic_wifi_controller);runtime_atomic_wifi_controller(rt,phase);return;}
 if(scenario=="catalog"){
  auto atomic=acquire(rt,6);const auto a=*static_cast<const risc_key_value_atomic_v1*>(atomic.api);
  assert(state(a).namespace_id==6);
  risc_runtime_capability_v1 catalog{sizeof(catalog)},duplicate{sizeof(duplicate)};
  assert(rt->acquire(RISC_APP_CATALOG_CAPABILITY,1,0,&catalog));
  const auto c=*static_cast<const risc_app_catalog_v1*>(catalog.api);
  assert(c.struct_size==sizeof(c)&&c.context&&c.snapshot&&c.get&&catalog.slot!=atomic.slot);
  risc_app_catalog_snapshot_v1 snapshot{sizeof(snapshot)};
  assert(c.snapshot(c.context,&snapshot)==RISC_APP_CATALOG_OK&&snapshot.count==2&&snapshot.generation);
  risc_app_catalog_entry_v1 entry{sizeof(entry)};
  assert(c.get(c.context,snapshot.generation,0,&entry)==RISC_APP_CATALOG_OK&&!strcmp(entry.id,"atomic-main"));
  assert(!rt->acquire(RISC_APP_CATALOG_CAPABILITY,1,0,&duplicate));
  assert(state(a).namespace_id==6);assert(rt->release(&atomic));invalid(a);
  assert(c.snapshot(c.context,&snapshot)==RISC_APP_CATALOG_OK);
  atomic=acquire(rt,6);const auto renewed=*static_cast<const risc_key_value_atomic_v1*>(atomic.api);
  assert(rt->release(&catalog));assert(c.snapshot(c.context,&snapshot)==RISC_APP_CATALOG_CONTEXT);
  assert(state(renewed).namespace_id==6);assert(rt->release(&atomic));
  puts("Runtime atomic+catalog: distinct live tables, exact namespace, metadata reads and independent revocation PASS");return;
 }
 if(scenario=="readthrough"){
  auto atomic=acquire(rt,6);auto a=*static_cast<const risc_key_value_atomic_v1*>(atomic.api);
  risc_runtime_capability_v1 g[3]{};risc_key_value_v1 kv[3]{};
  for(unsigned i=0;i<3;i++){g[i].struct_size=sizeof(g[i]);assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,i==1?2:1,i==2?7:6,&g[i]));kv[i]=*static_cast<const risc_key_value_v1*>(g[i].api);}
  for(unsigned i=0;i<2;i++){readCheck(kv[i],"native");readCheck(readerView(readers[i]),"native",true);}
  char out[16];uint32_t n=0;assert(!kv[2].get(kv[2].context,"sample",out,sizeof out,&n)&&n==5&&!memcmp(out,"other",5));assert(!kv[2].put(kv[2].context,"sample","other",5)&&legacyReads==1&&legacyWrites==1);
  std::thread foreign([&]{for(auto& b:readers)test_atomic_bound_stopped(b);for(auto& k:kv){char out='x';uint32_t n=99;assert(k.get(k.context,"sample",&out,1,&n)==RISC_KEY_VALUE_CONTEXT&&!n);}});foreign.join();
  const auto t=begin(a);
  for(unsigned i=0;i<2;i++){n=99;assert(kv[i].get(kv[i].context,"sample",out,sizeof out,&n)==RISC_KEY_VALUE_BUSY&&!n);n=99;assert(readers[i].get(readers[i].context,"sample",out,sizeof out,&n)==RISC_BOUND_KEY_VALUE_BUSY&&!n);}
  assert(runtime_atomic_worker_step());assert(!finish(a,t).status);
  for(unsigned i=0;i<2;i++){readCheck(kv[i],"copied");readCheck(readerView(readers[i]),"copied",true);}
  for(auto& cap:g){assert(rt->release(&cap));}assert(rt->release(&atomic));assert(legacyReads==1&&legacyWrites==1);puts("Runtime atomic read-through: exact app/provider KV1/KV2 reads, bounds, Busy, denied legacy writes and unrelated namespace PASS");return;
 }
 if(child){
  invalid(saved);if(hostCopy.context)invalid(hostCopy);
  risc_runtime_capability_v1 denied{sizeof(denied)};assert(!rt->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,6,&denied));
  auto grant=acquire(rt,7);auto a=*static_cast<const risc_key_value_atomic_v1*>(grant.api);assert(state(a).namespace_id==7);assert(rt->release(&grant));return;
 }
 if(scenario=="normal"&&entries>1){invalid(saved);return;}
 risc_runtime_capability_v1 denied{sizeof(denied)};
 assert(!rt->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,0,&denied));
 assert(!rt->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,2,6,&denied));
 assert(!rt->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,8,&denied));
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,6,&denied));
 auto grant=acquire(rt,6);auto a=*static_cast<const risc_key_value_atomic_v1*>(grant.api);saved=a;
 if(scenario.rfind("guard-",0)==0){
  auto st=state(a);sharedRetained.store(true);
  if(scenario=="guard-exit")return;
  if(scenario=="guard-state")assert(a.state(a.context,&st)==RISC_ATOMIC_RETAINED);
  else if(scenario=="guard-get"){char out='x';uint32_t n=3;assert(a.get(a.context,st.revision,"sample",&out,1,&n)==RISC_ATOMIC_RETAINED&&!n&&out=='x');}
  else {uint64_t token=9;assert(a.submit(a.context,st.revision,nullptr,0,"intent",6,&token)==RISC_ATOMIC_RETAINED&&!token);}
  assert(runtime->retained());invalid(a);return;
 }
 if(scenario=="state-retained"){
  risc_key_value_atomic_state_v1 st{sizeof(st)};assert(a.state(a.context,&st)==RISC_ATOMIC_RETAINED&&runtime->retained());invalid(a);return;
 }
 assert(state(a).namespace_id==6);
 std::thread foreign([&]{invalid(a);});foreign.join();
 if(scenario=="resident"){
  hostCopy=a;risc_resident_client_v1 client{};client.struct_size=sizeof(client);assert(rt->resident_shell(&client));
  risc_resident_callbacks_v1 callbacks{};callbacks.api_version=1;callbacks.struct_size=sizeof(callbacks);
  callbacks.dispatch=[](void*,const risc_resident_request_v1*,risc_resident_reply_v1*)->int32_t{return RISC_RESIDENT_OK;};
  assert(!client.register_shell(client.invocation,&callbacks));
  risc_resident_result_v1 result{sizeof(result)};assert(!client.run_foreground(client.invocation,"child.elf",&result));
  assert(state(a).namespace_id==6);assert(rt->release(&grant));invalid(a);return;
 }
 const auto revision=state(a).revision;uint64_t token=begin(a);assert(!runtime->residentResetSafe());
 if(scenario=="pending")return;
 if(scenario=="retained"){
  endpointGate->finishResult=C::Retained;assert(runtime_atomic_worker_step());
  risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};
  assert(a.collect(a.context,token,&receipt)==RISC_ATOMIC_RETAINED&&runtime->retained());invalid(a);return;
 }
 auto peer=acquire(rt,6);auto p=*static_cast<const risc_key_value_atomic_v1*>(peer.api);
 risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};
 assert(p.collect(p.context,token,&receipt)==RISC_ATOMIC_INVALID&&p.cancel(p.context,token)==RISC_ATOMIC_INVALID);
 assert(a.collect(a.context,token+1,&receipt)==RISC_ATOMIC_INVALID);
 assert(a.collect(a.context,token,&receipt)==RISC_ATOMIC_PENDING);
 assert(!rt->release(&grant)&&grant.slot&&!runtime->retained());
 assert(!endpoint->workerStep());assert(runtime_atomic_worker_step());
 assert(!rt->release(&grant));receipt=finish(a,token);assert(!receipt.status&&receipt.operation_id&&receipt.revision>revision&&endpoint->scratchCleared());
 assert(get(*core,"sample")=="copied"&&runtime->residentResetSafe());
 assert(a.collect(a.context,token,&receipt)==RISC_ATOMIC_INVALID);assert(rt->release(&peer));
 uint64_t refused=9;assert(a.submit(a.context,revision,nullptr,0,"stale",5,&refused)==RISC_ATOMIC_STALE&&!refused);
 token=begin(a,"cancel");assert(a.cancel(a.context,token)==RISC_ATOMIC_PENDING);assert(runtime_atomic_worker_step());assert(finish(a,token).status==RISC_ATOMIC_CANCELLED&&get(*core,"sample")=="copied");
 token=begin(a,"committed");assert(runtime_atomic_worker_step());assert(a.cancel(a.context,token)==RISC_ATOMIC_PENDING);assert(!finish(a,token).status&&get(*core,"sample")=="committed");
 assert(rt->release(&grant));invalid(a);grant=acquire(rt,6);auto renewed=*static_cast<const risc_key_value_atomic_v1*>(grant.api);assert(renewed.context!=a.context);invalid(a);
 // Grant revocation at invocation return, followed by a fresh default.
 saved=renewed;assert(rt->request_launch("child.elf"));
}
static void file(const char* name,const std::string& content){std::ofstream(directory+"/"+name)<<content;}
static std::string manifest(const char* id,const char* elf,unsigned api=1){return std::string("{\"type\":\"application\",\"id\":\"")+id+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""+elf+"\",\"entry\":\"app_main\",\"requires\":[{\"capability\":\"storage.key-value.atomic\",\"api\":"+std::to_string(api)+"}]}";}
static std::string grant(unsigned ns,unsigned api=1){return "{\"capability\":\"storage.key-value.atomic\",\"api\":"+std::to_string(api)+",\"instance_id\":"+std::to_string(ns)+"}";}
static void boot(unsigned ns=6,unsigned api=1,bool multi=true){
 file("app.json",manifest("atomic-main","default.elf",api));file("child.json",manifest("atomic-child","child.elf"));
 file("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":[],\"app_capabilities\":[{\"manifest\":\"app.json\",\"grants\":["+grant(ns,api)+(multi?","+grant(7):"")+ "]},{\"manifest\":\"child.json\",\"grants\":["+grant(7)+"]}]"+(scenario=="resident"?",\"resident_shell\":{\"api\":1,\"host\":\"default.elf\",\"foreground\":[\"child.elf\"]}":"")+"}");
 if(scenario=="catalog"){
  auto app=manifest("atomic-main","default.elf",api);
  app.replace(app.find("\"requires\":[")+12,0,"{\"capability\":\"app.catalog\",\"api\":1},");file("app.json",app);
  file("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":[],\"app_capabilities\":[{\"manifest\":\"app.json\",\"grants\":[{\"capability\":\"app.catalog\",\"api\":1,\"instance_id\":0},"+grant(ns,api)+","+grant(7)+"]},{\"manifest\":\"child.json\",\"grants\":["+grant(7)+"]}]}");
 }
}
static bool bind(R&r){
 assert(!r.registerAtomicKeyValue("atomic-main",0,*endpoint));assert(!r.registerAtomicKeyValue("atomic-main",7,*endpoint));assert(!r.registerAtomicKeyValue("bad/id",6,*endpoint));
 assert(r.registerAtomicKeyValue("atomic-main",6,*endpoint));assert(!r.registerAtomicKeyValue("atomic-main",6,*endpoint));
 if(scenario=="readthrough"){assert(r.registerAtomicKeyValueReadThrough(6,*endpoint));assert(!r.registerAtomicKeyValueReadThrough(6,*endpoint));assert(!r.registerAtomicKeyValueReadThrough(7,*endpoint));}
 assert(r.registerAtomicKeyValue("atomic-main",7,*otherEndpoint));assert(r.registerAtomicKeyValue("atomic-child",7,*otherEndpoint));return true;
}
int main(int argc,char** argv){
 assert(argc==3);directory=argv[1];scenario=argv[2];
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 Disk d,otherDisk;Alloc allocation;C c(6,d.backend(),limits(),allocation.api()),other(7,otherDisk.backend(),limits(),allocation.api());assert(!c.initialize(nullptr,0,{})&&!other.initialize(nullptr,0,{}));
 Gate gate,otherG;auto guard=gate.api();guard.retained=[](void*){return sharedRetained.load();};
 if(scenario=="state-retained")guard.release=[](void*){return false;};
 A a(c,guard),otherA(other,otherG.api());endpoint=&a;otherEndpoint=&otherA;endpointGate=&gate;otherGate=&otherG;core=&c;otherCore=&other;
 boot();
 if(scenario=="admission"){
  {R r({owner,health,delay,logLine});assert(!r.prepare(directory.c_str()));}
  for(unsigned ns:{0u,8u}){boot(ns);R r({owner,health,delay,logLine,bind});assert(!r.prepare(directory.c_str()));}
  boot(6,2);{R r({owner,health,delay,logLine,bind});assert(!r.prepare(directory.c_str()));}
  boot();file("app.json",manifest("unregistered","default.elf"));{R r({owner,health,delay,logLine,bind});assert(!r.prepare(directory.c_str()));}
  boot();{R r({owner,health,delay,logLine,bind});assert(r.prepare(directory.c_str()));assert(!r.registerAtomicKeyValue("late",6,a));}
  puts("Runtime atomic exact app/namespace/API admission and trusted registration PASS");return 0;
 }
 if(scenario=="readthrough"){
  assert(!put(c,"sample","native"));std::string bytes(80,'b');assert(!put(c,"large",bytes.c_str()));
  JsonDocument doc;std::ifstream input(directory+"/boot.json");assert(!deserializeJson(doc,input));
  auto req=doc["app_capabilities"][0]["grants"].as<JsonArray>();
  for(unsigned i=0;i<3;i++){auto g=req.add<JsonObject>();g["capability"]=RISC_KEY_VALUE_CAPABILITY;g["api"]=i==1?2:1;g["instance_id"]=i==2?7:6;}
  JsonDocument manifestDoc;std::ifstream appInput(directory+"/app.json");assert(!deserializeJson(manifestDoc,appInput));
  for(unsigned v=1;v<=2;v++){auto q=manifestDoc["requires"].as<JsonArray>().add<JsonObject>();q["capability"]=RISC_KEY_VALUE_CAPABILITY;q["api"]=v;
   const auto id=std::to_string(v);file(("reader"+id+".json").c_str(),"{\"type\":\"driver\",\"id\":\"atomic-reader-"+id+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"driver_abi\":2,\"file_name\":\"reader"+id+".elf\",\"requires\":[{\"capability\":\"storage.key-value.bound\",\"api\":"+id+"}],\"provides\":[{\"capability\":\"test.atomic.reader"+id+"\",\"api\":1}]}");
   auto d=doc["drivers"].as<JsonArray>().add<JsonObject>();d["manifest"]="reader"+id+".json";auto keys=d["key_value"].to<JsonArray>();
   for(const char*key:{"sample","large","missing"}){auto k=keys.add<JsonObject>();k["key"]=key;k["namespace"]=6;k["access"]="read-write";}
  }
  std::string json;serializeJson(doc,json);file("boot.json",json);json.clear();serializeJson(manifestDoc,json);file("app.json",json);
 }
 R r({owner,health,delay,logLine,bind,scenario=="readthrough"?&legacy:nullptr});runtime=&r;assert(r.prepare(directory.c_str()));
 if(scenario=="pending"||scenario=="retained"||scenario=="state-retained"||scenario.rfind("guard-",0)==0){
  assert(!r.run()&&r.retained()&&finishes==0&&!r.residentResetSafe());invalid(saved);
  // No destructor or cleanup retries are permitted for retained custody.
  puts("Runtime atomic pending/terminal custody pins invocation and rejects stale API PASS");fflush(stdout);_exit(0);
 }
 assert(r.run()&&!r.retained());if(saved.context)invalid(saved);
 if(scenario=="readthrough")for(auto& b:readers)test_atomic_bound_stopped(b);
 if(scenario=="normal")assert(entries==3&&finishes==3);
 if(scenario=="resident")assert(entries==2&&finishes==2);
 if(scenario=="controller"){
  C reboot(6,d.backend(),limits(),allocation.api());assert(!reboot.load());Gate restoredGate;A restored(reboot,restoredGate.api());endpoint=&restored;endpointGate=&restoredGate;core=&reboot;phase=1;
  R next({owner,health,delay,logLine,bind});runtime=&next;assert(next.prepare(directory.c_str())&&next.run()&&!next.retained());
 }
 puts("Real Runtime atomic grants: owner/worker, copied inputs, exact receipt, cancel races, stale tables, handoff and fresh invocation PASS");
}
