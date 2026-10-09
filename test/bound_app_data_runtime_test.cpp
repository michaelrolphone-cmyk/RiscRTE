#include "bootstrap/Runtime.h"
#include "runtime/storage/AppDataFiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
namespace fs=std::filesystem;
using RiscBoot::Runtime;
static std::string root;
static Runtime* running;
static bool owned=true,nativeSafe=true,dataSafe=true,failedStart=false,retainedTriggered=false,faultInStart=false;
static unsigned calls=0,loaded=0,unloaded=0,quiesced=0,stopped=0,diagnosed=0,appEntries=0,appFinis=0,starts=0;
static int faultOp=0;
static bool faultArmed=false,indirectFault=false,relayResult=false;
static int32_t otherStatus=0;
static unsigned relayStage=0;
static bool eagerRun=false,cooperativeArmed=false;
static unsigned cooperativeMode=0,cooperativeCalls=0;
static risc_bound_app_data_v1 saved[2]{},prior[2]{};
static bool owner(){return owned;}
static bool safe(){return nativeSafe;}
static bool health(risc_runtime_health_v1*){return true;}
static bool log(const char*){return true;}
static void delay(uint32_t){}
static RiscStorage::AppDataFiles files({nullptr,[](void*){return 0u;},[](void*){return true;},malloc,free});
static int32_t status(unsigned op){++calls;if(faultArmed && faultOp==int(op)){retainedTriggered=true;return RISC_APP_DATA_RETAINED;}return otherStatus;}
static RiscBoot::AppDataBackend backend{nullptr,
 [](void*,uint32_t n,const char* p,uint32_t* s,uint64_t* r){int32_t e=status(1);return e?e:files.stat(n,p,s,r);},
 [](void*,uint32_t n,const char* p,uint64_t v,void* b,uint32_t z,uint32_t* s,uint64_t* r){int32_t e=status(2);return e?e:files.read(n,p,v,b,z,s,r);},
 [](void*,uint32_t n,const char* p,uint64_t v,const void* b,uint32_t z){int32_t e=status(3);return e?e:files.replace(n,p,v,b,z);},
 [](void*){return dataSafe && files.exitSafe();}};
