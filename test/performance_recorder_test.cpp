#include "diagnostics/Performance.h"
#include "RiscPerformanceV1.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
using namespace RiscPerf;
static uint64_t ticks=0;static unsigned clocks=0,owners=0;static bool owned=true;
static uint64_t clockTest(){++clocks;return ticks+=3;}
static uint64_t realClock(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static bool ownerTest(){++owners;return owned;}
static const Record& at(unsigned i){return data.records[(data.head+Capacity-data.count+i)%Capacity];}
struct Transport {
 bool connected_=true;size_t capacity=9,partial=3,calls=0;std::string output;
 bool connected(){return connected_;}size_t writable(){return capacity;}
 size_t write(const uint8_t* p,size_t n){assert(n<=64 && n<=capacity);++calls;n=n<partial?n:partial;output.append(reinterpret_cast<const char*>(p),n);return n;}
};
static void command(Replay& replay,const char* s){for(;*s;++s)replay.input(*s);}
int main(){
 configure(clockTest,ownerTest,false);emit(1);interaction(0,1);invocation("ignored");aggregate(26);{Scope s(10,11);AggregateScope a(26);}assert(now()==0 && clocks==0 && owners==0 && data.count==0);
 configure(clockTest,ownerTest,true);
 const auto id=interaction(0,InteractionBegin,1);assert(id!=0);
 emit(Recognized);invocation("launcher.elf");{Scope s(AppLoadBegin,AppLoadEnd);}
 invocation("child.elf");emit(FirstDraw);assert(interaction(id+1,Complete)==0);
 assert(interaction(0,AppLoadBegin)==0);
 assert(data.durations_us[AppLoadBegin]>0);
 assert(data.count==7 && at(0).phase==1 && at(1).phase==2 && at(3).phase==12 && at(4).phase==13);
 for(unsigned i=0;i<data.count;++i){assert(at(i).interaction==id);if(i)assert(at(i).timestamp_us>at(i-1).timestamp_us);}
 assert(at(6).invocation==2 && data.overhead_count==7 && data.overhead_min_us==3 && data.overhead_max_us==3 && data.overhead_total_us==21);
 const auto before=data.count;for(unsigned i=0;i<1000;++i){AggregateScope scope(26,2);}assert(data.count==before && data.counts[26]==1000 && data.values[26]==2000 && data.durations_us[26]==3000);
 assert(interaction(0,InteractionEnd)==id && at(data.count-1).interaction==id);emit(Counter);assert(at(data.count-1).interaction==0);
 for(unsigned phase=9;phase<60;++phase)assert(interaction(0,phase)==0);
 assert(interaction(0,62)==0 && interaction(0,63)==0 && interaction(0,UINT32_MAX)==0);
 const auto spanId=interaction(0,1,3);interaction(spanId,60,1);interaction(spanId,60,2);interaction(spanId,61,2);interaction(spanId,61,1);
 assert(at(data.count-4).phase==60 && at(data.count-1).phase==61 && at(data.count-4).value==at(data.count-1).value && at(data.count-1).timestamp_us>at(data.count-4).timestamp_us);
 owned=false;const auto seq=data.next;emit(1);aggregate(26);assert(data.next==seq);owned=true;
 configure(clockTest,ownerTest,true);for(unsigned i=0;i<Capacity+10;++i)emit(Counter,i);
 assert(data.count==Capacity && data.lost==10 && data.counts[Counter]==Capacity+10 && at(0).sequence==11 && at(Capacity-1).value==Capacity+9);
 data.counts[Counter]=UINT32_MAX;data.values[26]=UINT64_MAX;data.durations_us[26]=UINT64_MAX;emit(Counter);aggregate(26,3,4);assert(data.counts[Counter]==UINT32_MAX && data.values[26]==UINT64_MAX && data.durations_us[26]==UINT64_MAX);
 Replay replay;Transport transport;command(replay,"perfx\nPERF\nperf\rX\n");assert(!replay.active());command(replay,"perf\r\n");assert(replay.active());const auto snapshotCount=data.counts[Counter];emit(Counter);command(replay,"perf\n");
 transport.capacity=0;replay.poll(transport);assert(transport.calls==0);transport.capacity=9;
 unsigned polls=0;while(replay.active() && ++polls<20000)replay.poll(transport);assert(!replay.active());assert(transport.output.find("RTE_PERF begin schema=1") ==0 && transport.output.find("RTE_PERF end\n")!=std::string::npos);assert(snapshotCount==UINT32_MAX);
 command(replay,"perf\n");replay.poll(transport);transport.connected_=false;replay.poll(transport);assert(!replay.active());transport.connected_=true;command(replay,"perf\n");assert(replay.active());
 risc_runtime_api_v1 api{};api.api_version=1;api.struct_size=RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE;api.trace=interaction;assert(risc_perf_trace_v1(&api,0,1,1)==0);api.struct_size=sizeof(api);assert(risc_perf_trace_v1(&api,0,1,1)!=0);
 configure(realClock,ownerTest,true);constexpr unsigned N=1000000;const auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<N;++i)emit(Counter,i);const auto elapsed=std::chrono::steady_clock::now()-start;
 std::cout<<"performance recorder passed; host steady-clock emit mean ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()/N<<" data_bytes="<<sizeof(Data)<<" replay_bytes="<<sizeof(Replay)<<"\n";
}
