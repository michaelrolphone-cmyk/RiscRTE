#include "bootstrap/Runtime.h"
#include "RuntimeFileFixture.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <string>
#include <cstring>
#include <unistd.h>
using namespace RiscBoot;
static std::string mode,root;
static bool owned=true,metadataFault=false;
static unsigned opens=0,getCalls=0,putCalls=0,destroyed=0,statusCalls=0,checkpointCalls=0;
static Runtime* runtime;
static bool owner(){return owned;}
struct Session final:SettingsSession {
 mutable bool terminal=false;
 ~Session(){++destroyed;}
 uint32_t maxBlobSize() const override{if(metadataFault){if(mode.find("retained")!=std::string::npos)terminal=true;else owned=false;}return 2048;}
 int32_t get(uint32_t ns,const char*key,void*out,uint32_t cap,uint32_t*n,uint32_t,bool) override {
  assert(ns==7&&std::string(key)=="mode"&&cap>=5);++getCalls;memset(out,'x',cap);*n=5;
  if(mode=="partial")return RISC_KEY_VALUE_IO;
  if(mode=="oversize"){*n=cap+1;return 0;}
  if(mode=="empty"){*n=0;return 0;}
  if(mode=="owner-loss"){owned=false;return 0;}
  if(mode=="retained-read"){terminal=true;return 0;}
  std::ifstream f(root+"/../configuration");std::string v;f>>v;if(v.empty())return RISC_KEY_VALUE_NOT_FOUND;
  *n=v.size();memcpy(out,v.data(),*n);return 0;
 }
 int32_t put(uint32_t ns,const char*key,const void*p,uint32_t n,bool) override{
  assert(ns==7&&std::string(key)=="mode");++putCalls;
  if(mode=="retained-write"){terminal=true;return 0;}
  std::ofstream(root+"/../configuration").write(static_cast<const char*>(p),n);return 0;
 }
 bool configure(uint32_t,uint32_t,bool,const uint32_t*,uint32_t) override{return true;}
 uint32_t checkpointCount()const override{return 0;} uint32_t format()const override{return 1;}
 uint32_t sharedCount()const override{return 0;} uint32_t sharedNamespace(uint32_t)const override{return 0;}
 uint32_t status(uint32_t)const override{++statusCalls;if(mode=="retained-status")terminal=true;if(mode=="owner-status")owned=false;return 1;}
 int32_t restore(uint32_t,const char*,const void*,uint32_t,bool)override{++checkpointCalls;return RISC_KEY_VALUE_CONTEXT;}
 int32_t snapshot(uint32_t,void*,uint32_t,uint32_t*,uint64_t*)override{++checkpointCalls;return RISC_KEY_VALUE_CONTEXT;}
 int32_t acknowledge(uint32_t,uint64_t)override{++checkpointCalls;return RISC_KEY_VALUE_CONTEXT;}
 bool launchReady(uint32_t,bool*)const override{return true;}
 bool retained()const override{return terminal;}
};
static Session* current;
static SettingsProvider provider{nullptr,[](void*)->std::unique_ptr<SettingsSession>{++opens;auto p=std::make_unique<Session>();current=p.get();if(mode=="factory-retained")p->terminal=true;if(mode=="factory-owner")owned=false;return p;}};
extern "C" void settings_service_test_entry(){
 auto*rt=risc_runtime_get_api(1);assert(rt);if(mode=="no-storage")return;risc_runtime_capability_v1 grant{};grant.struct_size=sizeof grant;
 if(mode.find("preference-")==0){
  assert(rt->acquire("storage.preferences",1,7,&grant));auto*api=static_cast<const risc_preference_store_v1*>(grant.api);
  risc_preference_store_state_v1 state{};state.struct_size=sizeof state;assert(api->state(api->context,&state)==0);
  metadataFault=true;int32_t rc;uint64_t token=77;
  if(mode.find("submit")!=std::string::npos)rc=api->submit(api->context,state.revision,nullptr,0,"intent",6,&token);
  else {state.namespace_id=999;rc=api->state(api->context,&state);assert(state.namespace_id==999);}
  const bool retained=mode.find("retained")!=std::string::npos;
  assert(rc==(retained?RISC_ATOMIC_RETAINED:RISC_ATOMIC_INVALID)&&!getCalls&&!putCalls);
  if(mode.find("submit")!=std::string::npos)assert(token==0);
  if(retained)_exit(0);
  owned=true;metadataFault=false;assert(rt->release(&grant));return;
 }
 assert(rt->acquire("storage.key-value",1,7,&grant));auto*kv=static_cast<const risc_key_value_v1*>(grant.api);
 if(mode=="retained-status"||mode=="owner-status"){
  assert(Runtime::installationSettingsRestore(runtime,1,"test",nullptr,0,true)==RISC_KEY_VALUE_CONTEXT);
  uint32_t n=99;uint64_t generation=0;char out[8];
  assert(Runtime::installationSettingsSnapshot(runtime,1,out,sizeof out,&n,&generation)==RISC_KEY_VALUE_CONTEXT&&n==0);
  assert(Runtime::installationSettingsAck(runtime,1,1)==RISC_KEY_VALUE_CONTEXT);
  assert(statusCalls==1&&checkpointCalls==0);
  if(mode=="retained-status")_exit(0);
  owned=true;assert(rt->release(&grant));return;
 }
 if(mode=="retained-before")current->terminal=true;
 char out[16];memset(out,'!',sizeof out);uint32_t n=99;
 if(mode=="normal"||mode=="restart"){
  if(mode=="normal")assert(kv->put(kv->context,"mode","saved",5)==0);
  assert(kv->get(kv->context,"mode",out,sizeof out,&n)==0&&n==5&&std::string(out,n)=="saved");
  assert(rt->release(&grant));return;
 }
 int32_t rc=mode=="retained-write"?kv->put(kv->context,"mode","saved",5):kv->get(kv->context,"mode",out,sizeof out,&n);
 const bool lost=mode=="owner-loss"||mode.find("retained")==0;
 assert(rc==(lost?RISC_KEY_VALUE_CONTEXT:RISC_KEY_VALUE_IO));
 if(mode!="retained-write"){assert(n==0);for(char c:out)assert(c=='!');}
 if(mode.find("retained")==0){
  const auto before=getCalls+putCalls;
  assert(kv->get(kv->context,"mode",out,sizeof out,&n)==RISC_KEY_VALUE_CONTEXT);
  assert(kv->put(kv->context,"mode","saved",5)==RISC_KEY_VALUE_CONTEXT);
  assert(getCalls+putCalls==before&&destroyed==0);
  if(mode=="retained-before")assert(getCalls==0);
  _exit(0); // terminal session custody intentionally outlives this process
 }
 owned=true;assert(rt->release(&grant));
}
int main(int argc,char**argv){
 assert(argc==3);root=std::string(argv[1])+"/store";mode=argv[2];
 std::filesystem::create_directories(root);std::filesystem::copy_file(std::string(argv[1])+"/default.elf",root+"/default.elf",std::filesystem::copy_options::overwrite_existing);
 std::ofstream(root+"/cohort.json")<<"{}";
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 std::ofstream(root+"/app.json")<<R"({"type":"application","id":"settings-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"storage.key-value","api":1}]})";
 std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"storage.key-value","api":1,"instance_id":7}]}]})";
 RiscTest::FileSources sources({root.c_str()});Port p{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};p.settings=&provider;
 if(mode.find("preference-")==0){
  for(const char* name:{"app.json","boot.json"}){const std::string path=root+"/"+name;std::ifstream in(path);std::string text{std::istreambuf_iterator<char>(in),{}};const auto at=text.find("storage.key-value");assert(at!=std::string::npos);text.replace(at,17,"storage.preferences");std::ofstream(path)<<text;}
 }
 if(mode=="absent"||mode=="no-storage")p.settings=nullptr;
 if(mode=="no-storage"||mode.find("factory-")==0){
  std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"default.elf","drivers":[]})";
 }
 SettingsProvider unavailable{};if(mode=="unavailable")p.settings=&unavailable;
 {Runtime r(p);runtime=&r;assert(sources.bind(r));
  if(mode=="absent"||mode=="unavailable"){assert(!r.prepare(root.c_str()));assert(!opens);return 0;}
  if(mode.find("factory-")==0){
   assert(!r.prepare(root.c_str())&&opens==1&&!getCalls&&!putCalls);
   if(mode=="factory-retained"){assert(r.retained()&&!destroyed);_exit(0);}
   owned=true;return 0;
  }
  assert(r.prepare(root.c_str()));
  auto* active=current;
  if(mode=="normal"){
   {Runtime candidate({});assert(r.validateCohort(candidate,root.c_str(),[](void*,const char*,bool){return true;},nullptr));assert(current!=active&&opens==2&&!destroyed);}
   assert(destroyed==1);current=active;
   {Runtime rejected({});assert(!r.validateCohort(rejected,root.c_str(),[](void*,const char*,bool){return false;},nullptr));assert(current!=active&&opens==3);}
   assert(destroyed==2);current=active;
  }
  assert(r.run());}
 assert(opens==(mode=="no-storage"?0u:mode=="normal"?3u:1u)&&destroyed==opens);
 printf("Semantic settings Runtime %s: backend absent, copied reads, owner/custody boundary PASS\n",mode.c_str());
}
