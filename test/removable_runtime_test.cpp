// Real Runtime and provider lifecycle; host ELF mapping substitutes Xtensa.
#include "bootstrap/Runtime.h"
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
static unsigned defaults,children,starts,stops,opens,closes,live;
static size_t offset;
static bool present=true;
static void write(const std::string& name,const std::string& value){std::ofstream(root+"/"+name)<<value;}
static void selectImage(const char* name){
  std::ifstream f(root+"/"+name,std::ios::binary);payload.assign(std::istreambuf_iterator<char>(f),{});assert(payload.size()>52);
}
extern "C" unsigned removable_event(unsigned e,unsigned value) {
  if(e==0){++defaults;if(defaults==2 && mode=="replacement")selectImage("revision2.elf");return defaults<3;}
  if(e==1){assert(!live && opens==closes);++children;assert(value==(mode=="replacement" && children==2?2u:1u));}
  if(e==2)++starts;
  if(e==3){assert(!live);++stops;}
  return 0;
}
extern "C" const risc_storage_volume_api_v1* removable_volume() {
  static risc_storage_volume_api_v1 v{};
  v.api_version=1;v.struct_size=sizeof(v);
  v.refresh=[](void*){return true;};v.ready=[](void*){return present;};
  v.file_open_read=[](void*,const char* p,uint64_t* n)->uint32_t {
    assert(!strcmp(p,"/Apps/child/child.elf"));
    if(mode=="missing" || mode=="busy")return 0;
    ++opens;live=1;offset=0;*n=payload.size();return 1;
  };
  v.file_read=[](void*,uint32_t,void* out,size_t n)->size_t {
    assert(live && n<=4096);if(mode=="removed" && offset>1000)return 0;
    n=std::min(size_t(333),std::min(n,payload.size()-offset));memcpy(out,payload.data()+offset,n);offset+=n;return n;
  };
  v.file_close=[](void*,uint32_t,bool){assert(live);++closes;if(mode=="close-retained")return false;live=0;return true;};
  return &v;
}
extern "C" void* esp_elf_malloc(uint32_t n,bool){return malloc(n);}
extern "C" void esp_elf_free(void* p){free(p);}
extern "C" bool esp_elf_validate_file(const uint8_t* p,size_t n){return n>52 && !memcmp(p,"\177ELF",4);}
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
  write("boot.json",boot);
  auto* runtime=new RiscBoot::Runtime({[](){return true;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}});
  if(mode=="invalid-path" || mode=="default-external") {assert(!runtime->prepare(root.c_str()));assert(!starts);delete runtime;return 0;}
  if(!runtime->prepare(root.c_str())){fprintf(stderr,"prepare: %s\n",runtime->error());abort();}
  unsigned inspected=0;
  assert(runtime->inspectImages([](void* c,const char* p,bool){assert(!strstr(p,"child.elf"));++*static_cast<unsigned*>(c);return true;},&inspected));assert(inspected==2);
  const bool retained=mode=="close-retained";
  assert(runtime->run()!=retained);assert(runtime->retained()==retained);
  if(retained){assert(defaults==1 && !children && live && !stops);}
  else {assert(defaults==3 && !live && starts==stops);assert(children==((mode=="normal" || mode=="replacement")?2u:0u));delete runtime;}
  printf("SD Runtime mode=%s defaults=%u children=%u opens=%u closes=%u provider_starts=%u stops=%u PASS\n",mode.c_str(),defaults,children,opens,closes,starts,stops);
}
