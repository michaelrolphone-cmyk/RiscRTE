#include "bootstrap/Runtime.h"
#include "runtime/storage/PosixFiles.h"
#include "support/native_registry/backend.h"
#include "diagnostics/Performance.h"
#include <esp_elf.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static std::string mode;
static unsigned events[8], loads[3], unloads[3];
static const unsigned* rootApi;
static size_t pendingImageSize;
static void* childBytes;
static unsigned imageAllocations, allocationFailures, childFrees;
static bool completes,terminal,inChildRead;
static uint64_t clockMs;
static unsigned readMilliseconds,imageYields,readerCalls;
static size_t progressBytes,lastYieldBytes,maxYieldBytes;
static int childError;

// Executable-only clock replacement: production Runtime still calls its real
// std::chrono checkpoint implementation. Only fixture reads/delays advance time.
std::chrono::steady_clock::time_point std::chrono::steady_clock::now() noexcept {
  return time_point(std::chrono::duration_cast<duration>(std::chrono::milliseconds(clockMs)));
}
static uint64_t traceClock() {errno=EDOM;return clockMs*1000;}
static void delay(uint32_t ticks) {
  assert(ticks==1);
  if(!inChildRead)return;
  const size_t bytes=progressBytes-lastYieldBytes;
  assert(bytes && bytes<=32*1024);maxYieldBytes=std::max(maxYieldBytes,bytes);
  lastYieldBytes=progressBytes;++imageYields;
  if(mode=="yield-timeout")clockMs+=30000;
}

extern "C" void* risc_test_real_dlopen_reader_instance(const char*,esp_dl_image_reader,void*);
struct ObservedReader {esp_dl_image_reader reader;void* context;};
static int observeReader(void* raw,const char* path,elf_file_t* file) {
  const auto& observed=*static_cast<ObservedReader*>(raw);
  const uint32_t first=RiscPerf::data.next;
  const int result=observed.reader(observed.context,path,file);
  const int error=errno;++readerCalls;
  unsigned phases[4]{},count=0;
  for(uint32_t i=0;i<RiscPerf::data.count;++i) {
    const auto& record=RiscPerf::data.records[(RiscPerf::data.head+RiscPerf::Capacity-RiscPerf::data.count+i)%RiscPerf::Capacity];
    if(record.sequence>=first && record.phase>=40 && record.phase<=46) {
      assert(count<4);phases[count++]=record.phase;
      if(record.phase==41)assert(record.value==file->size);
    }
  }
  if(result==0)assert(count==4 && phases[0]==40 && phases[1]==41 && phases[2]==45 && phases[3]==46);
  else assert(count==2 && phases[0]==40 && phases[1]==44);
  if(std::strstr(path,"/child.elf")) {
    inChildRead=false;childError=error;
    if(result<0)assert(error==(terminal?EIO:mode=="oom-clean"?ENOMEM:ETIMEDOUT));
  }
  errno=error;return result;
}
extern "C" void* esp_dlopen_reader_instance(const char* path,esp_dl_image_reader reader,void* context) {
  ObservedReader observed{reader,context};
  return risc_test_real_dlopen_reader_instance(path,observeReader,&observed);
}

extern "C" void* risc_test_base_elf_malloc(uint32_t, bool);
extern "C" void risc_test_base_elf_free(void*);
extern "C" void* esp_elf_malloc(uint32_t size, bool executable) {
  if(pendingImageSize) {
    assert(!executable && size==pendingImageSize && !childBytes);
    pendingImageSize=0;++imageAllocations;
    if(mode=="oom-close-failed" || mode=="oom-clean") {++allocationFailures;errno=ENOMEM;return nullptr;}
    childBytes=risc_test_base_elf_malloc(size,executable);assert(childBytes);return childBytes;
  }
  return risc_test_base_elf_malloc(size,executable);
}
extern "C" void esp_elf_free(void* memory) {
  if(memory && memory==childBytes) {assert(!terminal);++childFrees;childBytes=nullptr;}
  risc_test_base_elf_free(memory);
}
extern "C" void esp_elf_close(elf_file_t* file) {
  if(!file)return;
  esp_elf_free(file->payload);file->payload=nullptr;file->size=0;
}
extern "C" void runtime_image_custody_event(unsigned event,const void* api) {
  assert(event && event<8);++events[event];
  if(event==1) {assert(!rootApi);rootApi=static_cast<const unsigned*>(api);}
  else {assert(!terminal);if(event>=4)assert(completes);if(event==4)assert(api==rootApi);}
}
static unsigned imageIndex(const char* path) {
  if(std::strstr(path,"/root.elf"))return 0;
  if(std::strstr(path,"/child.elf"))return 1;
  assert(std::strstr(path,"/default.elf"));return 2;
}
extern "C" void risc_test_native_loading(const char* path) {
  const unsigned index=imageIndex(path);++loads[index];
  if(index)assert(completes);
}
extern "C" void risc_test_native_unloading(const char* path,void*) {
  assert(!terminal);++unloads[imageIndex(path)];
}

