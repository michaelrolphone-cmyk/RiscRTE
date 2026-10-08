#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifndef RISC_PERFORMANCE_TRACE
#define RISC_PERFORMANCE_TRACE 0
#endif
namespace RiscPerf {
enum Phase : uint32_t {
  InteractionBegin=1, Recognized=2, Dispatched=3, FirstDraw=4, PresentSubmit=5,
  Complete=6, InteractionEnd=7, Counter=8, Invocation=9,
  PrepareBegin=10, PrepareEnd=11, AppLoadBegin=12, AppLoadEnd=13,
  InitBegin=14, InitEnd=15, Entry=16, Return=17, UnloadBegin=18, UnloadEnd=19,
  LaunchRequested=20, AppLoadFail=21, ProviderAcquireBegin=22, ProviderAcquireEnd=23,
  ProviderStartBegin=24, ProviderStartEnd=25, SchedulerWait=26, ProviderPoll=27,
  MetadataReadBegin=28, MetadataReadEnd=29, ParseBegin=30, ParseEnd=31, NativeProviderWait=32, MetadataReadWait=33
};
}
#if defined(ESP_PLATFORM) && !RISC_PERFORMANCE_TRACE
// Production opt-out has no recorder storage, clocks or owner checks. Host
// regressions retain the configurable implementation for enabled/disabled tests.
namespace RiscPerf {
inline bool allowed(){return false;}
inline void configure(uint64_t(*)(),bool(*)(),bool){}
inline uint64_t now(){return 0;}
inline uint32_t identity(const char*){return 0;}
inline void emit(uint32_t,uint32_t=0){}
inline void aggregate(uint32_t,uint32_t=1,uint64_t=0){}
inline uint32_t interaction(uint32_t,uint32_t,uint32_t=0){return 0;}
inline void invocation(const char*){}
inline void finish(uint32_t,uint32_t,uint64_t,uint32_t=0){}
class Scope {public:Scope(uint32_t,uint32_t,uint32_t=0){} void result(uint32_t){}};
class AggregateScope {public:explicit AggregateScope(uint32_t,uint32_t=1){}};
}
#else
// Owner-task-only, fixed-storage recorder. No allocation or transport in emit.
namespace RiscPerf {
constexpr uint32_t Capacity=128, PhaseCapacity=64;

struct Record { uint64_t timestamp_us; uint32_t interaction,invocation,phase,value,sequence; };
struct Data {
  Record records[Capacity]{};
  uint32_t counts[PhaseCapacity]{};
  uint64_t values[PhaseCapacity]{},durations_us[PhaseCapacity]{};
  uint32_t head=0,count=0,lost=0,next=1,currentInteraction=0,nextInteraction=1,currentInvocation=0;
  uint64_t overhead_total_us=0,overhead_min_us=UINT64_MAX,overhead_max_us=0;
  uint32_t overhead_count=0;
};
inline Data data{};
inline uint64_t (*clockUs)()=nullptr;
inline bool (*ownerTask)()=nullptr;
inline bool enabled=false;
inline void saturate(uint32_t& n){if(n!=UINT32_MAX)++n;}
inline void configure(uint64_t(*clock)(),bool(*owner)(),bool on){
  enabled=false;data=Data{};clockUs=clock;ownerTask=owner;enabled=on && clock && owner;
}
inline bool allowed(){return enabled && ownerTask && ownerTask();}
inline void record(uint32_t phase,uint32_t value){
  const uint64_t started=clockUs();
  if(data.next==UINT32_MAX){saturate(data.lost);return;}
  data.records[data.head]={started,data.currentInteraction,data.currentInvocation,phase,value,data.next++};
  data.head=(data.head+1)%Capacity;
  if(data.count<Capacity)++data.count;else saturate(data.lost);
  if(phase<PhaseCapacity)saturate(data.counts[phase]);
  const uint64_t ended=clockUs();
  const uint64_t elapsed=ended>=started?ended-started:0;
  if(elapsed<data.overhead_min_us)data.overhead_min_us=elapsed;
  if(elapsed>data.overhead_max_us)data.overhead_max_us=elapsed;
  if(UINT64_MAX-data.overhead_total_us<elapsed)data.overhead_total_us=UINT64_MAX;
  else data.overhead_total_us+=elapsed;
  saturate(data.overhead_count);
}
inline uint64_t now(){return allowed()?clockUs():0;}
inline void add(uint64_t& sum,uint64_t value){sum=UINT64_MAX-sum<value?UINT64_MAX:sum+value;}
inline void aggregate(uint32_t phase,uint32_t value=1,uint64_t duration=0){
  if(!allowed() || phase>=PhaseCapacity)return;
  saturate(data.counts[phase]);add(data.values[phase],value);add(data.durations_us[phase],duration);
}
class AggregateScope {
 public:
  explicit AggregateScope(uint32_t phase,uint32_t value=1):phase_(phase),value_(value),active_(allowed()),start_(active_?clockUs():0){}
  ~AggregateScope(){if(active_ && allowed()){const auto end=clockUs();aggregate(phase_,value_,end>=start_?end-start_:0);}}
  AggregateScope(const AggregateScope&)=delete;
  AggregateScope& operator=(const AggregateScope&)=delete;
 private:uint32_t phase_,value_;bool active_;uint64_t start_;
};
inline void emit(uint32_t phase,uint32_t value=0){if(allowed())record(phase,value);}
inline uint32_t interaction(uint32_t id,uint32_t phase,uint32_t value=0){
  if(!allowed() || !phase || (phase>Counter && phase!=60 && phase!=61))return 0;
  if(phase==InteractionBegin){
    if(id || data.nextInteraction==UINT32_MAX)return 0;
    data.currentInteraction=data.nextInteraction++;
  }else if(id && id!=data.currentInteraction)return 0;
  const uint32_t result=data.currentInteraction;
  record(phase,value);
  if(phase==InteractionEnd)data.currentInteraction=0;
  return result;
}
// Diagnostic identity only: bounded names, never file bytes or an admission gate.
inline uint32_t identity(const char* subject){
  if(!allowed())return 0;
  uint32_t hash=2166136261u;
  if(subject)for(unsigned i=0;i<128 && subject[i];++i)hash=(hash^uint8_t(subject[i]))*16777619u;
  return hash;
}
inline void invocation(const char* subject){
  if(!allowed())return;
  saturate(data.currentInvocation);
  record(Invocation,identity(subject));
}
inline void finish(uint32_t begin,uint32_t end,uint64_t started,uint32_t value=0){
  if(!allowed())return;
  const auto ended=clockUs();
  if(begin<PhaseCapacity)add(data.durations_us[begin],ended>=started?ended-started:0);
  record(end,value);
}

class Scope {
 public:
  Scope(uint32_t begin,uint32_t end,uint32_t value=0):begin_(begin),end_(end),value_(value),active_(allowed()),start_(active_?clockUs():0){
    if(active_)record(begin,value);
  }
  void result(uint32_t value){value_=value;}
  ~Scope(){if(active_ && allowed()){const auto end=clockUs();if(begin_<PhaseCapacity)add(data.durations_us[begin_],end>=start_?end-start_:0);record(end_,value_);}}
  Scope(const Scope&)=delete;
  Scope& operator=(const Scope&)=delete;
 private: uint32_t begin_,end_,value_;bool active_;uint64_t start_;
};
// Exact "perf" line snapshots on the owner task. Replay is independently bounded
// and never restarts an active dump. Transport must itself honor writable().
class Replay {
 public:
  void disconnect(){used_=pending_=offset_=index_=0;discard_=cr_=active_=false;}
  void input(char ch){
    if(ch=='\r'){if(cr_)discard_=true;cr_=true;return;}
    if(ch=='\n'){
      if(!discard_ && used_==4 && std::memcmp(command_,"perf",4)==0 && !active_ && allowed()){
        snapshot_=data;active_=true;index_=pending_=offset_=0;
      }
      used_=0;discard_=cr_=false;return;
    }
    if(cr_)discard_=true;
    if(used_<sizeof(command_))command_[used_++]=ch;else discard_=true;
  }
  bool active()const{return active_;}
  template<class Transport>void poll(Transport& out){
    if(!out.connected()){disconnect();return;}
    if(!active_)return;
    if(!pending_)prepare();
    if(!pending_)return;
    size_t n=pending_-offset_,available=out.writable();
    if(n>64)n=64;
    if(n>available)n=available;
    if(!n)return;
    size_t written=out.write(reinterpret_cast<const uint8_t*>(line_)+offset_,n);
    if(written>n)written=n;
    offset_+=written;
    if(offset_==pending_){pending_=offset_=0;if(++index_>snapshot_.count+PhaseCapacity+1)active_=false;}
  }
 private:
  void prepare(){
    int n=0;
    if(index_==0)n=std::snprintf(line_,sizeof(line_),"RTE_PERF begin schema=1 records=%lu lost=%lu overhead_count=%lu overhead_min_us=%llu overhead_max_us=%llu overhead_total_us=%llu\n",
      (unsigned long)snapshot_.count,(unsigned long)snapshot_.lost,(unsigned long)snapshot_.overhead_count,
      (unsigned long long)(snapshot_.overhead_count?snapshot_.overhead_min_us:0),
      (unsigned long long)snapshot_.overhead_max_us,(unsigned long long)snapshot_.overhead_total_us);
    else if(index_<=snapshot_.count){
      const auto& r=snapshot_.records[(snapshot_.head+Capacity-snapshot_.count+index_-1)%Capacity];
      n=std::snprintf(line_,sizeof(line_),"RTE_PERF seq=%lu us=%llu interaction=%lu invocation=%lu phase=%lu value=%lu\n",
        (unsigned long)r.sequence,(unsigned long long)r.timestamp_us,(unsigned long)r.interaction,
        (unsigned long)r.invocation,(unsigned long)r.phase,(unsigned long)r.value);
    }else if(index_<=snapshot_.count+PhaseCapacity){
      const auto phase=index_-snapshot_.count-1;
      n=std::snprintf(line_,sizeof(line_),"RTE_PERF counter phase=%lu count=%lu value_total=%llu duration_us=%llu\n",(unsigned long)phase,(unsigned long)snapshot_.counts[phase],(unsigned long long)snapshot_.values[phase],(unsigned long long)snapshot_.durations_us[phase]);
    }else n=std::snprintf(line_,sizeof(line_),"RTE_PERF end\n");
    if(n>0 && size_t(n)<sizeof(line_))pending_=size_t(n);else disconnect();
  }
  Data snapshot_{};
  char command_[4]{},line_[256]{};
  size_t used_=0,pending_=0,offset_=0,index_=0;
  bool discard_=false,cr_=false,active_=false;
};
}

#endif
