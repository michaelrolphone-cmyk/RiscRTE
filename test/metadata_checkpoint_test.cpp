// Only provider I/O, wall time and scheduler waits are substituted. The real
// readJson, size/deadline checks, JSON guard and parser execute on every input.
#include "bootstrap/Json.h"
#include "bootstrap/RuntimeFiles.h"
#include "runtime/storage/PosixFiles.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr uint64_t Millisecond=1000000;
uint64_t clockNs=0,readNs=0,delayNs=Millisecond,clockCallNs=0;
uint64_t readCalls=0,readBytes=0,delayCalls=0,requestedTicks=0,opens=0,closes=0;
size_t sinceYield=0,maxBetweenYields=0;
struct Input {
  std::string bytes;
  size_t offset=0,maxRead=512;
  unsigned failRead=0;
  bool eof=false,error=false,failClose=false,failOpen=false,failAllocation=false;
} input;
FILE* fake(){return reinterpret_cast<FILE*>(&input);}
const char* FakePath="/metadata-checkpoint-fixture";
bool providerRead(const char*,JsonDocument&,bool*);
void reset(const std::string& bytes) {
  input={};input.bytes=bytes;
  clockNs=readNs=clockCallNs=0;delayNs=Millisecond;
  readCalls=readBytes=delayCalls=requestedTicks=opens=closes=0;
  sinceYield=maxBetweenYields=0;
#if RISC_PERFORMANCE_TRACE
  RiscPerf::configure([](){return clockNs/1000;},[](){return true;},true);
#endif
}
std::string json(size_t size) {
  std::string bytes="{\"valid\":true}";
  assert(size>=bytes.size());bytes.resize(size,' ');return bytes;
}
bool read(bool* retained=nullptr) {
  JsonDocument doc;
  const bool ok=providerRead(FakePath,doc,retained);
  assert(opens==1 && closes==(input.failOpen?0:1));
#if RISC_PERFORMANCE_TRACE
  assert(RiscPerf::data.counts[33]==delayCalls);
  assert(RiscPerf::data.values[33]==requestedTicks);
#endif
  assert(requestedTicks==delayCalls);return ok;
}
void fixedBounds() {
  reset(json(16));assert(read() && readCalls==2 && delayCalls==1 && readBytes==16);
  reset(json(32768));assert(read() && readCalls==65 && delayCalls==1 && maxBetweenYields==32768);
  reset(json(65536));assert(read() && readCalls==129 && delayCalls==2 && maxBetweenYields==32768);
  reset(json(32769));assert(read() && readCalls==66 && delayCalls==2 && maxBetweenYields==32768);
  // Nonzero short reads must continue, including a final short read before EOF.
  reset(json(1234));input.maxRead=7;assert(read() && readCalls==178 && readBytes==1234 && delayCalls==1);
  reset(json(32768));readNs=Millisecond;
  assert(read() && readCalls==65 && delayCalls==32 && maxBetweenYields==1024);
  reset(json(32768));readNs=3*Millisecond;
  assert(read() && readCalls==65 && delayCalls==65 && maxBetweenYields==512);
  // Large absolute clock values cannot change elapsed-time checkpoints.
  reset(json(65536));clockNs=uint64_t(UINT32_MAX)*Millisecond;
  assert(read() && delayCalls==2 && maxBetweenYields==32768);
  // Each short metadata file yields, so repeatedly resetting its byte/time
  // budget cannot starve the scheduler across a complete graph's manifests.
  for(unsigned i=0;i<100;++i){reset(json(16));assert(read() && delayCalls==1);}
  puts("JSON checkpoints: short/partial/EOF/exact-bound/64KiB reads; byte/time and consecutive-file fairness PASS");
}
void deadlineAndFailures() {
  reset(json(16));clockCallNs=5001*Millisecond;
  assert(!read() && readCalls==0 && delayCalls==0);
  reset(json(65536));readNs=1001*Millisecond;
  assert(!read() && readCalls==5 && delayCalls==5);
  reset(json(16));delayNs=5001*Millisecond;
  assert(!read() && readCalls==2 && delayCalls==1);
  reset(json(16));delayNs=5000*Millisecond;
  assert(read()); // Preserve the existing strictly-greater-than-five-second bound.
  reset(json(65537));assert(!read() && readBytes==65537 && readCalls==129 && delayCalls==3);
  reset("{");assert(!read() && readCalls==2 && delayCalls==1);
  reset("{\"x\":1,\"x\":2}");assert(!read());
  reset("");assert(!read() && readCalls==1 && delayCalls==0);
  reset(json(1234));input.failRead=2;assert(!read() && readBytes==512 && readCalls==2 && delayCalls==1);
  reset(json(16));input.failRead=1;assert(!read() && readBytes==0 && delayCalls==0);
  reset(json(16));input.failAllocation=true;assert(!read() && readCalls==0 && delayCalls==0);
  reset(json(16));input.failOpen=true;assert(!read() && readCalls==0 && delayCalls==0);
  bool retained=false;
  reset(json(16));input.failClose=true;assert(!read(&retained) && retained);
  reset(json(16));assert(read(&retained) && retained);
  reset("{");input.failClose=true;retained=false;assert(!read(&retained) && retained);
  puts("JSON checkpoints: deadline during I/O/wait, exact bound, oversize, corruption, read/OOM/open/close failure and sticky retention PASS");
}
void storeCounts(const char* root) {
  const std::string base=std::string(root)+"/";
  std::ifstream file(base+"boot.json",std::ios::binary);
  const std::string bytes{std::istreambuf_iterator<char>(file),{}};
  JsonDocument boot;assert(RiscBoot::parse(bytes.data(),bytes.size(),boot));
  std::vector<std::string> paths{base+"boot.json",base+boot["board"].as<const char*>()};
  for(JsonObjectConst row:boot["drivers"].as<JsonArrayConst>())paths.push_back(base+row["manifest"].as<const char*>());
  bool cohort=false;
  for(JsonObjectConst row:boot["app_capabilities"].as<JsonArrayConst>()){
    paths.push_back(base+row["manifest"].as<const char*>());
    for(JsonObjectConst grant:row["grants"].as<JsonArrayConst>())
      if(RiscBoot::eq(grant["capability"],"runtime.retained-wake"))cohort=true;
  }
  if(cohort)paths.push_back(base+"cohort.json");
  reset("");
  for(const auto& path:paths){JsonDocument doc;assert(providerRead(path.c_str(),doc,nullptr));}
  assert(opens==paths.size() && closes==opens);
  printf("Exact-store production reader (zero simulated I/O time): files=%llu bytes=%llu reads=%llu delay_calls=%llu requested_ticks=%llu max_bytes_between_yields=%zu\n",
    (unsigned long long)opens,(unsigned long long)readBytes,(unsigned long long)readCalls,
    (unsigned long long)delayCalls,(unsigned long long)requestedTicks,maxBetweenYields);
}
}

