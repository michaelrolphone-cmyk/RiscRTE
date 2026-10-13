#include "bootstrap/Runtime.h"
#include <cassert>
#include <fstream>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
using RiscBoot::Runtime;
static std::string root,mode,longRoute;
static bool owned=true,safe=true,reading=false,journalFault=false;
static unsigned nativeIo=0,entries=0,reentry=0;
static Runtime* runtime;
static const risc_runtime_api_v1* runtimeApi;
static const risc_installation_v1* installer;
static risc_app_catalog_v1 saved{},current{};
static risc_runtime_capability_v1 grant{sizeof(grant)};
static RiscBoot::KeyValueBackend kv{nullptr,
 [](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{assert(!reading);return RISC_KEY_VALUE_NOT_FOUND;},
 [](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{assert(false);return RISC_KEY_VALUE_IO;},2048,
 [](void*,uint32_t,void*,uint32_t,uint32_t*)->int32_t{assert(!reading);return RISC_KEY_VALUE_NOT_FOUND;},
 [](void*,uint32_t,const void*,uint32_t)->int32_t{assert(!reading);return journalFault?RISC_KEY_VALUE_IO:RISC_KEY_VALUE_OK;},
 [](void*,uint32_t,bool* exists)->int32_t{assert(!reading);*exists=false;return RISC_KEY_VALUE_OK;}};
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" FILE* __wrap_fopen(const char* p,const char* m){if(reading)++nativeIo;return __real_fopen(p,m);}
extern "C" DIR* __real_opendir(const char*);
extern "C" DIR* __wrap_opendir(const char* p){if(reading)++nativeIo;return __real_opendir(p);}
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
extern "C" size_t __wrap_fread(void* p,size_t n,size_t count,FILE* f){if(reading)++nativeIo;return __real_fread(p,n,count,f);}
extern "C" void app_catalog_test_installer(const void* api){installer=static_cast<const risc_installation_v1*>(api);}
static bool owner(){return owned;}
static bool health(risc_runtime_health_v1*){return true;}
static bool log(const char*){return true;}
static void delay(uint32_t){}
static bool storageSafe(){
  if(reentry){const unsigned action=reentry;reentry=0;
    if(action==1){risc_app_catalog_snapshot_v1 nested{sizeof(nested)};assert(current.snapshot(current.context,&nested)==RISC_APP_CATALOG_CONTEXT);}
    else if(action==2){assert(runtimeApi->release(&grant));}
    else if(action==3)owned=false;
    else if(action==4){risc_runtime_capability_v1 nested{sizeof(nested)};assert(!runtimeApi->acquire("app.catalog",1,0,&nested));assert(!runtimeApi->acquire("storage.key-value",1,7,&nested));}
  }
  return safe;
}
static risc_app_catalog_snapshot_v1 snapshot(const risc_app_catalog_v1& api){
  reading=true;risc_app_catalog_snapshot_v1 out{sizeof(out)};
  assert(api.snapshot(api.context,&out)==RISC_APP_CATALOG_OK);reading=false;
  assert(out.struct_size==sizeof(out)&&out.generation);assert(!nativeIo);return out;
}
static risc_app_catalog_entry_v1 get(const risc_app_catalog_v1& api,uint64_t gen,uint32_t index,int32_t status=0){
  reading=true;risc_app_catalog_entry_v1 out{sizeof(out)};
  const int32_t rc=api.get(api.context,gen,index,&out);if(rc!=status)fprintf(stderr,"get generation=%llu index=%u expected=%d actual=%d\n",static_cast<unsigned long long>(gen),index,status,rc);assert(rc==status);reading=false;assert(!nativeIo);return out;
}
static void acquire(){grant={sizeof(grant)};assert(runtimeApi->acquire(RISC_APP_CATALOG_CAPABILITY,1,0,&grant));current=*static_cast<const risc_app_catalog_v1*>(grant.api);}
static std::string manifest(const char* id,const char* file,const std::string& extra="",bool catalog=false){
  return std::string("{\"type\":\"application\",\"id\":\"")+id+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""+file+"\",\"entry\":\"app_main\",\"requires\":"+(catalog?"[{\"capability\":\"app.catalog\",\"api\":1}]":"[]")+extra+"}";
}
static void write(const std::string& name,const std::string& value){std::ofstream f(root+"/"+name);assert(f);f<<value;}
extern "C" void app_catalog_test_entry(const risc_runtime_api_v1* rt,int child){
  assert(rt);runtimeApi=rt;++entries;
  risc_app_catalog_snapshot_v1 probe{sizeof(probe)};
  if(child){assert(saved.snapshot(saved.context,&probe)==RISC_APP_CATALOG_CONTEXT);assert(!rt->acquire("app.catalog",1,0,&grant));return;}
  if(entries>1){assert(saved.snapshot(saved.context,&probe)==RISC_APP_CATALOG_CONTEXT);acquire();auto s=snapshot(current);assert(s.count==(mode=="long_path"?1u:23u));saved=current;return;}
  assert(!rt->acquire("app.catalog",2,0,&grant));assert(!rt->acquire("app.catalog",1,9,&grant));
  assert(!rt->acquire("platform.installation",1,0,&grant));assert(!rt->acquire("platform.bank-store",1,0,&grant));
  if(mode=="unauthorized"){assert(!rt->acquire("app.catalog",1,0,&grant));return;}
  if(mode=="acquire_retained" || mode=="acquire_lost_owner") {
    if(mode=="acquire_retained")safe=false;else reentry=3;
    assert(!rt->acquire("app.catalog",1,0,&grant));
    risc_runtime_health_v1 h{sizeof(h)};assert(!rt->health(&h)&&runtime->retained());
    owned=safe=true;assert(!rt->health(&h));assert(!rt->release(&grant));return;
  }
  reentry=4;acquire();assert(current.api_version==1&&current.struct_size==sizeof(current)&&current.context&&current.snapshot&&current.get);
  saved=current;
  if(mode=="retained"){
    safe=false;assert(current.snapshot(current.context,&probe)==RISC_APP_CATALOG_RETAINED);
    safe=true;assert(current.snapshot(current.context,&probe)==RISC_APP_CATALOG_RETAINED);assert(!rt->release(&grant));return;
  }
  auto s=snapshot(current);
  if(mode=="long_path") {assert(s.count==1);auto entry=get(current,s.generation,0);assert(entry.file_name==longRoute&&!strcmp(entry.id,"long-route"));assert(entry.flags==RISC_APP_CATALOG_AVAILABLE);saved=current;assert(rt->request_launch(entry.file_name));return;}
  if(mode=="normalized_metadata") {assert(s.count==1);auto entry=get(current,s.generation,0);assert(!strcmp(entry.display_name,"legacy-label")&&!entry.icon[0]&&!entry.min_firmware_version[0]);return;}
  if(mode=="empty"){assert(s.count==0);get(current,s.generation,0,RISC_APP_CATALOG_NOT_FOUND);return;}
  assert(runtime->appInventoryCount()==25&&s.count==23);
  for(uint32_t i=0;i<s.count;++i){auto e=get(current,s.generation,i);assert(e.flags==RISC_APP_CATALOG_AVAILABLE);
    const std::string expected=i==0?"child.elf":"app"+std::to_string(i)+".elf";assert(e.file_name==expected);assert(e.id=="app"+std::to_string(i));
    assert(strcmp(e.file_name,"default.elf")&&strcmp(e.file_name,"launcher.elf"));
    if(i==1)assert(!strcmp(e.display_name,"Installed title")&&!strcmp(e.icon,"\xe2\x98\x80")&&!strcmp(e.min_firmware_version,"0.2.0"));
    else assert(e.min_firmware_version[0]==0);
  }
  assert(current.snapshot(current.context,nullptr)==RISC_APP_CATALOG_INVALID);
  probe.struct_size=sizeof(probe)-1;assert(current.snapshot(current.context,&probe)==RISC_APP_CATALOG_INVALID);probe.struct_size=sizeof(probe);
  risc_app_catalog_entry_v1 bad{sizeof(bad)-1};assert(current.get(current.context,s.generation,0,&bad)==RISC_APP_CATALOG_INVALID);
  get(current,s.generation,23,RISC_APP_CATALOG_NOT_FOUND);get(current,0,0,RISC_APP_CATALOG_INVALID);get(current,s.generation+1,0,RISC_APP_CATALOG_STALE);
  assert(current.snapshot(reinterpret_cast<void*>(UINTPTR_MAX),&probe)==RISC_APP_CATALOG_CONTEXT);
  owned=false;assert(current.snapshot(current.context,&probe)==RISC_APP_CATALOG_CONTEXT);owned=true;
  // Snapshot uses the admitted copy after original metadata files disappear.
  assert(!unlink((root+"/app1.json").c_str()));auto unchanged=snapshot(current);assert(unchanged.generation==s.generation);
  reentry=1;assert(snapshot(current).generation==s.generation);
  reentry=3;probe.count=12345;assert(current.snapshot(current.context,&probe)==RISC_APP_CATALOG_CONTEXT&&probe.count==12345);owned=true;
  reentry=2;probe.count=12345;assert(current.snapshot(current.context,&probe)==RISC_APP_CATALOG_CONTEXT&&probe.count==12345);
  assert(saved.snapshot(saved.context,&probe)==RISC_APP_CATALOG_CONTEXT);acquire();assert(saved.snapshot(saved.context,&probe)==RISC_APP_CATALOG_CONTEXT);
  risc_runtime_capability_v1 oldGrant=grant;auto old=current;assert(rt->release(&grant));acquire();assert(!rt->release(&oldGrant));assert(old.snapshot(old.context,&probe)==RISC_APP_CATALOG_CONTEXT);
  assert(installer);char launch[193]{};
  std::string app=manifest("new-app","native.elf",",\"display_name\":\"New app\",\"icon\":\"A\"");
  const auto before=snapshot(current);
  assert(installer->admit(installer->context,"Apps/new-app/manifest.json",app.c_str(),launch,sizeof(launch))==RISC_INSTALL_OK);
  assert(!strcmp(launch,"Apps/new-app/native.elf"));get(current,before.generation,0,RISC_APP_CATALOG_STALE);
  auto admitted=snapshot(current);assert(admitted.count==24&&admitted.generation>before.generation);
  auto e=get(current,admitted.generation,23);assert(!strcmp(e.file_name,launch)&&!strcmp(e.display_name,"New app"));
  for(const std::string& extra:{std::string(",\"icon\":123"),std::string(",\"icon\":\"")+std::string(24,'x')+"\""}) {
    auto malformed=manifest("new-app","native.elf",extra);assert(installer->admit(installer->context,"Apps/new-app/manifest.json",malformed.c_str(),launch,sizeof(launch))==RISC_INSTALL_INVALID);
    assert(snapshot(current).generation==admitted.generation);
  }
  app=manifest("new-app","native.elf",",\"display_name\":\"Renamed\"");
  assert(installer->admit(installer->context,"Apps/new-app/manifest.json",app.c_str(),launch,sizeof(launch))==RISC_INSTALL_OK);
  get(current,admitted.generation,23,RISC_APP_CATALOG_STALE);admitted=snapshot(current);assert(admitted.count==24);
  assert(!strcmp(get(current,admitted.generation,23).display_name,"Renamed"));
  risc_install_candidate_v1 candidate{"Apps/new-app/manifest.json",app.c_str()};risc_install_result_v1 result{};
  assert(installer->check(installer->context,&candidate,1,&result,false)==0);assert(snapshot(current).count==24);
  auto preForget=snapshot(current);assert(installer->forget(installer->context,launch,true)==0);assert(snapshot(current).generation==preForget.generation);
  assert(installer->forget(installer->context,launch,false)==0);get(current,preForget.generation,0,RISC_APP_CATALOG_STALE);auto removed=snapshot(current);assert(removed.count==23);
  app=manifest("new-app","native.elf",",\"launcher_visible\":false");assert(installer->admit(installer->context,"Apps/new-app/manifest.json",app.c_str(),launch,sizeof(launch))==0);
  assert(snapshot(current).count==23&&runtime->appInventoryCount()==26);
  // Settings recovery changes launch availability without media probing in
  // catalog callbacks. Both directions invalidate a prior publication.
  std::string settings=manifest("settings-app","settings.elf");
  settings.replace(settings.find("\"requires\":[]"),13,"\"requires\":[{\"capability\":\"storage.key-value\",\"api\":1}]");
  risc_install_candidate_v1 settingCandidate{"Apps/settings-app/manifest.json",settings.c_str()};uint32_t slot=1;
  assert(installer->namespaced(installer->context,&settingCandidate,&slot,1,&result,false,false)==RISC_INSTALL_OK&&result.status==0);
  auto cold=snapshot(current);assert(cold.count==24);assert(get(current,cold.generation,23).flags==0);
  assert(installer->settings_restore(installer->context,1,"settings-app",nullptr,0,true)==RISC_KEY_VALUE_OK);
  get(current,cold.generation,23,RISC_APP_CATALOG_STALE);auto ready=snapshot(current);
  assert(get(current,ready.generation,23).flags==RISC_APP_CATALOG_AVAILABLE);
  unsigned char settingsBytes[RiscStorage::SdFirstKeyValue::Bytes];uint32_t settingsSize=0;uint64_t settingsGeneration=0;
  assert(installer->settings_snapshot(installer->context,1,settingsBytes,sizeof(settingsBytes),&settingsSize,&settingsGeneration)==0);
  journalFault=true;assert(installer->settings_ack(installer->context,1,settingsGeneration)==RISC_KEY_VALUE_IO);journalFault=false;
  get(current,ready.generation,23,RISC_APP_CATALOG_STALE);auto uncertain=snapshot(current);
  assert(get(current,uncertain.generation,23).flags==0);
  assert(installer->forget(installer->context,"Apps/settings-app/settings.elf",false)==0);
  // A real child invocation cannot reuse the host grant or acquire undeclared
  // catalog access; the default returns with a fresh context after child exit.
  saved=current;assert(rt->request_launch("child.elf"));
}
int main(int argc,char** argv){
  assert(argc==3);root=argv[1];mode=argv[2];
  write("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":9,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[5],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
  write("sd.json",R"({"type":"driver","id":"catalog-volume","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"sd.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.installation","api":1}],"provides":[{"capability":"storage.volume","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
  bool declared=mode!="unauthorized";
  auto host=manifest("default","default.elf",",\"launcher_visible\":false",declared);
  if(declared)host.replace(host.find("\"requires\":[")+12,0,"{\"capability\":\"storage.key-value\",\"api\":1},");
  write("host.json",host);
  std::string boot=R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"sd.json","instance_id":9}],"installation":{"volume_instance":9,"grants":[{"capability":"storage.key-value","api":1,"instance_id":0}],"namespace_range":{"first":1000,"count":1},"settings_sd":true},"app_capabilities":[{"manifest":"host.json","grants":)";
  boot+=declared?"[{\"capability\":\"app.catalog\",\"api\":1,\"instance_id\":0},{\"capability\":\"storage.key-value\",\"api\":1,\"instance_id\":7}]":"[]";boot+="}";
  if(mode=="normal") {
    write("launcher.json",manifest("launcher","launcher.elf",",\"launcher_visible\":false"));boot+=",{\"manifest\":\"launcher.json\",\"grants\":[]}";
    for(unsigned i=0;i<23;++i){std::string name="app"+std::to_string(i),file=i==0?"child.elf":name+".elf";
      write(name+".json",manifest(name.c_str(),file.c_str(),i==1?",\"display_name\":\"Installed title\",\"icon\":\"\xe2\x98\x80\",\"min_firmware_version\":\"0.2.0\"":""));
      boot+=",{\"manifest\":\""+name+".json\",\"grants\":[]}";
    }
  }
  if(mode=="long_path") {
    const std::string folder(60,'a'),file=std::string(120,'b')+".elf";longRoute=folder+"/"+file;
    assert(!mkdir((root+"/"+folder).c_str(),0700));
    write(folder+"/manifest.json",manifest("long-route",file.c_str()));
    assert(!link((root+"/child.elf").c_str(),(root+"/"+longRoute).c_str()));
    boot+=",{\"manifest\":\""+folder+"/manifest.json\",\"grants\":[]}";
  }
  if(mode=="normalized_metadata") {
    write("legacy.json",manifest("legacy-label","legacy.elf",",\"display_name\":\""+std::string(120,'x')+"\",\"icon\":\"\\n\",\"min_firmware_version\":null,\"launcher_visible\":\"false\""));
    boot+=",{\"manifest\":\"legacy.json\",\"grants\":[]}";
  }
  boot+="]}";write("boot.json",boot);
  RiscBoot::Port port{owner,health,delay,log};port.providerStorageSafe=storageSafe;port.keyValue=&kv;
  // Native metadata read failure and malformed metadata are denied before any
  // default entry; they never create a fabricated successful empty catalog.
  if(mode=="normal") {
    for(const std::string& extra:{std::string(",\"launcher_visible\":0"),std::string(",\"display_name\":\"")+std::string(96,'x')+"\"",std::string(",\"min_firmware_version\":\"")+std::string(32,'x')+"\"",std::string(",\"display_name\":\"line\\nfeed\"")}) {
      auto optional=manifest("default","default.elf",extra,true);optional.replace(optional.find("\"requires\":[")+12,0,"{\"capability\":\"storage.key-value\",\"api\":1},");write("host.json",optional);Runtime accepted(port);assert(accepted.prepare(root.c_str()));assert(!entries);
    }
    assert(!unlink((root+"/host.json").c_str()));{Runtime rejected(port);assert(!rejected.prepare(root.c_str()));}
    write("host.json",host);
    auto wrong=boot;const auto at=wrong.find("\"instance_id\":0",wrong.find("\"app.catalog\""));wrong.replace(at,15,"\"instance_id\":9");write("boot.json",wrong);{Runtime rejected(port);assert(!rejected.prepare(root.c_str()));}write("boot.json",boot);
  }
  runtime=new Runtime(port);if(!runtime->prepare(root.c_str())){fprintf(stderr,"prepare: %s\n",runtime->error());abort();}
  const bool expectRetained=mode=="retained"||mode=="acquire_retained"||mode=="acquire_lost_owner";
  const bool ok=runtime->run();assert(ok==!expectRetained);assert(runtime->retained()==expectRetained);
  if(mode=="normal"||mode=="long_path")assert(entries==3);else assert(entries==1);
  risc_app_catalog_snapshot_v1 after{sizeof(after)};if(saved.snapshot)assert(saved.snapshot(saved.context,&after)==RISC_APP_CATALOG_CONTEXT);
  printf("app.catalog %s: real native grant, admitted inventory, fencing and zero catalog I/O PASS\n",mode.c_str());fflush(stdout);
  if(expectRetained)_Exit(0); // Contract: retained Runtime and mapped image stay alive.
  delete runtime;
}
