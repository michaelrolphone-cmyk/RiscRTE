#include "bootstrap/Runtime.h"
#include "support/native_registry/backend.h"
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
#include <cstring>
#include "RuntimeFileFixture.h"
static unsigned defaults,children,inits,finis,loads,unloads;
static unsigned childRevision=1;
static std::string mode;
// A complete generic provider wrapper counts every dispatched operation. The
// unavailable state is clean and reversible; retained custody is a separate,
// terminal RuntimeFiles state which can originate in another mapped source.
class FaultSource final {
 public:
  explicit FaultSource(const char* root):backend_(root) {
    table_=*backend_.api();table_.context=this;
#define WRAP(name,...) table_.name=call<&risc_storage_files_v1::name,__VA_ARGS__>
    WRAP(open,const char*,uint32_t,uint32_t*);
    WRAP(close,uint32_t);
    WRAP(read,uint32_t,void*,uint32_t,uint32_t*);
    WRAP(write,uint32_t,const void*,uint32_t,uint32_t*);
    WRAP(seek,uint32_t,uint64_t);
    WRAP(info,uint32_t,uint64_t*,uint64_t*);
    WRAP(sync,uint32_t);
    WRAP(truncate,uint32_t,uint64_t);
    WRAP(stat,const char*,risc_storage_files_stat_v1*);
    WRAP(mkdir,const char*);
    WRAP(rename,const char*,const char*);
    WRAP(remove,const char*);
    WRAP(dir_open,const char*,uint32_t*);
    WRAP(dir_next,uint32_t,risc_storage_files_entry_v1*);
    WRAP(dir_close,uint32_t);
    WRAP(dir_rewind,uint32_t);
    WRAP(dir_tell,uint32_t,uint64_t*);
    WRAP(dir_seek,uint32_t,uint64_t);
#undef WRAP
    assert(files.bind(&table_,allowed,allowed));
  }
  RiscBoot::RuntimeFiles files;
  unsigned calls=0;
  bool unavailable=false;
  bool exitSafe()const{return backend_.exitSafe();}
 private:
  static bool allowed(){return true;}
  template<auto Member,class... Args>static int32_t call(void*raw,Args... args){
    auto& source=*static_cast<FaultSource*>(raw);++source.calls;
    if(source.unavailable)return RISC_FILES_UNAVAILABLE;
    const auto* api=source.backend_.api();return (api->*Member)(api->context,args...);
  }
  RiscStorage::PosixFiles backend_;
  risc_storage_files_v1 table_{};
};
static FaultSource* activeSource;
static FaultSource* otherSource;
static unsigned faultCalls;
static bool sourceFaulted;
extern "C" unsigned image_cache_event(unsigned event,unsigned value){
  if(event==5)assert(value==1);
  if(event==6)assert(value==childRevision);
  if(event==1){++inits;return mode=="init-failure" && inits==2;}
  if(event==2){++defaults;return defaults<3?(mode=="loose"?2:mode=="missing"?3:1):0;}
  if(event==3){
    ++children;
    if(mode=="cache-unavailable" && children==1){
      assert(activeSource && !activeSource->files.retained() && activeSource->exitSafe());
      activeSource->unavailable=true;
      uint32_t handle=99;
      assert(activeSource->files.open("/default.elf",RISC_FILES_READ,&handle)==RISC_FILES_UNAVAILABLE && !handle);
      assert(!activeSource->files.retained());
      faultCalls=activeSource->calls;sourceFaulted=true;
    }
    return mode=="retained"?1:0;
  }
  if(event==4)++finis;
  return 0;
}
extern "C" void risc_test_native_loading(const char*) {
  assert(!risc_test_native_mapping_count());++loads;
}
extern "C" void risc_test_native_unloading(const char*path,void*){
  ++unloads;
  if(mode=="cache-retained-source" && std::strstr(path,"/child.elf")){
    assert(activeSource && otherSource && !sourceFaulted);
    assert(risc_test_native_image_count()==2); // Default is already a cache hit candidate.
    assert(activeSource->exitSafe() && otherSource->exitSafe());
    otherSource->files.retainCustody();
    assert(!activeSource->files.retained() && otherSource->files.retained());
    faultCalls=activeSource->calls+otherSource->calls;sourceFaulted=true;
  }
}
static void write(const std::string& path,const char* value){std::ofstream(path)<<value;}
int main(int argc,char**argv){
  assert(argc==3);const std::string root=argv[1];mode=argv[2];
  write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root+"/child.json",R"({"type":"application","id":"child","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"child.elf","entry":"app_main","requires":[]})");
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"child.json","grants":[]}]})");
  const bool sourceRetained=mode=="cache-retained-source";
  const bool sourceUnavailable=mode=="cache-unavailable";
  const bool retained=mode=="retained" || sourceRetained;
  for(unsigned session=0;session<(retained?1u:2u);++session){
    defaults=children=inits=finis=loads=unloads=0;sourceFaulted=false;
    activeSource=nullptr;otherSource=nullptr;faultCalls=0;
    const size_t reads=risc_test_native_read_count(),relocations=risc_test_native_relocation_count();
    const size_t legacyOpens=risc_test_native_legacy_open_count();
    RiscTest::FileSources runtimeFiles0({root.c_str()});
    FaultSource active(root.c_str()),other(root.c_str());
    const RiscBoot::RuntimeFileSources::Entry entries[]={{root.c_str(),&active.files},{"/unselected-candidate",&other.files}};
    const RiscBoot::RuntimeFileSources faultSources(entries,2);
    RiscBoot::Runtime runtime({[](){return true;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}});
    if(sourceUnavailable || sourceRetained){
      activeSource=&active;otherSource=&other;assert(runtime.bindFileSources(&faultSources));
    }else assert(runtimeFiles0.bind(runtime));
    assert(runtime.prepare(root.c_str()));assert(runtime.run()!=retained);assert(runtime.retained()==retained);
    assert(risc_test_native_image_count()==0); // Including retained-failure exit.
    assert(risc_test_native_legacy_open_count()==legacyOpens); // No POSIX loader escape.
    const size_t readDelta=risc_test_native_read_count()-reads;
    const size_t relocationDelta=risc_test_native_relocation_count()-relocations;
    if(sourceRetained){
      assert(sourceFaulted && faultSources.retained());
      assert(defaults==1 && children==1 && inits==2 && finis==2 && loads==2 && unloads==2);
      assert(!risc_test_native_mapping_count() && readDelta==2 && relocationDelta==2);
      assert(active.calls+other.calls==faultCalls); // Retained map blocks cached default before a probe.
    }else if(retained){assert(defaults==1 && children==1 && inits==2 && finis==1 && loads==2 && unloads==1);assert(risc_test_native_mapping_count()==1);assert(readDelta==2);}
    else {
      assert(defaults==3 && inits==loads && loads==unloads && !risc_test_native_mapping_count());
      if(mode=="missing"){assert(children==0 && inits==3 && finis==3 && readDelta==1 && relocationDelta==3);}
      else if(mode=="init-failure"){assert(children==1 && inits==5 && finis==4 && readDelta==2 && relocationDelta==5);}
      else {assert(children==2 && inits==5 && finis==5 && relocationDelta==5);assert(readDelta==(mode=="loose"?3u:2u));}
    }
    if(sourceUnavailable){
      assert(sourceFaulted && active.unavailable && !faultSources.retained());
      assert(active.calls==faultCalls && !other.calls); // Three fresh cache-hit mappings, zero provider I/O.
      active.unavailable=false;
      uint32_t handle=0;
      assert(active.files.open("/default.elf",RISC_FILES_READ,&handle)==RISC_FILES_OK && handle);
      assert(active.files.close(handle)==RISC_FILES_OK && active.exitSafe());
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