// Replace the out-of-line host clock implementation for deterministic boundary
// injection; no real sleeps or assumptions about host/disk speed enter counts.
std::chrono::steady_clock::time_point std::chrono::steady_clock::now() noexcept {
  const auto result=time_point(std::chrono::duration_cast<duration>(std::chrono::nanoseconds(clockNs)));
  clockNs+=clockCallNs;return result;
}
void vTaskDelay(uint32_t ticks) {
  ++delayCalls;requestedTicks+=ticks;clockNs+=ticks*delayNs;
  maxBetweenYields=std::max(maxBetweenYields,sinceYield);sinceYield=0;
}
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" int __real_fclose(FILE*);
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
extern "C" int __real_feof(FILE*);
extern "C" int __real_ferror(FILE*);
extern "C" void* __real_malloc(size_t);
extern "C" FILE* __wrap_fopen(const char* path,const char* mode) {
  ++opens;return std::strcmp(path,FakePath)?__real_fopen(path,mode):input.failOpen?nullptr:fake();
}
extern "C" int __wrap_fclose(FILE* stream) {++closes;return stream==fake()?(input.failClose?EOF:0):__real_fclose(stream);}
extern "C" size_t __wrap_fread(void* data,size_t size,size_t count,FILE* stream) {
  assert(size==1 && count<=512);
  ++readCalls;clockNs+=readNs;
  size_t got=0;
  if(stream!=fake())got=__real_fread(data,size,count,stream);
  else {
    if(input.failRead && readCalls==input.failRead){input.error=true;input.eof=true;return 0;}
    got=std::min({count,input.maxRead,input.bytes.size()-input.offset});
    if(got)std::memcpy(data,input.bytes.data()+input.offset,got);
    input.offset+=got;if(!got)input.eof=true;
  }
  readBytes+=size*got;sinceYield+=size*got;return got;
}
extern "C" int __wrap_feof(FILE* stream) {return stream==fake()?input.eof:__real_feof(stream);}
extern "C" int __wrap_ferror(FILE* stream) {return stream==fake()?input.error:__real_ferror(stream);}
extern "C" void* __wrap_malloc(size_t size) {return size==65537 && input.failAllocation?nullptr:__real_malloc(size);}
namespace {
FILE* providerStream=nullptr;
bool providerRead(const char* path,JsonDocument& doc,bool* retained) {
  // Unused callbacks are a real complete table; only open/read/close are
  // replaced, so the production generic broker validates the whole ABI.
  RiscStorage::PosixFiles backing("/");auto table=*backing.api();
  table.open=[](void*,const char* path,uint32_t flags,uint32_t* handle)->int32_t{
    assert(flags==RISC_FILES_READ && !providerStream);providerStream=fopen(path,"rb");
    if(!providerStream)return RISC_FILES_NOT_FOUND;
    *handle=1;return RISC_FILES_OK;
  };
  table.read=[](void*,uint32_t handle,void* bytes,uint32_t count,uint32_t* actual)->int32_t{
    assert(handle==1 && providerStream);*actual=fread(bytes,1,count,providerStream);
    return *actual || (feof(providerStream) && !ferror(providerStream))?RISC_FILES_OK:RISC_FILES_IO;
  };
  table.close=[](void*,uint32_t handle)->int32_t{
    assert(handle==1 && providerStream);const int result=fclose(providerStream);providerStream=nullptr;
    return result?RISC_FILES_RETAINED:RISC_FILES_OK;
  };
  RiscBoot::RuntimeFiles files;assert(files.bind(&table,[](){return true;},[](){return true;}));
  RiscBoot::RuntimeFileSources::Entry entry{"/",&files};RiscBoot::RuntimeFileSources sources(&entry,1);
  return RiscBoot::readJson(RiscBoot::RuntimeFileView(&sources,"/"),path,doc,retained);
}
}
int main(int argc,char** argv) {
  fixedBounds();deadlineAndFailures();
  if(argc==3 && !std::strcmp(argv[1],"--store"))storeCounts(argv[2]);else assert(argc==1);
}
