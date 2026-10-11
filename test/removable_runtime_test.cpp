// Real Runtime and provider lifecycle; host ELF mapping substitutes Xtensa.
#include "bootstrap/Runtime.h"
#include <RiscResidentShellV1.h>
#include "runtime/packages/InstalledAppAsyncDiscovery.h"
#include "runtime/storage/StorageWorker.h"
#include <RiscStorageAsyncAdapterV1.h>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_elf.h>
#include <private/esp_dlcache.h>
#include <dlfcn.h>
#include <cassert>
#include <fstream>
#include <vector>
#include <string>
#include <unistd.h>
#include <cstdlib>
static std::string mode,root;
static std::vector<uint8_t> payload;
static unsigned defaults,children,starts,stops,opens,closes,live,previousRuns;
static bool catalogMode(){return mode=="catalog"||mode=="catalog-resident"||mode=="catalog-authority";}
static unsigned baseCount(){return mode=="catalog-resident"?2:mode=="catalog-authority"?1:0;}
extern "C" bool removable_is_resident(){return mode=="catalog-resident";}
static size_t offset;
static bool present=true;
static RiscBoot::Runtime* runtime;
static const risc_installation_v1* installer;
extern "C" void removable_installation(const void*api){installer=static_cast<const risc_installation_v1*>(api);}
static unsigned privateWrites;
static RiscBoot::AppDataBackend privateData{nullptr,
 [](void*,uint32_t ns,const char*,uint32_t*n,uint64_t*r)->int32_t{assert(ns==(mode=="catalog-authority"?77u:1000u));*n=0;*r=privateWrites;return RISC_APP_DATA_NOT_FOUND;},
 [](void*,uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*)->int32_t{return RISC_APP_DATA_NOT_FOUND;},
 [](void*,uint32_t ns,const char*,uint64_t,const void*,uint32_t)->int32_t{assert(ns==(mode=="catalog-authority"?77u:1000u));++privateWrites;return RISC_APP_DATA_OK;},
 [](void*){return true;}};
