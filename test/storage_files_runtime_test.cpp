#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main storage_files_adapter_fixture_main
#include "storage_files_adapter_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "bootstrap/Runtime.h"
#include <fstream>
#include <thread>
#include <cassert>
#include <unistd.h>
#include <filesystem>
using R=RiscBoot::Runtime;
static std::thread::id ownerThread=std::this_thread::get_id();
static Backend volumes[2];
static GenericProvider generic[2];
static bool direct;
static const risc_runtime_api_v1* reentrantRuntime;
static risc_runtime_capability_v1* reentrantGrant;
static bool reentrantChecked;
static int32_t (*originalOpen)(void*,const char*,uint32_t,uint32_t*);

static R* runtime;
static std::string directory,scenario;
static risc_storage_files_v1 saved{};
static int32_t reentrantOpen(void* c,const char* path,uint32_t flags,uint32_t* out){
 assert(reentrantRuntime&&!reentrantRuntime->release(reentrantGrant));
 risc_runtime_capability_v1 denied{sizeof(denied)};assert(!reentrantRuntime->acquire(RISC_STORAGE_FILES_CAPABILITY,1,101,&denied));
 risc_storage_files_stat_v1 st{sizeof(st)};assert(saved.stat(saved.context,"/file",&st)==RISC_FILES_CONTEXT);
 assert(!runtime->residentResetSafe());reentrantChecked=true;return originalOpen(c,path,flags,out);
}
static bool owner(){return std::this_thread::get_id()==ownerThread;}
static bool health(risc_runtime_health_v1*){return true;}
static bool logLine(const char* s){fprintf(stderr,"%s\n",s);return true;}
static void delay(uint32_t){}
extern "C" const void* storage_runtime_backend(unsigned i){assert(i<2);return direct?static_cast<const void*>(&generic[i].api):static_cast<const void*>(volumes[i].base());}
extern "C" bool storage_runtime_backend_safe(unsigned i){assert(i<2);return direct?generic[i].handles.empty():volumes[i].handles.empty();}
extern "C" const char* storage_runtime_capability(){return direct?"storage.files":"storage.volume";}
static risc_runtime_capability_v1 acquire(const risc_runtime_api_v1* rt,const char* cap,uint64_t ns){risc_runtime_capability_v1 g{sizeof(g)};assert(rt->acquire(cap,1,ns,&g));return g;}
static void invalid(const risc_storage_files_v1& a){
 uint32_t token=99,count=99;char data='x';risc_storage_files_stat_v1 st{sizeof(st)};
 assert(a.open(a.context,"/file",RISC_FILES_READ,&token)==RISC_FILES_CONTEXT&&!token);
 assert(a.read(a.context,1,&data,1,&count)==RISC_FILES_CONTEXT&&!count&&data=='x');
 assert(a.close(a.context,1)==RISC_FILES_CONTEXT&&a.stat(a.context,"/file",&st)==RISC_FILES_CONTEXT);
}
extern "C" void test_atomic_app(unsigned hook,unsigned){
 const auto* rt=risc_runtime_get_api(1);assert(rt);
 if(hook!=1){risc_runtime_capability_v1 g{sizeof(g)};assert(!rt->acquire(RISC_STORAGE_FILES_CAPABILITY,1,0,&g));if(hook==2&&saved.context)invalid(saved);return;}
 volumes[0].calls.clear();volumes[1].calls.clear();
 auto catalog=acquire(rt,RISC_STORAGE_FILES_CATALOG_CAPABILITY,0);auto c=*static_cast<const risc_storage_files_catalog_v1*>(catalog.api);
 risc_storage_files_catalog_snapshot_v1 snapshot{sizeof(snapshot)};assert(!c.snapshot(c.context,&snapshot));
 assert(snapshot.count==(scenario=="unique"?1u:2u)&&snapshot.revision);
 risc_storage_files_namespace_v1 ns{sizeof(ns)};assert(!c.get(c.context,snapshot.revision,0,&ns)&&ns.namespace_id==101&&!strcmp(ns.label,"Documents"));
 if(snapshot.count==2)assert(!c.get(c.context,snapshot.revision,1,&ns)&&ns.namespace_id==202&&!strcmp(ns.label,"Archive"));
 assert(c.get(c.context,snapshot.revision+1,0,&ns)==RISC_FILES_STALE);
 assert(c.get(c.context,snapshot.revision,snapshot.count,&ns)==RISC_FILES_NOT_FOUND);
 assert(volumes[0].totalCalls()==0&&volumes[1].totalCalls()==0&&generic[0].totalCalls()==0&&generic[1].totalCalls()==0);
 auto peerCatalog=acquire(rt,RISC_STORAGE_FILES_CATALOG_CAPABILITY,0);auto pc=*static_cast<const risc_storage_files_catalog_v1*>(peerCatalog.api);
 assert(pc.get(pc.context,snapshot.revision,0,&ns)==RISC_FILES_STALE);assert(rt->release(&peerCatalog));
 risc_runtime_capability_v1 denied{sizeof(denied)};
 if(scenario!="unique")assert(!rt->acquire(RISC_STORAGE_FILES_CAPABILITY,1,0,&denied));
 assert(!rt->acquire("storage.volume",1,42,&denied));assert(!rt->acquire(RISC_STORAGE_FILES_CAPABILITY,1,42,&denied));
 if(scenario=="malformed-direct"){assert(!rt->acquire(RISC_STORAGE_FILES_CAPABILITY,1,101,&denied));assert(!runtime->retained()&&generic[0].totalCalls()==0&&volumes[0].totalCalls()==0);assert(rt->release(&catalog));return;}
 auto grant=acquire(rt,RISC_STORAGE_FILES_CAPABILITY,scenario=="unique"?0:101);auto a=*static_cast<const risc_storage_files_v1*>(grant.api);saved=a;
 if(scenario=="unavailable"){generic[0].results["stat"]=RISC_FILES_UNAVAILABLE;risc_storage_files_stat_v1 info{sizeof(info)};assert(a.stat(a.context,"/file",&info)==RISC_FILES_UNAVAILABLE&&!runtime->retained());assert(!c.snapshot(c.context,&snapshot)&&snapshot.count==2);assert(rt->release(&grant)&&rt->release(&catalog));return;}
 std::thread foreign([&]{invalid(a);risc_storage_files_catalog_snapshot_v1 s{sizeof(s)};assert(c.snapshot(c.context,&s)==RISC_FILES_CONTEXT);});foreign.join();
 if(scenario=="reentrant"){reentrantRuntime=rt;reentrantGrant=&grant;originalOpen=generic[0].api.open;generic[0].api.open=reentrantOpen;}
 uint32_t h=0;assert(!a.open(a.context,"/file",RISC_FILES_READ|RISC_FILES_WRITE,&h)&&h);
 assert(!rt->release(&grant)&&!runtime->residentResetSafe());
 if(scenario=="reentrant")assert(reentrantChecked);
 if(scenario=="open-exit")return;
 if(scenario=="failed-close"){
  volumes[0].fail="file_close";assert(a.close(a.context,h)==RISC_FILES_RETAINED&&runtime->retained());invalid(a);return;
 }
 auto peer=acquire(rt,RISC_STORAGE_FILES_CAPABILITY,101);auto p=*static_cast<const risc_storage_files_v1*>(peer.api);
 assert(p.close(p.context,h)==RISC_FILES_INVALID);assert(rt->release(&peer));
 char data[8]{};uint32_t n=0;assert(!a.read(a.context,h,data,6,&n)&&n==6&&!memcmp(data,"abcdef",6));
 assert(!a.seek(a.context,h,0));assert(!a.write(a.context,h,"XY",2,&n)&&n==2);assert(!a.sync(a.context,h));assert(!a.close(a.context,h));
 assert(runtime->residentResetSafe()&&(direct?generic[0].files["/file"].bytes:volumes[0].nodes["/file"].data)=="XYcdef");
 uint32_t d=0;assert(!a.dir_open(a.context,"/dir",&d));risc_storage_files_entry_v1 entry{sizeof(entry)};assert(!a.dir_next(a.context,d,&entry));assert(!a.dir_close(a.context,d));
 assert(rt->release(&grant));invalid(a);auto renewed=acquire(rt,RISC_STORAGE_FILES_CAPABILITY,101);assert(static_cast<const risc_storage_files_v1*>(renewed.api)->context!=a.context);assert(rt->release(&renewed));
 if(scenario!="unique"){auto second=acquire(rt,RISC_STORAGE_FILES_CAPABILITY,202);auto b=*static_cast<const risc_storage_files_v1*>(second.api);uint32_t file=0;assert(!b.open(b.context,"/file",RISC_FILES_READ,&file));assert(!b.read(b.context,file,data,6,&n)&&n==6&&!memcmp(data,"abcdef",6));assert(!b.close(b.context,file)&&rt->release(&second));}
 assert(rt->release(&catalog));assert(c.snapshot(c.context,&snapshot)==RISC_FILES_CONTEXT);
}
static void file(const char* name,const std::string& text){std::ofstream(directory+"/"+name)<<text;}
static void boot(){
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 for(unsigned i=0;i<2;++i){const std::string index=std::to_string(i);file(("provider"+index+".json").c_str(),"{\"type\":\"driver\",\"id\":\"runtime-files-"+(i?std::string("second"):std::string("first"))+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"driver_abi\":2,\"file_name\":\"provider"+index+".elf\",\"requires\":[],\"provides\":[{\"capability\":\"storage.volume\",\"api\":1}]}");}
 file("app.json",R"({"type":"application","id":"files-main","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"storage.files","api":1},{"capability":"storage.files.catalog","api":1}]})");
 file("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":[{\"manifest\":\"provider0.json\"},{\"manifest\":\"provider1.json\"}],\"app_capabilities\":[{\"manifest\":\"app.json\",\"grants\":[{\"capability\":\"storage.files\",\"api\":1,\"instance_id\":101,\"backend_capability\":\"storage.volume\",\"backend_instance\":0,\"backend_provider\":\"runtime-files-first\",\"label\":\"Documents\"},"+(scenario=="unique"?std::string():std::string("{\"capability\":\"storage.files\",\"api\":1,\"instance_id\":202,\"backend_capability\":\"storage.volume\",\"backend_instance\":0,\"backend_provider\":\"runtime-files-second\",\"label\":\"Archive\"},"))+"{\"capability\":\"storage.files.catalog\",\"api\":1,\"instance_id\":0}]}]}");
 if(direct)for(const char* name:{"provider0.json","provider1.json","boot.json"}){std::ifstream input(directory+"/"+name);std::string text((std::istreambuf_iterator<char>(input)),{});size_t pos=0;while((pos=text.find("storage.volume",pos))!=std::string::npos){text.replace(pos,14,"storage.files");pos+=13;}file(name,text);}
}
static void edit(const char* name,const std::string& from,const std::string& to){
 std::ifstream input(directory+"/"+name);std::string text((std::istreambuf_iterator<char>(input)),{});const auto at=text.find(from);assert(at!=std::string::npos);text.replace(at,from.size(),to);file(name,text);
}
static void policyTests(std::string base){
 directory=base+"/old";std::filesystem::create_directories(directory);boot();file("cohort.json","{}");
 for(const char* name:{"default.elf","provider0.elf","provider1.elf"})std::filesystem::copy_file(base+"/"+name,directory+"/"+name,std::filesystem::copy_options::overwrite_existing);
 const auto old=directory;R source({owner,health,delay,logLine});assert(source.prepare(old.c_str()));
 directory=base+"/new";std::filesystem::copy(old,directory,std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
 auto admitted=[](void*,const char* path,bool){return std::filesystem::is_regular_file(path);};
 {R candidate({});assert(source.validateCohort(candidate,directory.c_str(),admitted,nullptr));}
 edit("boot.json","Documents","Renamed");{R candidate({});assert(source.validateCohort(candidate,directory.c_str(),admitted,nullptr));}
 boot();edit("boot.json","\"backend_provider\":\"runtime-files-first\"","\"backend_provider\":\"runtime-files-second\"");
 {R candidate({});assert(!source.validateCohort(candidate,directory.c_str(),admitted,nullptr));}
 boot();edit("boot.json","\"instance_id\":101","\"instance_id\":303");{R candidate({});assert(!source.validateCohort(candidate,directory.c_str(),admitted,nullptr));}
 for(unsigned bad=0;bad<6;++bad){boot();
  if(bad==0)edit("boot.json",",\"backend_provider\":\"runtime-files-first\"","");
  if(bad==1)edit("boot.json","\"backend_instance\":0","\"backend_instance\":9");
  if(bad==2)edit("boot.json","\"backend_provider\":\"runtime-files-first\"","\"backend_provider\":\"missing\"");
  if(bad==3)edit("boot.json","\"instance_id\":101","\"instance_id\":0");
  if(bad==4)edit("boot.json","Documents",std::string(49,'x'));
  if(bad==5)edit("boot.json",direct?"\"backend_capability\":\"storage.files\"":"\"backend_capability\":\"storage.volume\"","\"backend_capability\":\"unknown\"");
  R candidate({owner,health,delay,logLine});assert(!candidate.prepare(directory.c_str()));
 }
 // Omission means unique provider selection. An explicitly supplied ID must
 // be a nonempty string, including when there is only one possible provider.
 for(unsigned form=0;form<7;++form){boot();JsonDocument doc;{std::ifstream input(directory+"/boot.json");assert(!deserializeJson(doc,input));}
  doc["drivers"].as<JsonArray>().remove(1);auto grants=doc["app_capabilities"][0]["grants"].as<JsonArray>();grants.remove(1);auto grant=grants[0].as<JsonObject>();
  if(form==0)grant.remove("backend_provider");
  if(form==2)grant["backend_provider"]=nullptr;
  if(form==3)grant["backend_provider"]="";
  if(form==4)grant["backend_provider"]=7;
  if(form==5)grant["backend_provider"]=true;
  if(form==6)grant["backend_provider"].to<JsonObject>();
  {std::ofstream output(directory+"/boot.json");serializeJson(doc,output);}
  R candidate({owner,health,delay,logLine});assert(candidate.prepare(directory.c_str())==(form<2));
 }
 assert(volumes[0].totalCalls()==0&&volumes[1].totalCalls()==0&&generic[0].totalCalls()==0&&generic[1].totalCalls()==0);
 puts("Generic files policy: exact mapping/admission, preserving cohort, no provider I/O PASS");
}
int main(int argc,char** argv){assert(argc==3);directory=argv[1];scenario=argv[2];direct=scenario=="direct"||scenario=="reentrant"||scenario=="malformed-direct"||scenario=="unavailable"||scenario=="direct-policies";if(scenario=="policies"||scenario=="direct-policies"){policyTests(directory);return 0;}if(scenario=="malformed-direct")generic[0].api.close=nullptr;boot();R r({owner,health,delay,logLine});runtime=&r;if(!r.prepare(directory.c_str())){fprintf(stderr,"prepare: %s\n",r.error());assert(false);}
 if(scenario=="open-exit"||scenario=="failed-close"){assert(!r.run()&&r.retained()&&!r.residentResetSafe());invalid(saved);puts("Generic files retained custody and exit barrier PASS");fflush(stdout);_exit(0);}
 assert(r.run()&&!r.retained());if(saved.context)invalid(saved);puts("Generic files real Runtime: catalog, namespace mapping, owner/handle isolation, CRUD, lifecycle PASS");
}