static RiscBoot::Port port(){RiscBoot::Port p{owner,health,delay,log};p.appData=&backend;p.providerStorageSafe=safe;return p;}
extern "C" void bound_files_denied(risc_bound_app_data_v1 api){
 if(!api.stat)return;
 unsigned before=calls;uint32_t size=42;uint64_t rev=42;char out=42;
 assert(api.stat(api.context,"state.bin",&size,&rev)==RISC_APP_DATA_CONTEXT && !size && !rev);
 size=42;rev=42;
 assert(api.read(api.context,"state.bin",1,&out,1,&size,&rev)==RISC_APP_DATA_CONTEXT && !size && !rev && out==42);
 assert(api.replace(api.context,"state.bin",1,"x",1)==RISC_APP_DATA_CONTEXT && calls==before);
}
extern "C" void bound_files_event(unsigned index,const char* event){
 assert(index<3 && !retainedTriggered);
 if(!strcmp(event,"load"))++loaded;
 if(!strcmp(event,"unload"))++unloaded;
 if(!strcmp(event,"start"))++starts;
 if(!strcmp(event,"quiesce"))++quiesced;
 if(!strcmp(event,"stop"))++stopped;
 if(!strcmp(event,"diagnostic"))++diagnosed;
}
static uint64_t revision(const risc_bound_app_data_v1& api,const char* name,uint32_t* actual=nullptr){
 uint32_t size=0;uint64_t rev=0;int32_t result=api.stat(api.context,name,&size,&rev);
 assert(result==RISC_APP_DATA_OK || result==RISC_APP_DATA_NOT_FOUND);
 assert((result==RISC_APP_DATA_OK)==bool(rev));if(actual)*actual=size;return rev;
}
static void put(const risc_bound_app_data_v1& api,const char* name,const std::string& data){
 auto rev=revision(api,name);assert(api.replace(api.context,name,rev,data.data(),uint32_t(data.size()))==RISC_APP_DATA_OK);
}
static std::string read(const risc_bound_app_data_v1& api,const char* name){
 uint32_t size=0,actual=0;uint64_t rev=revision(api,name,&size),got=0;
 std::string data(size,'\0');assert(api.read(api.context,name,rev,size?&data[0]:nullptr,size,&actual,&got)==RISC_APP_DATA_OK);
 assert(actual==size && got==rev);return data;
}
static void trigger(risc_bound_app_data_v1 api){
 faultArmed=true;
 const unsigned before=calls,events=loaded+unloaded+quiesced+stopped+diagnosed+starts;
 uint32_t size=42;uint64_t rev=42;char out=42;int32_t result;
 if(faultOp==1)result=api.stat(api.context,"state.bin",&size,&rev);
 else if(faultOp==2)result=api.read(api.context,"state.bin",1,&out,1,&size,&rev);
 else if(faultOp==3)result=api.replace(api.context,"state.bin",1,"x",1);
 else {dataSafe=false;result=api.stat(api.context,"state.bin",&size,&rev);retainedTriggered=true;}
 assert(result==RISC_APP_DATA_RETAINED && running->retained() && !running->active());
 assert(calls==before+unsigned(faultOp!=4));
 dataSafe=true;faultOp=0;
 bound_files_denied(api);bound_files_denied(saved[0]);bound_files_denied(saved[1]);
 assert(!risc_runtime_get_api(1));running->yield(1);assert(!running->launch("default.elf"));
 assert(loaded+unloaded+quiesced+stopped+diagnosed+starts==events);
}
extern "C" void bound_files_cooperative(unsigned index,bool service){
 assert(!retainedTriggered);++cooperativeCalls;
 if(cooperativeArmed && !index && cooperativeMode==(service?2u:1u))trigger(saved[0]);
}
extern "C" void bound_files_indirect(){trigger(saved[0]);}
extern "C" bool bound_files_relay_result(){return relayResult;}
extern "C" unsigned bound_files_relay_stage(){return relayStage;}
extern "C" bool bound_files_started(unsigned index,risc_bound_app_data_v1 api){
 bound_files_denied(prior[index]);bound_files_denied(saved[index]);
 assert(!saved[index].context || saved[index].context!=api.context);saved[index]=api;
 if(faultInStart && !index){trigger(api);return false;}
 // Actual files are available during start, before any app is active.
 if(!appEntries)assert(!running->active());
 if(revision(api,"state.bin"))assert(read(api,"state.bin")==std::string(index?"second":"first"));
 put(api,"state.bin",index?"second":"first");
 assert(read(api,"catalog.json")==std::string(index?"catalog-two":"catalog-one"));
 return !failedStart;
}
static void probes(){
 const auto api=saved[0];uint32_t size=42;uint64_t rev=42;char bytes[8];memset(bytes,42,sizeof(bytes));
 unsigned before=calls;
 for(const char* name:{static_cast<const char*>(nullptr),"",".hidden","../escape","bad/name","a\\b","a*","bad space","x\x7f"}){
  assert(api.stat(api.context,name,&size,&rev)==RISC_APP_DATA_INVALID && !size && !rev);
  assert(api.read(api.context,name,1,bytes,sizeof(bytes),&size,&rev)==RISC_APP_DATA_INVALID);
  assert(api.replace(api.context,name,1,"x",1)==RISC_APP_DATA_INVALID);
 }
 std::string longName(49,'x');assert(api.stat(api.context,longName.c_str(),&size,&rev)==RISC_APP_DATA_INVALID);
 for(const char* name:{"unknown","state.bin.x","STATE.bin"}){
  assert(api.stat(api.context,name,&size,&rev)==RISC_APP_DATA_CONTEXT);
  assert(api.read(api.context,name,1,bytes,sizeof(bytes),&size,&rev)==RISC_APP_DATA_CONTEXT);
  assert(api.replace(api.context,name,1,"x",1)==RISC_APP_DATA_CONTEXT);
 }
 assert(api.replace(api.context,"catalog.json",1,"x",1)==RISC_APP_DATA_CONTEXT);
 assert(api.stat(api.context,"state.bin",nullptr,&rev)==RISC_APP_DATA_INVALID);
 assert(api.stat(api.context,"state.bin",&size,nullptr)==RISC_APP_DATA_INVALID);
 assert(api.read(api.context,"state.bin",1,nullptr,1,&size,&rev)==RISC_APP_DATA_INVALID);
 assert(api.replace(api.context,"state.bin",1,nullptr,1)==RISC_APP_DATA_INVALID);
 assert(api.replace(api.context,"state.bin",1,"x",RISC_APP_DATA_FILE_MAX+1)==RISC_APP_DATA_INVALID);
 assert(calls==before && bytes[0]==42);
 owned=false;bound_files_denied(api);bound_files_denied(saved[1]);owned=true;
 auto token=revision(api,"state.bin");
 assert(api.read(api.context,"state.bin",token,nullptr,0,&size,&rev)==RISC_APP_DATA_BUFFER_SMALL && size==5 && rev==token);
 assert(api.read(api.context,"state.bin",token,bytes,4,&size,&rev)==RISC_APP_DATA_BUFFER_SMALL && size==5 && bytes[0]==42);
 put(api,"state.bin","fresh");
 assert(api.read(api.context,"state.bin",token,bytes,sizeof(bytes),&size,&rev)==RISC_APP_DATA_STALE && !size && !rev && bytes[0]==42);
 assert(api.replace(api.context,"state.bin",token,"stale",5)==RISC_APP_DATA_STALE);
 assert(read(saved[1],"state.bin")=="second");
 std::string full(RISC_APP_DATA_FILE_MAX,'x');put(api,"state.bin",full);assert(read(api,"state.bin")==full);
 put(api,"extra.bin","");assert(read(api,"extra.bin").empty());
 // The existing quota and full-file limit remain authoritative. A fifth file
 // in this namespace created by another authorized view still counts.
 uint32_t n;uint64_t r;assert(files.stat(5,"outside.bin",&n,&r)==RISC_APP_DATA_NOT_FOUND);
 assert(files.replace(5,"outside.bin",r,"x",1)==RISC_APP_DATA_OK);
 assert(api.replace(api.context,"fourth.bin",0,"x",1)==RISC_APP_DATA_NO_SPACE);
 put(api,"state.bin","first");
 // Preserve true non-retained backend failures, especially uncertain commits.
 for(int32_t result:{RISC_APP_DATA_IO,RISC_APP_DATA_UNAVAILABLE,RISC_APP_DATA_NO_SPACE,RISC_APP_DATA_COMMIT_UNKNOWN}){
  otherStatus=result;before=calls;
  assert(api.replace(api.context,"state.bin",1,"x",1)==result && calls==before+1);
  assert(!running->retained());
 }
 otherStatus=0;
}
extern "C" void bound_files_app_fini(){assert(!retainedTriggered);++appFinis;}
extern "C" void bound_files_app(){
 ++appEntries;const auto* rt=risc_runtime_get_api(1);assert(rt);
 if(eagerRun){assert(read(saved[0],"state.bin")=="first" && read(saved[1],"state.bin")=="second");return;}
 risc_runtime_capability_v1 first{},second{};first.struct_size=sizeof(first);second.struct_size=sizeof(second);
 if(indirectFault){
  const bool acquired=rt->acquire("test.files.relay",1,0,&first);
  if(acquired){assert(relayStage>=2);assert(!rt->release(&first));}
  assert(retainedTriggered);return;
 }
 if(!rt->acquire("test.files.first",1,0,&first)){assert(faultInStart && retainedTriggered);return;}
 assert(rt->acquire("test.files.second",1,0,&second));
 if(cooperativeMode){
  cooperativeArmed=true;
  if(cooperativeMode==1)rt->yield_ms(1);
  else {risc_runtime_capability_v1 extra{};extra.struct_size=sizeof(extra);assert(!rt->acquire("test.files.first",1,0,&extra));}
  assert(retainedTriggered);return;
 }
 if(faultOp){trigger(saved[0]);return;}
 probes();
 // Existing app-data uses its own explicit namespace and ordinary filename set.
 risc_runtime_capability_v1 app{};app.struct_size=sizeof(app);assert(rt->acquire(RISC_APP_DATA_CAPABILITY,1,9,&app));
 const auto own=*(const risc_app_data_v1*)app.api;put(own,"state.bin","app-nine");assert(read(saved[0],"state.bin")=="first");
 assert(rt->release(&app));bound_files_denied(own);
 auto old=saved[0];assert(rt->release(&first));bound_files_denied(old);
 assert(rt->acquire("test.files.first",1,0,&first));assert(saved[0].context!=old.context);bound_files_denied(old);
 // A transient native unsafe state permanently revokes both copied maps.
 nativeSafe=false;bound_files_denied(saved[0]);bound_files_denied(saved[1]);nativeSafe=true;
 bound_files_denied(saved[0]);bound_files_denied(saved[1]);
 assert(rt->release(&first));assert(rt->release(&second));
}
static void file(const std::string& name,const std::string& text){std::ofstream out(root+"/"+name);out<<text;assert(out.good());}
static JsonDocument parse(const std::string& s){JsonDocument doc;assert(!deserializeJson(doc,s));return doc;}
static void file(const std::string& name,const JsonDocument& doc){std::string s;serializeJson(doc,s);file(name,s);}
static const char* map0=R"([{"name":"catalog.json","namespace":5,"access":"read"},{"name":"state.bin","namespace":5,"access":"read-write"},{"name":"extra.bin","namespace":5,"access":"read-write"},{"name":"fourth.bin","namespace":5,"access":"read-write"}])";
static const char* map1=R"([{"name":"catalog.json","namespace":6,"access":"read"},{"name":"state.bin","namespace":6,"access":"read-write"}])";
static std::string driver(unsigned i){return std::string(R"({"type":"driver","id":")")+(i?"file-second":"file-first")+R"(","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider)"+std::to_string(i)+R"(.elf","requires":[{"capability":"storage.app-data.bound","api":1}],"provides":[{"capability":")"+(i?"test.files.second":"test.files.first")+R"(","api":1}]})";}
static const char* appManifest=R"({"type":"application","id":"file-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.files.first","api":1},{"capability":"test.files.second","api":1},{"capability":"storage.app-data","api":1}]})";
static JsonDocument boot(){return parse(std::string(R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"first.json","app_data":)")+map0+R"(},{"manifest":"second.json","app_data":)"+map1+R"(}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.files.first","api":1,"instance_id":0},{"capability":"test.files.second","api":1,"instance_id":0},{"capability":"storage.app-data","api":1,"instance_id":9}]}]})");}
static void install(){file("board.json",std::string(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})"));file("first.json",driver(0));file("second.json",driver(1));file("app.json",std::string(appManifest));file("boot.json",boot());}
static void admission(){
 unsigned before=calls,images=loaded;
 auto rejected=[&](){Runtime r(port());assert(!r.prepare(root.c_str()) && calls==before && loaded==images);};
 for(unsigned bad=0;bad<22;++bad){auto b=boot();auto entry=b["drivers"][0]["app_data"][0];
  switch(bad){
   case 0:b["drivers"][0]["app_data"]=nullptr;break;
   case 1:b["drivers"][0]["app_data"].to<JsonArray>();break;
   case 2:b["drivers"][0]["app_data"].as<JsonArray>().add(entry);break;
   case 3:entry["unknown"]=1;break;
   case 4:entry.remove("name");break;
   case 5:entry["name"]="bad/name";break;
   case 6:entry["name"]=".hidden";break;
   case 7:entry["name"]=std::string(49,'x');break;
   case 8:entry["namespace"]=0;break;
   case 9:entry["namespace"]=-1;break;
   case 10:entry["namespace"]=uint64_t(INT32_MAX)+1;break;
   case 11:entry["namespace"]="5";break;
   case 12:entry["namespace"]=true;break;
   case 13:entry["namespace"]=1.5;break;
   case 14:entry["access"]="write";break;
   case 15:entry["access"]=nullptr;break;
   case 16:entry["name"]="state.bin";break;
   case 17:b["drivers"][0].remove("app_data");break;
   case 18:b["drivers"][0]["app_data"]="bad";break;
   case 19:entry["name"]="wild*";break;
   case 20:b["drivers"][0]["app_data"][1]=17;break;
   case 21:entry["name"]="";break;
  }
  file("boot.json",b);rejected();
 }
 install();auto d=parse(driver(0));d["requires"][0]["api"]=2;file("first.json",d);rejected();
 d=parse(driver(0));d["requires"].to<JsonArray>();file("first.json",d);rejected();
 d=parse(driver(0));d["provides"][0]["capability"]=RISC_BOUND_APP_DATA_CAPABILITY;file("first.json",d);rejected();
 install();auto a=parse(appManifest);a["requires"][0]["capability"]=RISC_BOUND_APP_DATA_CAPABILITY;file("app.json",a);
 auto b=boot();b["app_capabilities"][0]["grants"][0]["capability"]=RISC_BOUND_APP_DATA_CAPABILITY;file("boot.json",b);rejected();
 install();{auto p=port();p.appData=nullptr;Runtime r(p);assert(!r.prepare(root.c_str()));}
 auto original=backend;
 for(unsigned i=0;i<4;++i){backend=original;if(i==0)backend.stat=nullptr;if(i==1)backend.read=nullptr;if(i==2)backend.replace=nullptr;if(i==3)backend.exitSafe=nullptr;Runtime r(port());assert(!r.prepare(root.c_str()));}
 backend=original;assert(calls==before && loaded==images);
 {Runtime r(port());assert(r.prepare(root.c_str()) && calls==before && loaded==images);}
 puts("Bound app-data exact policy/schema, backend-required, provider-only and metadata zero-I/O PASS");
}
int main(int argc,char** argv){
 assert(argc==2);root=argv[1];fs::create_directory(root+"/files");assert(files.configure((root+"/files").c_str()));
 assert(files.replace(5,"catalog.json",0,"catalog-one",11)==0);assert(files.replace(6,"catalog.json",0,"catalog-two",11)==0);
 install();admission();
 for(unsigned cycle=0;cycle<2;++cycle){
  // Remove the quota-test fourth committed file between ordinary boots.
  fs::remove(root+"/files/n00000005/outside.bin");
  appEntries=0;Runtime r(port());running=&r;assert(r.prepare(root.c_str()));assert(r.run() && !r.retained());
  assert(appEntries==1 && starts==quiesced && quiesced==stopped && loaded==unloaded);
  for(unsigned i=0;i<2;++i){bound_files_denied(saved[i]);prior[i]=saved[i];saved[i]={};}
 }
 auto eager=boot();eager["provider_activation"]="eager";file("boot.json",eager);
 eagerRun=true;appEntries=0;{Runtime r(port());running=&r;assert(r.prepare(root.c_str()));assert(r.run() && appEntries==1);}eagerRun=false;
 failedStart=true;{Runtime r(port());running=&r;unsigned before=diagnosed;assert(r.prepare(root.c_str()));assert(!r.run() && !r.retained());assert(diagnosed==before+1 && starts==quiesced && stopped==quiesced && loaded==unloaded);}failedStart=false;file("boot.json",boot());
 puts("Bound app-data real Runtime/ELF and file backend: read/replace/RO/namespaces/quota/revisions/release/restart/failure revocation PASS");
 // Retained invocations are intentionally never destroyed. Each isolated child
 // proves no post-fault provider callback, no fini and no retry after a reset flag.
 for(bool duringStart:{false,true})for(int op=1;op<=4;++op){
  file("boot.json",duringStart?eager:boot());pid_t pid=fork();assert(pid>=0);
  if(!pid){faultOp=op;faultInStart=duringStart;appEntries=0;unsigned finis=appFinis;
   auto r=std::make_unique<Runtime>(port());running=r.get();assert(r->prepare(root.c_str()));
   assert(!r->run() && r->retained() && retainedTriggered && appFinis==finis);
   if(duringStart)assert(!appEntries);
   unsigned before=calls;for(const auto& api:saved)bound_files_denied(api);assert(calls==before);
   (void)r.release();_exit(0);
  }
  int status=0;assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
 }
 file("boot.json",boot());
 puts("Bound app-data stat/read/replace/unsafe reset retention during eager start and app: sticky no-I/O/no-diagnostic/no-teardown PASS");
 for(unsigned mode=1;mode<=2;++mode){pid_t pid=fork();assert(pid>=0);
  if(!pid){faultOp=3;cooperativeMode=mode;appEntries=0;unsigned finis=appFinis;
   auto r=std::make_unique<Runtime>(port());running=r.get();assert(r->prepare(root.c_str()));
   assert(!r->run() && r->retained() && retainedTriggered && appFinis==finis);
   (void)r.release();_exit(0);
  }
  int status=0;assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
 }
 puts("Bound app-data retention from poll/service stops graph callbacks and app teardown PASS");
 // A provider without direct file authority can cause retention via a dependency.
 // Both successful and rejected relay starts must keep every image pinned.
 file("relay.json",std::string(R"({"type":"driver","id":"file-relay","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"relay.elf","requires":[{"capability":"test.files.first","api":1}],"provides":[{"capability":"test.files.relay","api":1}]})"));
 auto b=boot();b["drivers"].as<JsonArray>().add<JsonObject>()["manifest"]="relay.json";
 b["app_capabilities"][0]["grants"][0]["capability"]="test.files.relay";file("boot.json",b);
 auto a=parse(appManifest);a["requires"][0]["capability"]="test.files.relay";file("app.json",a);
 for(unsigned stage=0;stage<4;++stage)for(bool startResult:{false,true}){
  if(stage==1 && startResult)continue; // Diagnostics only run after a rejected start.
  pid_t pid=fork();assert(pid>=0);
  if(!pid){faultOp=3;indirectFault=true;relayResult=startResult;relayStage=stage;appEntries=0;unsigned finis=appFinis;
   auto r=std::make_unique<Runtime>(port());running=r.get();assert(r->prepare(root.c_str()));
   assert(!r->run() && r->retained() && retainedTriggered && appFinis==finis);
   (void)r.release();_exit(0);
  }
  int status=0;assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
 }
 puts("Bound app-data indirect dependency retention during start/diagnostic/quiesce/stop fences all subsequent callbacks PASS");
}