extern "C" void removable_private_data(const risc_runtime_api_v1*rt){
 if(!catalogMode())return;
 risc_runtime_capability_v1 grant{};grant.struct_size=sizeof(grant);
 assert(rt->acquire("storage.app-data",1,0,&grant));
 const auto*api=static_cast<const risc_app_data_v1*>(grant.api);
 assert(api->replace(api->context,"settings",0,"x",1)==RISC_APP_DATA_OK);
 assert(rt->release(&grant));
}
static std::string launchName="child.elf";
static RiscStorage::Worker storageWorker;
static unsigned discoveryIo;
static FILE* manifestFile;
static DIR* appDirectory;
extern "C" const char* removable_launch_name(){return launchName.c_str();}
extern "C" const risc_storage_volume_api_v1* removable_volume();
static void write(const std::string& name,const std::string& value){std::ofstream(root+"/"+name)<<value;}
static void selectImage(const char* name){
  std::ifstream f(root+"/"+name,std::ios::binary);payload.assign(std::istreambuf_iterator<char>(f),{});assert(payload.size()>52);
}
extern "C" unsigned removable_event(unsigned e,unsigned value) {
  if(e==0){++defaults;
    if(defaults==1 && (mode=="discovery"||catalogMode())) {
      assert(runtime->appCount()==baseCount());
      const char* manifest=R"({"type":"application","id":"child","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"child.elf","entry":"app_main","requires":[]})";
      const char* policy=R"({"manifest":"child.json","grants":[],"image_source":{"volume_instance":9,"path":"Apps/child/child.elf"}})";
      // Failed admission cannot consume a slot or poison the subsequent app.
      std::string bad=policy;
      bad.replace(bad.find("Apps/child/child.elf"),20,"../child.elf");
      assert(!runtime->admitInstalledApp(bad.c_str(),manifest));
      bad=policy;bad.replace(bad.find("volume_instance\":9"),18,"volume_instance\":8");
      assert(!runtime->admitInstalledApp(bad.c_str(),manifest));
      bad=manifest;bad.replace(bad.find("\"requires\":[]"),13,"\"requires\":[{\"capability\":\"raw.memory\",\"api\":1}]");
      assert(!runtime->admitInstalledApp(policy,bad.c_str()));
      assert(runtime->appCount()==baseCount());
      // Add metadata to the real mock SD directory only AFTER prepare/first
      // default entry. No boot policy or firmware launcher names this package.
      (void)mkdir((root+"/Apps").c_str(),0700);
      (void)mkdir((root+"/Apps/child").c_str(),0700);
      write("Apps/child/manifest.json",manifest);
      std::vector<std::pair<std::string,std::string>> staged;
      RiscPackages::InstalledAppAsyncDiscovery scan;
      assert(scan.start(removable_volume(),[](void*ctx,const char* path,const char* json){
        static_cast<std::vector<std::pair<std::string,std::string>>*>(ctx)->emplace_back(path,json);return true;
      },&staged));
      unsigned steps=0;
      while(scan.state()!=RiscPackages::InstalledAppAsyncDiscovery::State::Complete){
        assert(scan.state()!=RiscPackages::InstalledAppAsyncDiscovery::State::Failed &&
               scan.state()!=RiscPackages::InstalledAppAsyncDiscovery::State::Retained && ++steps<1000);
        unsigned before=discoveryIo;scan.step();assert(discoveryIo==before);
        if(steps%3==0)storageWorker.workerStep();
        assert(runtime->appCount()==baseCount());
      }
      if(catalogMode()){
        if(mode=="catalog-authority"){
          char bytes[4096];uint32_t actual=99;assert(!runtime->appInventory(0,bytes,sizeof(bytes),&actual)&&actual==0);
          char denied[193];assert(installer->admit(installer->context,"Apps/other/manifest.json",staged[0].second.c_str(),denied,sizeof(denied))==RISC_INSTALL_DENIED);
        }
        assert(installer);risc_install_candidate_v1 item{staged[0].first.c_str(),staged[0].second.c_str()};risc_install_result_v1 result{};
        assert(installer->check(installer->context,&item,1,&result,true)==RISC_INSTALL_OK&&result.status==RISC_INSTALL_OK);
        assert(runtime->appCount()==baseCount());
        assert(installer->commit(installer->context,&item,1,&result,true)==RISC_INSTALL_OK&&result.status==RISC_INSTALL_OK);
        launchName=result.launch_path;assert(runtime->appCount()==baseCount()+1);
        assert(installer->commit(installer->context,&item,1,&result,true)==RISC_INSTALL_OK&&runtime->appCount()==baseCount()+1);
        assert(installer->forget(installer->context,launchName.c_str(),false)==RISC_INSTALL_OK);
        assert(installer->commit(installer->context,&item,1,&result,true)==RISC_INSTALL_OK);
        char admitted[193];
        for(unsigned cycle=0;cycle<160;++cycle){
          std::string id="churn"+std::to_string(cycle),path="Apps/"+id+"/manifest.json";
          std::string m=manifest;auto at=m.find("\"id\":\"child\"");m.replace(at,12,"\"id\":\""+id+"\"");
          assert(installer->admit(installer->context,path.c_str(),m.c_str(),admitted,sizeof(admitted))==RISC_INSTALL_OK);
          assert(installer->forget(installer->context,admitted,false)==RISC_INSTALL_OK);
          assert(runtime->appCount()<=baseCount()+2);
        }
        unsigned filled=0;
        for(;filled<100;++filled){
          std::string id="filled"+std::to_string(filled),path="Apps/"+id+"/manifest.json";
          std::string m=manifest;auto at=m.find("\"id\":\"child\"");m.replace(at,12,"\"id\":\""+id+"\"");
          int status=installer->admit(installer->context,path.c_str(),m.c_str(),admitted,sizeof(admitted));
          if(status==RISC_INSTALL_FULL)break;assert(status==RISC_INSTALL_OK);
        }
        assert(filled>20&&filled<100);
        std::string updated=manifest;updated.replace(updated.find("1.0.0"),5,"1.0.1");
        assert(installer->admit(installer->context,item.manifest_path,updated.c_str(),admitted,sizeof(admitted))==RISC_INSTALL_OK);
        std::string invalid=updated;invalid.replace(invalid.find("xtensa-esp32s3"),13,"bad-cpu");
        assert(installer->admit(installer->context,item.manifest_path,invalid.c_str(),admitted,sizeof(admitted))==RISC_INSTALL_INVALID);
        // Replacement can evict a full catalog transactionally, without a spare.
        item.manifest_json=updated.c_str();assert(installer->commit(installer->context,&item,1,&result,true)==RISC_INSTALL_OK);
        JsonDocument privateManifest;assert(!deserializeJson(privateManifest,updated));
        auto privateNeed=privateManifest["requires"].add<JsonObject>();privateNeed["capability"]="storage.app-data";privateNeed["api"]=1;
        std::string privateJson;serializeJson(privateManifest,privateJson);item.manifest_json=privateJson.c_str();uint32_t slot=1;
        assert(installer->namespace_capacity==128);
        int checked=installer->check(installer->context,&item,1,&result,true);assert(checked==RISC_INSTALL_OK&&result.status==(mode=="catalog-authority"?RISC_INSTALL_OK:RISC_INSTALL_DENIED));
        assert(installer->namespaced(installer->context,&item,&slot,1,&result,true,true)==RISC_INSTALL_OK&&result.status==RISC_INSTALL_OK);
        assert(installer->namespaced(installer->context,&item,&slot,1,&result,true,false)==RISC_INSTALL_OK&&result.status==RISC_INSTALL_OK);
        uint32_t changedSlot=2;
        assert(installer->namespaced(installer->context,&item,&changedSlot,1,&result,false,true)==RISC_INSTALL_OK&&result.status==RISC_INSTALL_DENIED);
        changedSlot=129;assert(installer->namespaced(installer->context,&item,&changedSlot,1,&result,false,true)==RISC_INSTALL_INVALID);
        assert(installer->forget(installer->context,launchName.c_str(),false)==RISC_INSTALL_OK);
        assert(installer->namespaced(installer->context,&item,&slot,1,&result,true,false)==RISC_INSTALL_OK&&result.status==RISC_INSTALL_OK);
        puts("Installed registry: 160 distinct install/remove cycles, update at capacity, failed-update rollback PASS");
      } else for(const auto& item:staged){const char*path=item.first.c_str();const char*json=item.second.c_str();
        std::string folder(path);folder.resize(folder.rfind('/')+1);
        JsonDocument m;assert(!deserializeJson(m,json));
        launchName=folder+m["file_name"].as<const char*>();
        JsonDocument p;p["manifest"]=path;p["grants"].to<JsonArray>();
        p["image_source"]["volume_instance"]=9;p["image_source"]["path"]=launchName;
        std::string encoded;serializeJson(p,encoded);
        assert(runtime->admitInstalledApp(encoded.c_str(),json));
      }
      assert(scan.admitted()==1 && !scan.rejected() && !manifestFile && !appDirectory);
      if(!catalogMode())assert(runtime->appCount()==baseCount()+1);
      assert(!runtime->admitInstalledApp(policy,manifest));
      if(!catalogMode())assert(runtime->appCount()==baseCount()+1);
    }
    if(defaults==2 && mode=="replacement")selectImage("revision2.elf");return defaults<3;}
  if(e==1){assert(!live && opens==closes);++children;assert(value==(mode=="replacement" && children==2?2u:1u));}
  if(e==6){++previousRuns;assert(value==1);}
  if(e==2)++starts;
  if(e==3){assert(!live);++stops;}
  return 0;
}
extern "C" void removable_resident_entry(const risc_runtime_api_v1*rt){
  risc_resident_client_v1 host{};host.struct_size=sizeof(host);assert(rt->resident_shell(&host));
  risc_resident_callbacks_v1 callbacks{1,sizeof(callbacks),nullptr,
    [](void*,const risc_resident_request_v1*,risc_resident_reply_v1*)->int32_t{return RISC_RESIDENT_OK;},nullptr,nullptr};
  assert(host.register_shell(host.invocation,&callbacks)==RISC_RESIDENT_OK);
  if(defaults==0){risc_resident_result_v1 result{};result.struct_size=sizeof(result);
    assert(host.run_foreground(host.invocation,"previous.elf",&result)==RISC_RESIDENT_OK);
    assert(previousRuns==1); // Inactive allocated foreground context remains.
  }
  if(removable_event(0,0)){risc_resident_result_v1 result{};result.struct_size=sizeof(result);
    assert(host.run_foreground(host.invocation,launchName.c_str(),&result)==RISC_RESIDENT_HANDOFF);
  }
}
extern "C" const risc_storage_volume_api_v1* removable_volume() {
  static risc_storage_volume_api_v1_async transport{};
  auto& ext=*reinterpret_cast<risc_storage_volume_api_v1_ext*>(&transport);auto& v=ext.base;
  v.api_version=1;v.struct_size=sizeof(transport);
  v.refresh=[](void*){return true;};v.ready=[](void*){return present;};
  v.dir_open=[](void*,const char* p)->uint32_t{++discoveryIo;appDirectory=opendir((root+p).c_str());return appDirectory?1:0;};
  v.dir_next=[](void*,uint32_t,risc_storage_dirent_v1*out){
    ++discoveryIo;while(auto*e=readdir(appDirectory)){if(e->d_name[0]=='.')continue;
      snprintf(out->name,sizeof(out->name),"%s",e->d_name);out->is_directory=e->d_type==DT_DIR;return true;}
    return false;
  };
  ext.handle_error=[](void*,uint32_t h,bool dir)->uint32_t{return dir?0:h==2&&manifestFile&&ferror(manifestFile)?1:0;};
  ext.dir_close_checked=[](void*,uint32_t){++discoveryIo;bool ok=closedir(appDirectory)==0;if(ok)appDirectory=nullptr;return ok;};
  v.file_open_read=[](void*,const char* p,uint64_t* n)->uint32_t {
    if(!strcmp(p,"/Apps/child/manifest.json")){
      ++discoveryIo;manifestFile=fopen((root+p).c_str(),"rb");if(!manifestFile)return 0;
      assert(!fseek(manifestFile,0,SEEK_END));*n=ftell(manifestFile);rewind(manifestFile);return 2;
    }
    assert(!strcmp(p,"/Apps/child/child.elf"));
    if(mode=="missing" || mode=="busy")return 0;
    ++opens;live=1;offset=0;*n=payload.size();return 1;
  };
  v.file_read=[](void*,uint32_t h,void* out,size_t n)->size_t {
    if(h==2){++discoveryIo;return fread(out,1,n,manifestFile);}
    assert(live && n<=4096);if(mode=="removed" && offset>1000)return 0;
    n=std::min(size_t(333),std::min(n,payload.size()-offset));memcpy(out,payload.data()+offset,n);offset+=n;return n;
  };
  v.file_close=[](void*,uint32_t h,bool){if(h==2){++discoveryIo;bool ok=fclose(manifestFile)==0;if(ok)manifestFile=nullptr;return ok;}assert(live);++closes;if(mode=="close-retained")return false;live=0;return true;};
  static risc_storage_worker_v1 workerApi{};static risc_storage_async_adapter_v1 bridge{};
  static bool initialized=false;
  if(!initialized){initialized=true;
   auto&fs=transport.fs;fs.state.state_tag=RISC_STORAGE_STATE_TAG;fs.state.state_version=1;
   fs.state.observe=[](void*)->int32_t{return present?RISC_STORAGE_STATE_READY:RISC_STORAGE_STATE_UNAVAILABLE;};
   fs.fs_tag=RISC_STORAGE_FS_TAG;fs.fs_version=1;
   fs.dir_tell=[](void*,uint32_t,uint64_t*){return false;};fs.dir_seek=[](void*,uint32_t,uint64_t){return false;};
   fs.metadata=[](void*,const char*,risc_storage_metadata_v1*)->int32_t{++discoveryIo;return RISC_STORAGE_FS_NOT_FOUND;};
   fs.file_truncate=[](void*,uint32_t,uint64_t){return false;};fs.replace_file=[](void*,const char*,const char*){return false;};fs.recover_replace=[](void*,const char*){return false;};
   workerApi.submit=[](void*,const void*q,uint32_t n,uint64_t*t){return storageWorker.submit(q,n,t);};
   workerApi.collect=[](void*,uint64_t t,void*q,uint32_t n,uint32_t*z,int32_t*r){return storageWorker.collect(t,q,n,z,r);};
   workerApi.cancel=[](void*,uint64_t t){return storageWorker.cancel(t);};
   bridge.worker=&workerApi;bridge.volume=&v;bridge.ext=&ext;bridge.fs=&fs;
   assert(storageWorker.bind(risc_storage_async_execute,&bridge)==0);
   transport.async_tag=RISC_STORAGE_ASYNC_TAG;transport.async_version=RISC_STORAGE_ASYNC_VERSION;
   transport.begin=[](void*,uint64_t*t){return risc_storage_async_begin(&bridge,t);};
   transport.submit=[](void*,uint64_t token,const risc_storage_async_request_v1*q,uint64_t*t){return risc_storage_async_submit(&bridge,token,q,t);};
   transport.collect=[](void*,uint64_t token,uint64_t t,risc_storage_async_result_v1*r){return risc_storage_async_collect(&bridge,token,t,r);};
   transport.end=[](void*,uint64_t token){return risc_storage_async_end(&bridge,token);};
  }
  return &v;
}
extern "C" void* esp_elf_malloc(uint32_t n,bool){return malloc(n);}
extern "C" void esp_elf_free(void* p){free(p);}
extern "C" bool esp_elf_validate_file(const uint8_t* p,size_t n){
#if defined(__APPLE__)
  return n>52 && p[0]==0xcf && p[1]==0xfa && p[2]==0xed && p[3]==0xfe;
#else
  return n>52 && !memcmp(p,"\177ELF",4);
#endif
}
extern "C" void* esp_dlopen_reader_instance(const char* name,esp_dl_image_reader reader,void* context){
  assert(!live);elf_file_t file{};assert(reader(context,name,&file)==0);
  char temporary[]="/tmp/riscrte-sd-test-XXXXXX";int fd=mkstemp(temporary);assert(fd>=0);
  FILE* out=fdopen(fd,"wb");assert(out);assert(fwrite(file.payload,1,file.size,out)==file.size);assert(!fclose(out));
  esp_elf_free(file.payload);void* module=dlopen(temporary,RTLD_NOW|RTLD_LOCAL);unlink(temporary);assert(module);return module;
}
int main(int argc,char** argv) {
  assert(argc==3);root=argv[1];mode=argv[2];selectImage("revision1.elf");
  if(mode=="malformed")payload[0]=0;
  if(mode=="absent")present=false;
  write("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":9,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[5],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
  write("sd.json",R"({"type":"driver","id":"sd-test","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"sd.elf","requires":[{"capability":"hardware.device","api":1}],"provides":[{"capability":"storage.volume","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
  write("child.json",R"({"type":"application","id":"child","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"child.elf","entry":"app_main","requires":[]})");
  std::string boot=R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"sd.json","instance_id":9}],"app_capabilities":[{"manifest":"child.json","grants":[],"image_source":{"volume_instance":9,"path":"Apps/child/child.elf"}}]})";
  if(mode=="invalid-path")boot.replace(boot.find("Apps/child/child.elf"),20,"../child.elf");
  if(mode=="default-external")boot.replace(boot.find("default.elf"),11,"child.elf");
  if(mode=="discovery"||catalogMode())boot=R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"sd.json","instance_id":9}],"app_capabilities":[]})";
  if(catalogMode()){
    JsonDocument doc;assert(!deserializeJson(doc,boot));doc["provider_activation"]="eager";
    doc["installation"]["volume_instance"]=9;auto grant=doc["installation"]["grants"].to<JsonArray>().add<JsonObject>();grant["capability"]="storage.app-data";grant["api"]=1;grant["instance_id"]=0;
    doc["installation"]["namespace_range"]["first"]=1000;doc["installation"]["namespace_range"]["count"]=128;boot.clear();serializeJson(doc,boot);
    std::ifstream source(root+"/sd.json");std::string raw((std::istreambuf_iterator<char>(source)),{});JsonDocument sd;assert(!deserializeJson(sd,raw));
    auto req=sd["requires"].add<JsonObject>();req["capability"]=RISC_INSTALLATION_CAPABILITY;req["api"]=1;raw.clear();serializeJson(sd,raw);write("sd.json",raw);
  }
  if(mode=="catalog-resident"){
    JsonDocument doc;assert(!deserializeJson(doc,boot));
    auto shell=doc["resident_shell"].to<JsonObject>();shell["api"]=1;shell["host"]="default.elf";shell["foreground"].to<JsonArray>().add("previous.elf");
    for(const char*name:{"default","previous"}){
      JsonDocument m;m["type"]="application";m["id"]=name;m["version"]="1.0.0";m["architecture"]="xtensa-esp32s3";m["file_name"]=std::string(name)+".elf";m["entry"]="app_main";m["requires"].to<JsonArray>();
      std::string raw;serializeJson(m,raw);write(std::string(name)+".json",raw);
      auto policy=doc["app_capabilities"].add<JsonObject>();policy["manifest"]=std::string(name)+".json";policy["grants"].to<JsonArray>();
    }
    boot.clear();serializeJson(doc,boot);
  }
  if(mode=="catalog-authority"){
    JsonDocument doc,m;assert(!deserializeJson(doc,boot));
    std::ifstream f(root+"/child.json");std::string text((std::istreambuf_iterator<char>(f)),{});assert(!deserializeJson(m,text));
    auto req=m["requires"].add<JsonObject>();req["capability"]="storage.app-data";req["api"]=1;
    text.clear();serializeJson(m,text);write("child.json",text);
    auto p=doc["app_capabilities"].add<JsonObject>();p["manifest"]="child.json";p["catalog_authority"]=true;
    p["image_source"]["volume_instance"]=9;p["image_source"]["path"]="Apps/child/child.elf";
    auto g=p["grants"].add<JsonObject>();g["capability"]="storage.app-data";g["api"]=1;g["instance_id"]=77;
    boot.clear();serializeJson(doc,boot);
  }
  write("boot.json",boot);
  RiscBoot::Port port{[](){return true;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};port.appData=&privateData;runtime=new RiscBoot::Runtime(port);
  if(mode=="invalid-path" || mode=="default-external") {assert(!runtime->prepare(root.c_str()));assert(!starts);delete runtime;return 0;}
  if(catalogMode()){
    // Boot cannot reserve any namespace already owned by an app, even before
    // a dynamic catalog has loaded. No native I/O is needed to reject overlap.
    JsonDocument conflicting;assert(!deserializeJson(conflicting,boot));
    write("namespace-owner.json",R"({"type":"application","id":"namespace-owner","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"owner.elf","entry":"app_main","requires":[{"capability":"storage.app-data","api":1}]})");
    auto owner=conflicting["app_capabilities"].add<JsonObject>();owner["manifest"]="namespace-owner.json";
    auto grant=owner["grants"].to<JsonArray>().add<JsonObject>();grant["capability"]="storage.app-data";grant["api"]=1;grant["instance_id"]=1000;
    std::string invalid;serializeJson(conflicting,invalid);write("boot.json",invalid);
    {RiscBoot::Runtime overlapping(port);assert(!overlapping.prepare(root.c_str()));}
    write("boot.json",boot);
  }
  if(!runtime->prepare(root.c_str())){fprintf(stderr,"prepare: %s\n",runtime->error());abort();}
  if(mode=="discovery")assert(runtime->reserveInstalledApps(4));
  unsigned inspected=0;
  assert(runtime->inspectImages([](void* c,const char* p,bool){assert(!strstr(p,"child.elf"));++*static_cast<unsigned*>(c);return true;},&inspected));assert(inspected==(mode=="catalog-resident"?3u:2u));
  const bool retained=mode=="close-retained";
  assert(runtime->run()!=retained);assert(runtime->retained()==retained);
  if(retained){assert(defaults==1 && !children && live && !stops);}
  else {assert(defaults==3 && !live && starts==stops);assert(children==((mode=="normal" || mode=="replacement" || (mode=="discovery"||catalogMode()))?2u:0u));delete runtime;}
  if(catalogMode())assert(privateWrites==2);
  printf("SD Runtime mode=%s defaults=%u children=%u opens=%u closes=%u provider_starts=%u stops=%u PASS\n",mode.c_str(),defaults,children,opens,closes,starts,stops);
}
