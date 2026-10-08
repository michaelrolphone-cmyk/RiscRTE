#include "bootstrap/Runtime.h"
#include "diagnostics/Performance.h"
#include "diagnostics/StageLog.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <string>
#include <vector>

static unsigned generation=0;
static bool ownerOk=true;
static uint64_t ticks=0;
static std::vector<uint32_t> begins,childIds;
static unsigned initialized=0,finalized=0;
static std::vector<std::string> lines;
static std::vector<uint32_t> waits;
static bool startupAdmit=true;
extern "C" bool test_startup_admit(){return startupAdmit;}
#if RISC_STAGE_LOGS
namespace RiscDiagnostics {
static uint64_t stageTicks=0;
uint64_t monotonicUs(){return ++stageTicks;}
void timestamped(const char* format,...){
  char text[256];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);
  lines.emplace_back(text);
}
}
static size_t findLine(const std::string& text,size_t after=0){
  for(size_t i=after;i<lines.size();++i)if(lines[i].find(text)!=std::string::npos)return i;
  assert(false && "missing plain stage statement");return 0;
}
#endif
extern "C" bool test_startup_cleanup(){return true;}
extern "C" unsigned performance_generation(){return ++generation;}
extern "C" void performance_owner(int value){ownerOk=value!=0;}
extern "C" void performance_observe(unsigned event,uint32_t id){
  if(event==1)begins.push_back(id);
  if(event==2)childIds.push_back(id);
  if(event==10)++initialized;
  if(event==11)++finalized;
}
static bool owner(){return ownerOk;}
static uint64_t clockUs(){ticks+=7;return ticks;}
static void write(const std::string& path,const char* value){std::ofstream(path)<<value;}
static std::vector<RiscPerf::Record> records(){
  std::vector<RiscPerf::Record> out;
  for(uint32_t i=0;i<RiscPerf::data.count;++i)
    out.push_back(RiscPerf::data.records[(RiscPerf::data.head+RiscPerf::Capacity-RiscPerf::data.count+i)%RiscPerf::Capacity]);
  return out;
}
static void run(const std::string& root,bool enabled){
  generation=initialized=finalized=0;begins.clear();childIds.clear();lines.clear();waits.clear();ticks=0;
  RiscPerf::configure(clockUs,owner,enabled);
  RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){return true;},
    [](uint32_t ms){waits.push_back(ms);if(RiscPerf::enabled)ticks+=uint64_t(ms)*1000;},[](const char* s){lines.emplace_back(s);return true;}});
  assert(runtime.prepare(root.c_str()));assert(runtime.run());assert(!runtime.retained());
  assert(generation==3 && initialized==4 && finalized==4);
  assert((waits==std::vector<uint32_t>{3,1,1,3,1,1,3}));
  assert(std::count(lines.begin(),lines.end(),"RTE_APP child=failed action=reload-default")==1);
  for(const auto& line:lines)assert(line.find("RTE_PERF")==std::string::npos);
#if RISC_STAGE_LOGS
  // Real app lifecycle, including default/child handoff and failed-child
  // fallback, emits named statements automatically with the recorder off too.
  size_t next=0;
  for(const char* file:{"default.elf","child.elf","default.elf","missing.elf","default.elf"}){
    next=findLine("app load begin file="+root+"/"+file,next);
    next=findLine("app load end file="+root+"/"+file,next+1);
    assert(lines[next].find("elapsed_us=")!=std::string::npos);
    if(std::string(file)=="missing.elf")assert(lines[next].find("result=failed")!=std::string::npos);
    else {
      next=findLine("app init begin file="+root+"/"+file,next+1);
      next=findLine("app init end file="+root+"/"+file+" result=0",next+1);
      next=findLine("app entry begin file="+root+"/"+file,next+1);
      next=findLine("app entry returned file="+root+"/"+file,next+1);
      next=findLine("app unload begin file="+root+"/"+file,next+1);
      next=findLine("app unload end file="+root+"/"+file+" result=ok",next+1);
    }
    ++next;
  }
