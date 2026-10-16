#include "bootstrap/Runtime.h"
#include "RuntimeFileFixture.h"
#include <cassert>
#include <fstream>
#include <string>

extern "C" const char risc_platform_capacity[];
static bool owned=true;
static unsigned tables=0,ioCalls=0;
static bool owner(){return owned;}
static const uint32_t table[2]={1,sizeof(table)};
static const risc_realtime_control_api_v1 realtime{1,sizeof(realtime),nullptr,
  [](void*,risc_realtime_snapshot_v1*)->int32_t{++ioCalls;return RISC_REALTIME_IO;},
  [](void*,int64_t,uint32_t)->int32_t{++ioCalls;return RISC_REALTIME_IO;}};
static bool bind(RiscBoot::Runtime& runtime){
  for(unsigned i=0;i<tables;++i)
    if(!runtime.registerPlatform("platform.capacity-test",1,RiscBoot::Runtime::Scope::Device,i+1,table))return false;
  return runtime.registerRealtime(&realtime);
}
static RiscBoot::Port port(){
  return {owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){++ioCalls;},[](const char*){return true;}};
}
static void file(const std::string& root,const char* name,const char* text){
  std::ofstream(root+"/"+name)<<text;
}
int main(int argc,char** argv){
  assert(argc==2);
  assert(!strcmp(risc_platform_capacity,RISC_PLATFORM_CAPACITY_MARKER));
  {
    RiscBoot::Runtime runtime(port());
    owned=false;assert(!runtime.registerPlatform("platform.capacity-test",1,RiscBoot::Runtime::Scope::Device,1,table));owned=true;
    assert(runtime.registerPlatform("platform.capacity-test",1,RiscBoot::Runtime::Scope::Device,1,table));
    assert(!runtime.registerPlatform("platform.capacity-test",1,RiscBoot::Runtime::Scope::Device,1,table));
    for(unsigned i=1;i<RISC_PLATFORM_CAPACITY;++i)
      assert(runtime.registerPlatform("platform.capacity-test",1,RiscBoot::Runtime::Scope::Device,i+1,table));
    assert(!runtime.registerPlatform("platform.capacity-test",1,RiscBoot::Runtime::Scope::Device,RISC_PLATFORM_CAPACITY+1,table));
  }
  const std::string root=argv[1];
  file(root,"board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"capacity-test","revision":"unspecified","buses":[],"devices":[]})");
  file(root,"volume.json",R"({"type":"driver","id":"test-volume","version":"1.0.0","architecture":"xtensa-esp32s3","driver_abi":2,"file_name":"volume.elf","requires":[{"capability":"platform.realtime","api":1}],"provides":[{"capability":"storage.volume","api":1}]})");
  file(root,"app.json",R"({"type":"application","id":"capacity-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})");
  file(root,"boot.json",R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"volume.json"}],"app_capabilities":[{"manifest":"app.json","grants":[]}],"installation":{"volume_instance":0,"internal_volume_instance":0,"grants":[]}})");
  for(unsigned count:{unsigned(RISC_PLATFORM_CAPACITY-2),unsigned(RISC_PLATFORM_CAPACITY-1),unsigned(RISC_PLATFORM_CAPACITY+1),32u}){
    RiscTest::FileSources sources({root});
    auto selected=port();selected.bindPlatforms=bind;selected.files=sources.sources();
    RiscBoot::Runtime runtime(selected);tables=count;
    const bool expected=count+2<=RISC_PLATFORM_CAPACITY;
    const bool prepared=runtime.prepare(root.c_str());
    if(prepared!=expected)fprintf(stderr,"capacity=%u tables=%u error=%s\n",unsigned(RISC_PLATFORM_CAPACITY),count,runtime.error());
    assert(prepared==expected);assert(!ioCalls);
  }
  printf("capacity=%u runtime_object_bytes=%zu: exact registration bound, duplicate/context rejection and prepare including realtime/install tables PASS\n",unsigned(RISC_PLATFORM_CAPACITY),sizeof(RiscBoot::Runtime));
}
