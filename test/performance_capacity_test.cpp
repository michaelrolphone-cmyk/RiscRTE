#include "diagnostics/Performance.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace RiscPerf;
static uint64_t ticks;
static uint64_t clockTest(){return ++ticks;}
static bool ownerTest(){return true;}
struct Transport {
 std::string output;unsigned calls=0;
 bool connected(){return true;}size_t writable(){return 19;}
 size_t write(const uint8_t* bytes,size_t size){assert(size<=19&&size<=64);++calls;const auto n=size<7?size:7;output.append(reinterpret_cast<const char*>(bytes),n);return n;}
};
int main(){
 static_assert(PhaseCapacity==64,"all cumulative phases remain available");
 static_assert(sizeof(Record)==32,"record footprint proof changed");
 configure(clockTest,ownerTest,true);
 for(unsigned phase=0;phase<64;++phase)aggregate(phase,phase+1,(phase+1)*100);
 emit(FirstDraw);
 for(unsigned batch=0;batch<10;++batch)for(unsigned tag=0x40;tag<=0x4e;++tag)emit(Counter,(tag<<24)|batch);
 for(unsigned i=0;i<7;++i)emit(Dispatched,i);
 assert(data.count==Capacity && data.lost==158-Capacity);
 unsigned groups=0,expected=0;
 for(unsigned i=0;i<data.count;++i){
  const auto& record=data.records[(data.head+Capacity-data.count+i)%Capacity];
  const unsigned tag=record.phase==Counter?record.value>>24:0;
  if(tag==0x40)expected=0x41;
  else if(expected && tag==expected){if(tag==0x4e){++groups;expected=0;}else ++expected;}
  else expected=0;
 }
 assert(groups>=3); //64slots retain three whole15-record groups in this mixed case.
 for(unsigned phase=0;phase<64;++phase){assert(data.values[phase]==phase+1&&data.durations_us[phase]==(phase+1)*100);}
 Replay replay;for(char c:std::string("perf\n"))replay.input(c);
 Transport transport;while(replay.active())replay.poll(transport);
 assert(transport.output.find("schema=1 records="+std::to_string(Capacity))!=std::string::npos);
 assert(transport.output.find("RTE_PERF end\n")!=std::string::npos);
 unsigned counters=0;size_t at=0;while((at=transport.output.find("RTE_PERF counter phase=",at))!=std::string::npos){++counters;++at;}
 assert(counters==64);
 printf("Performance capacity=%u data_bytes=%zu replay_bytes=%zu record_bytes=%zu complete_app_groups=%u all_phase_counters=64 bounded_partial_replay=PASS\n",Capacity,sizeof(Data),sizeof(Replay),sizeof(Record),groups);
}