#endif
  assert(begins.size()==3 && childIds.size()==1);
  if(!enabled){
    assert(ticks==0 && RiscPerf::data.count==0 && RiscPerf::data.overhead_count==0);
    assert((begins==std::vector<uint32_t>{0,0,0}) && childIds[0]==0);
    return;
  }
  assert(begins[0] && begins[1]>begins[0] && begins[2]>begins[1]);
  assert(childIds[0]==begins[0]);
  const auto captured=records();assert(!captured.empty() && RiscPerf::data.lost==0);
  uint64_t last=0;uint32_t sequence=0;
  for(const auto& r:captured){assert(r.timestamp_us>last && r.sequence>sequence);last=r.timestamp_us;sequence=r.sequence;assert(r.value!=999);}
  auto position=[&](uint32_t invocation,uint32_t phase){
    auto it=std::find_if(captured.begin(),captured.end(),[&](const auto& r){return r.invocation==invocation && r.phase==phase;});
    assert(it!=captured.end());return size_t(it-captured.begin());
  };
  for(uint32_t invocation:{1u,2u,3u,5u}){
    size_t previous=position(invocation,RiscPerf::Invocation);
    for(uint32_t phase:{RiscPerf::AppLoadBegin,RiscPerf::AppLoadEnd,RiscPerf::InitBegin,RiscPerf::InitEnd,
                       RiscPerf::Entry,RiscPerf::Return,RiscPerf::UnloadBegin,RiscPerf::UnloadEnd}){
      const size_t next=position(invocation,phase);assert(next>previous);previous=next;
    }
  }
  // The handoff only begins loading after the previous image has been unmapped.
  assert(position(1,RiscPerf::LaunchRequested)<position(1,RiscPerf::Return));
  assert(position(1,RiscPerf::UnloadEnd)<position(2,RiscPerf::AppLoadBegin));
  assert(position(3,RiscPerf::UnloadEnd)<position(4,RiscPerf::AppLoadBegin));
  assert(position(4,RiscPerf::AppLoadBegin)<position(4,RiscPerf::AppLoadFail));
  assert(position(4,RiscPerf::AppLoadFail)<position(5,RiscPerf::AppLoadBegin));
  for(const auto& r:captured){
    if(r.invocation==2)assert(r.interaction==begins[0]);
    if(r.invocation==4)assert(r.interaction==begins[1]);
    if(r.invocation==4)assert(r.phase!=RiscPerf::Entry && r.phase!=RiscPerf::UnloadEnd);
  }
  assert(RiscPerf::data.counts[RiscPerf::Invocation]==5);
  assert(RiscPerf::data.counts[RiscPerf::AppLoadBegin]==5);
  assert(RiscPerf::data.counts[RiscPerf::Entry]==4);
  assert(RiscPerf::data.counts[RiscPerf::AppLoadFail]==1);
  assert(RiscPerf::data.counts[RiscPerf::InteractionBegin]==3);
  assert(RiscPerf::data.counts[RiscPerf::Recognized]==3);
  assert(RiscPerf::data.counts[RiscPerf::SchedulerWait]==3);
  assert(RiscPerf::data.values[RiscPerf::SchedulerWait]==9);
  assert(RiscPerf::data.durations_us[RiscPerf::SchedulerWait]==9021);
  for(const auto& r:captured)assert(r.phase!=RiscPerf::SchedulerWait && r.phase!=RiscPerf::ProviderPoll);
  assert(RiscPerf::data.overhead_count>0 && RiscPerf::data.overhead_min_us==7 && RiscPerf::data.overhead_max_us==7);
  const uint32_t count=RiscPerf::data.count;const uint64_t before=ticks;
  ownerOk=false;assert(!RiscPerf::interaction(0,1,0));RiscPerf::emit(8);ownerOk=true;
  assert(RiscPerf::data.count==count && ticks==before);
}
int main(int argc,char** argv){
  assert(argc==2);const std::string root=argv[1];
  write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[]})");
  run(root,true);run(root,false);
  write(root+"/driver.json",R"({"type":"driver","id":"startup-failure","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[],"provides":[{"capability":"test.startup","api":1}]})");
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"driver.json"}]})");
  RiscPerf::configure(clockUs,owner,true);generation=2;
  {
    RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}});
    assert(runtime.prepare(root.c_str()) && runtime.run());
  }
  for(uint32_t phase:{RiscPerf::ProviderAcquireBegin,RiscPerf::ProviderAcquireEnd,RiscPerf::ProviderStartBegin,RiscPerf::ProviderStartEnd})
    assert(RiscPerf::data.counts[phase]==1);
  std::vector<uint32_t> providerPhases;
  for(const auto& r:records())if(r.phase>=22 && r.phase<=25)providerPhases.push_back(r.phase);
  assert((providerPhases==std::vector<uint32_t>{22,24,25,23}));
#if RISC_STAGE_LOGS
  auto start=findLine("provider load begin id=startup-failure");
  start=findLine("provider start begin id=startup-failure",start+1);
  start=findLine("provider start end id=startup-failure result=ok elapsed_us=",start+1);
  findLine("provider load end id=startup-failure result=ok elapsed_us=",start+1);
  // Failed startup is named and does not acquire a false successful endpoint.
  lines.clear();startupAdmit=false;
  {
    RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char* s){lines.emplace_back(s);return true;}});
    assert(runtime.prepare(root.c_str()) && !runtime.run());
  }
  findLine("provider start end id=startup-failure result=failed elapsed_us=");
  findLine("provider load end id=startup-failure result=failed elapsed_us=");
  findLine("primary probe pullup denied");
  startupAdmit=true;lines.clear();
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"driver.json"}]})");
  generation=2;
  {
    RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char* s){lines.emplace_back(s);return true;}});
    assert(runtime.prepare(root.c_str()) && runtime.run());
  }
  findLine("providers activation mode=demand selected=1");
  findLine("provider deferred id=startup-failure reason=demand-until-acquired");
  for(const auto& line:lines)assert(line.find("provider start begin")==std::string::npos);
#endif
  RiscPerf::configure(clockUs,owner,true);
  for(uint32_t i=0;i<RiscPerf::Capacity+23;++i)RiscPerf::emit(RiscPerf::Counter,i);
  assert(RiscPerf::data.count==RiscPerf::Capacity && RiscPerf::data.lost==23);
  const auto overflow=records();assert(overflow.front().value==23 && overflow.back().value==RiscPerf::Capacity+22);
  assert(RiscPerf::data.counts[RiscPerf::Counter]==RiscPerf::Capacity+23);
  puts("Performance production Runtime: lifecycle order, timing, owner/disabled, bounded overflow, failed child fallback, cross-app interaction and fresh invocation PASS");
}
