#include "bootstrap/Runtime.h"
#include "support/native_registry/backend.h"
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
static unsigned defaults,children,inits,finis,loads,unloads;
static unsigned childRevision=1;
static std::string mode;
extern "C" unsigned image_cache_event(unsigned event,unsigned value){
  if(event==5)assert(value==1);
  if(event==6)assert(value==childRevision);
  if(event==1){++inits;return mode=="init-failure" && inits==2;}
  if(event==2){++defaults;return defaults<3?(mode=="loose"?2:mode=="missing"?3:1):0;}
  if(event==3){++children;return mode=="retained"?1:0;}
  if(event==4)++finis;
  return 0;
}
extern "C" void risc_test_native_loading(const char*) {
  assert(!risc_test_native_mapping_count());++loads;
}
extern "C" void risc_test_native_unloading(const char*,void*){++unloads;}
static void write(const std::string& path,const char* value){std::ofstream(path)<<value;}
int main(int argc,char**argv){
  assert(argc==3);const std::string root=argv[1];mode=argv[2];
  write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root+"/child.json",R"({"type":"application","id":"child","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"child.elf","entry":"app_main","requires":[]})");
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"child.json","grants":[]}]})");
  const bool retained=mode=="retained";
  for(unsigned session=0;session<(retained?1u:2u);++session){
    defaults=children=inits=finis=loads=unloads=0;
    const size_t reads=risc_test_native_read_count(),relocations=risc_test_native_relocation_count();
    RiscBoot::Runtime runtime({[](){return true;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}});
    assert(runtime.prepare(root.c_str()));assert(runtime.run()!=retained);assert(runtime.retained()==retained);
    assert(risc_test_native_image_count()==0); // Including retained-failure exit.
    const size_t readDelta=risc_test_native_read_count()-reads;
    const size_t relocationDelta=risc_test_native_relocation_count()-relocations;
    if(retained){assert(defaults==1 && children==1 && inits==2 && finis==1 && loads==2 && unloads==1);assert(risc_test_native_mapping_count()==1);assert(readDelta==2);}
    else {
      assert(defaults==3 && inits==loads && loads==unloads && !risc_test_native_mapping_count());
      if(mode=="missing"){assert(children==0 && inits==3 && finis==3 && readDelta==1 && relocationDelta==3);}
      else if(mode=="init-failure"){assert(children==1 && inits==5 && finis==4 && readDelta==2 && relocationDelta==5);}
      else {assert(children==2 && inits==5 && finis==5 && relocationDelta==5);assert(readDelta==(mode=="loose"?3u:2u));}
    }
    printf("Runtime mode=%s session=%u: file_reads=%zu relocations=%zu init=%u fini=%u live_mappings=%zu cached_inputs=%zu\n",mode.c_str(),session,readDelta,relocationDelta,inits,finis,risc_test_native_mapping_count(),risc_test_native_image_count());
    if(mode=="replacement" && session==0){
      // Explicit store replacement between immutable Runtime sessions, using
      // the same mount/path. The next session must run the new admitted bytes.
      std::ifstream replacement(root+"/replacement.elf",std::ios::binary);
      std::ofstream target(root+"/child.elf",std::ios::binary|std::ios::trunc);
      target<<replacement.rdbuf();target.close();assert(target);childRevision=2;
    }
  }
}