// Complete generic-file table. Faults apply only to child.elf; metadata and the
// root dependency use exactly the same selected Runtime source successfully.
class Source final {
 public:
  explicit Source(const char* root,bool inject):backend_(root),inject_(inject) {
    table_=*backend_.api();table_.context=this;
#define WRAP(name,...) table_.name=call<&risc_storage_files_v1::name,__VA_ARGS__>
    WRAP(write,uint32_t,const void*,uint32_t,uint32_t*);
    WRAP(seek,uint32_t,uint64_t);
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
    table_.open=open;table_.close=close;table_.read=read;table_.info=info;
    assert(files.bind(&table_,allowed,allowed));
  }
  RiscBoot::RuntimeFiles files;
  unsigned calls=0,childOpens=0,childReads=0,childCloses=0,childInfos=0;
  uint32_t childHandle=0;
  size_t childReadBytes=0,childLength=0;
  bool exitSafe()const{return backend_.exitSafe();}
 private:
  static bool allowed(){return true;}
  template<auto Member,class... Args>static int32_t call(void* raw,Args... args) {
    auto& source=*static_cast<Source*>(raw);++source.calls;
    const auto* api=source.backend_.api();return (api->*Member)(api->context,args...);
  }
  static int32_t open(void* raw,const char* path,uint32_t flags,uint32_t* handle) {
    auto& s=*static_cast<Source*>(raw);
    const bool target=s.inject_ && !std::strcmp(path,"/child.elf");
    if(target) {
      // Child is first in boot order: root is alive solely through GraphV2's
      // dependency pin, with no independent boot grant to hide a cleanup bug.
      assert(events[1]==1 && !events[2] && !events[3]);
      assert(rootApi && rootApi[1]==0x51a7c0de && risc_test_native_mapping_count()==1);
      assert(!s.childHandle);++s.childOpens;
    }
    const int32_t rc=call<&risc_storage_files_v1::open>(raw,path,flags,handle);
    if(target){assert(rc==RISC_FILES_OK && *handle);s.childHandle=*handle;inChildRead=true;}
    return rc;
  }
  static int32_t info(void* raw,uint32_t handle,uint64_t* length,uint64_t* position) {
    auto& s=*static_cast<Source*>(raw);
    const int32_t rc=call<&risc_storage_files_v1::info>(raw,handle,length,position);
    if(handle==s.childHandle && s.childHandle) {
      ++s.childInfos;assert(rc==RISC_FILES_OK);
      if(s.childInfos==1) {assert(!*position && *length>=52);s.childLength=*length;pendingImageSize=*length;}
    }
    return rc;
  }
  static int32_t read(void* raw,uint32_t handle,void* bytes,uint32_t size,uint32_t* actual) {
    auto& s=*static_cast<Source*>(raw);
    if(handle==s.childHandle && s.childHandle) {
      ++s.childReads;assert(childBytes && !allocationFailures);
      if(s.childReads==1)assert(bytes==childBytes); // Provider fills the owned snapshot directly.
      if(mode=="read-retained") {++s.calls;*actual=0;return RISC_FILES_RETAINED;}
    }
    const int32_t rc=call<&risc_storage_files_v1::read>(raw,handle,bytes,size,actual);
    if(handle==s.childHandle && s.childHandle) {
      s.childReadBytes+=*actual;progressBytes=s.childReadBytes;
      if(*actual) {
        assert(*actual<=4096);clockMs+=readMilliseconds;
        if(mode=="read-timeout")clockMs+=30000;
      }
    }
    return rc;
  }
  static int32_t close(void* raw,uint32_t handle) {
    auto& s=*static_cast<Source*>(raw);
    const bool target=handle==s.childHandle && s.childHandle;
    if(target) {
      ++s.childCloses;
      if(mode=="close-failed" || mode=="oom-close-failed") {++s.calls;return RISC_FILES_UNAVAILABLE;}
    }
    const int32_t rc=call<&risc_storage_files_v1::close>(raw,handle);
    if(target && rc==RISC_FILES_OK){s.childHandle=0;inChildRead=false;}
    return rc;
  }
  RiscStorage::PosixFiles backend_;
  risc_storage_files_v1 table_{};
  bool inject_;
};
static void write(const std::string& path,const char* value) {std::ofstream out(path);out<<value;assert(out);}
int main(int argc,char** argv) {
  assert(argc==3);const std::string root=argv[1];mode=argv[2];
  terminal=mode=="read-retained" || mode=="close-failed" || mode=="oom-close-failed";
  completes=mode=="clean" || mode=="fairness-0ms" || mode=="fairness-1ms" || mode=="fairness-3ms";
  assert(terminal || completes || mode=="read-timeout" || mode=="yield-timeout" || mode=="oom-clean");
  readMilliseconds=mode=="fairness-1ms"?1:mode=="fairness-3ms"?3:0;
  RiscPerf::configure(traceClock,[](){return true;},true);
  write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root+"/root.json",R"({"type":"driver","id":"custody-root","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"root.elf","requires":[],"provides":[{"capability":"test.root","api":1}]})");
  write(root+"/child.json",R"({"type":"driver","id":"custody-child","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"child.elf","requires":[{"capability":"test.root","api":1}],"provides":[{"capability":"test.child","api":1}]})");
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"child.json"},{"manifest":"root.json"}]})");
  // Retained Runtime, source map, provider tokens and bytes deliberately live
  // until process exit. Destroying one would violate the production custody
  // contract, so each fault runs in its own process (including under ASan).
  auto* source=new Source(root.c_str(),true);
  auto* spare=new Source(root.c_str(),false);
  auto* entries=new RiscBoot::RuntimeFileSources::Entry[2]{{root.c_str(),&source->files},{"/unused-provider",&spare->files}};
  auto* sources=new RiscBoot::RuntimeFileSources(entries,2);
  auto* runtime=new RiscBoot::Runtime({[](){return true;},[](risc_runtime_health_v1*){return true;},delay,[](const char*){return true;}});
  assert(runtime->bindFileSources(sources) && runtime->prepare(root.c_str()));
  assert(!risc_test_native_mapping_count() && !events[1] && !source->childOpens);
  assert(runtime->run()==completes && runtime->retained()==terminal);
  assert(!risc_test_native_legacy_open_count() && !spare->calls);
  assert(source->childOpens==1 && imageAllocations==1 && !pendingImageSize);
  assert(allocationFailures==unsigned(mode=="oom-close-failed" || mode=="oom-clean"));
  assert(events[1]==1 && loads[0]==1 && !risc_test_native_image_count());
  assert(readerCalls==2); // Driver reader boundary observed; default uses cached-reader API.
  assert(RiscPerf::data.counts[40]==(completes?3u:2u));
  for(unsigned phase:{41u,45u,46u})assert(RiscPerf::data.counts[phase]==(completes?3u:1u));
  assert(RiscPerf::data.counts[44]==unsigned(!completes));
  assert(source->childLength==256*1024);
  if(completes) {
    assert(!sources->retained() && source->exitSafe() && !source->childHandle);
    for(unsigned i=1;i<8;++i)assert(events[i]==1);
    for(unsigned i=0;i<3;++i)assert(loads[i]==1 && unloads[i]==1);
    assert(!risc_test_native_mapping_count() && childFrees==1 && !childBytes);
    const unsigned expected=readMilliseconds==0?8:readMilliseconds==1?32:64;
    assert(imageYields==expected && maxYieldBytes==256*1024/expected);
  }else if(terminal) {
    assert(sources->retained() && source->files.retained() && !source->exitSafe() && source->childHandle);
    assert(risc_test_native_mapping_count()==1 && risc_test_native_relocation_count()==1);
    assert(risc_test_native_read_count()==1); // Only root passed admission; failed child has no module handle.
    assert(!loads[1] && !loads[2] && !unloads[0] && !unloads[1] && !unloads[2]);
    for(unsigned i=2;i<8;++i)assert(!events[i]); // No quiesce, stop, child start or default invocation.
    assert(!childFrees && rootApi[1]==0x51a7c0de);
    if(mode=="read-retained")assert(source->childReads==1 && !source->childCloses && childBytes);
    else assert(source->childCloses==1);
    if(mode=="oom-close-failed")assert(!source->childReads && !childBytes);
    if(mode=="close-failed")assert(source->childReadBytes==source->childLength && childBytes);
    const unsigned calls=source->calls;
    assert(!runtime->run() && runtime->retained());
    assert(!runtime->prepare(root.c_str()));
    assert(source->calls==calls && !spare->calls && !risc_test_native_legacy_open_count());
    assert(source->childOpens==1 && imageAllocations==1 && risc_test_native_mapping_count()==1);
    assert(!events[2] && !events[3] && !unloads[0] && rootApi[1]==0x51a7c0de);
  }else {
    assert(!sources->retained() && source->exitSafe() && !source->childHandle);
    assert(!risc_test_native_mapping_count() && events[2]==1 && events[3]==1 && unloads[0]==1);
    assert(!loads[1] && !loads[2] && !unloads[1] && !unloads[2]);
    assert(source->childCloses==1 && !childBytes && childFrees==unsigned(mode!="oom-clean"));
    for(unsigned i=4;i<8;++i)assert(!events[i]);
    if(mode=="read-timeout")assert(source->childReads==1 && !imageYields && clockMs==30000);
    if(mode=="yield-timeout")assert(source->childReads==8 && imageYields==1 && clockMs==30000);
    if(mode=="oom-clean")assert(!source->childReads && !imageYields && childError==ENOMEM);
  }
  if(!terminal){delete runtime;delete sources;delete[] entries;delete spare;delete source;}
  std::printf("Runtime internal image custody mode=%s: root_start/quiesce/stop=%u/%u/%u child_maps=%u allocation_failures=%u yields=%u max_yield_bytes=%zu PASS\n",
              mode.c_str(),events[1],events[2],events[3],loads[1],allocationFailures,imageYields,maxYieldBytes);
}
