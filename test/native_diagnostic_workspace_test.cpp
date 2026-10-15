// Include the production adapter to inspect its private storage without adding
// a firmware test hook. SDK/transport calls below remain host-only substitutes.
#include "ports/esp32s3/SleepDiagnostics.cpp"
#include <cassert>
#include <cstring>
#include <string>
#include <algorithm>
namespace {
void* currentTask=reinterpret_cast<void*>(1);
bool inIsr=false,connected=true;
uint64_t ticks=1000;
size_t writeLimit=64,space=64,reads=0,writes=0;
std::string input,output;
int resetReason=ESP_RST_POWERON;
bool owned(){return currentTask==reinterpret_cast<void*>(1);}
uint64_t clockUs(){return ++ticks;}
}
extern "C" {
TaskHandle_t xTaskGetCurrentTaskHandle(){return currentTask;}
bool xPortInIsrContext(){return inIsr;}
int64_t esp_timer_get_time(){return int64_t(++ticks);}
int esp_reset_reason(){return resetReason;}
int esp_sleep_get_wakeup_cause(){return 0;}
}
namespace RiscPlatform {
Console console;
Console::operator bool()const{return connected;}
int Console::available(){return int(input.size());}
int Console::read(){++reads;if(input.empty())return -1;int c=input.front();input.erase(0,1);return c;}
int Console::availableForWrite()const{return int(space);}
size_t Console::write(const uint8_t* bytes,size_t count){
  assert(connected && count<=space && count<=64);++writes;
  count=std::min(count,writeLimit);output.append(reinterpret_cast<const char*>(bytes),count);return count;
}
}
namespace {
void step(){
 const auto before=output.size(),beforeReads=reads,beforeWrites=writes;
 RiscDiagnostics::poll();
 assert(output.size()-before<=64 && reads-beforeReads<=16 && writes-beforeWrites<=1);
}
std::string dump(const char* command,const char* end){
 output.clear();input=command;
 for(unsigned i=0;i<20000 && output.find(end)==std::string::npos;++i)step();
 assert(output.find(end)!=std::string::npos);return output;
}
void checkConstructed(){
 using namespace RiscDiagnostics;
 assert(reinterpret_cast<uintptr_t>(&replayWorkspace())%alignof(ReplayWorkspace)==0);
#if RISC_SLEEP_DIAGNOSTICS
 assert(!sleepReplay().active());
 assert(checkpointState().sequence==0 && checkpointState().invocation==0 && checkpointState().length==0);
#endif
#if RISC_PERFORMANCE_TRACE
 assert(!perfReplay().active());
 // Replay's first member is the copied Data snapshot. Inspect its representation
 // to catch accidentally dropping nonzero initializers into a NOLOAD section.
 static_assert(std::is_standard_layout<RiscPerf::Replay>::value);
 static_assert(std::is_trivially_copyable<RiscPerf::Data>::value);
 RiscPerf::Data snapshot;
 std::memcpy(static_cast<void*>(&snapshot),static_cast<const void*>(&perfReplay()),sizeof(snapshot));
 assert(snapshot.next==1 && snapshot.nextInteraction==1 && snapshot.overhead_min_us==UINT64_MAX);
 assert(snapshot.count==0 && snapshot.head==0 && snapshot.overhead_count==0);
#endif
}
}
int main(){
 using namespace RiscDiagnostics;
 static_assert(RISC_NATIVE_ESP_IDF && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY && CONFIG_SPIRAM_BOOT_INIT);
 std::memset(replayStorage,0xa5,sizeof(replayStorage));
 poll();line("before owner");lightReturn(-1,0);
 for(auto byte:replayStorage)assert(byte==0xa5);
 start();checkConstructed();
#if RISC_SLEEP_DIAGNOSTICS
 const std::string app(CheckpointApplicationMax,'a'),text(CheckpointTextMax,'b');
 assert(checkpoint(app.c_str(),7,text.c_str(),uint32_t(text.size()))==0);
 const auto expected=dump("diag\n","RTE_DIAG end\n");
 assert(expected.find("length=768 truncated=0")!=std::string::npos);
 for(auto limit:{size_t(1),size_t(7),size_t(63)}){writeLimit=limit;assert(dump("diag\n","RTE_DIAG end\n")==expected);}
 writeLimit=64;
 input="diag\n";output.clear();step();
 assert(checkpoint("new.elf",8,"later",5)==0);
 for(unsigned i=0;i<20000 && output.find("RTE_DIAG end\n")==std::string::npos;++i)step();
 assert(output==expected); // Replay owns its immutable complete checkpoint copy.
 inIsr=true;assert(checkpoint("isr.elf",9,"bad",3)==-1);inIsr=false;
#endif
#if RISC_PERFORMANCE_TRACE
 RiscPerf::configure(clockUs,owned,true);
 for(unsigned i=0;i<RiscPerf::Capacity+9;++i)RiscPerf::emit(RiscPerf::Counter,i);
 for(unsigned phase=0;phase<RiscPerf::PhaseCapacity;++phase)RiscPerf::aggregate(phase,phase+1,phase+10);
 const auto perfExpected=dump("perf\n","RTE_PERF end\n");
 assert(perfExpected.find("records="+std::to_string(RiscPerf::Capacity)+" lost=9")!=std::string::npos);
 size_t counters=0,at=0;while((at=perfExpected.find("RTE_PERF counter phase=",at))!=std::string::npos){++counters;++at;}
 assert(counters==64);
 for(auto limit:{size_t(1),size_t(7),size_t(63)}){writeLimit=limit;assert(dump("perf\n","RTE_PERF end\n")==perfExpected);}
 writeLimit=64;
 input="perf\n";output.clear();step();RiscPerf::emit(RiscPerf::Counter,0xabcdef);
 for(unsigned i=0;i<20000 && output.find("RTE_PERF end\n")==std::string::npos;++i)step();
 assert(output==perfExpected);
#endif
#if RISC_SLEEP_DIAGNOSTICS && RISC_PERFORMANCE_TRACE
 for(const char* command:{"diag\nperf\n","perf\ndiag\n"}){
  input=command;output.clear();for(unsigned i=0;i<20000;++i)step();
  assert(output.find(command[0]=='d'?"RTE_PERF":"RTE_DIAG")==std::string::npos);
 }
#endif
 currentTask=reinterpret_cast<void*>(2);input="diag\nperf\n";
 const auto beforeReads=reads,beforeWrites=writes;poll();line("foreign");
 assert(reads==beforeReads && writes==beforeWrites);
 currentTask=reinterpret_cast<void*>(1);input.clear();
 // Repeated startup reconstructs every default even over an old snapshot; RTC
 // evidence retains its independent reset policy and the workspace never owns heap memory.
 for(unsigned i=0;i<8;++i){
  resetReason=ESP_RST_SW;std::memset(replayStorage,0xa5,sizeof(replayStorage));
  start();checkConstructed();
#if RISC_SLEEP_DIAGNOSTICS
  assert(valid(journal) && journal.boot==i+2);
#endif
 }
 std::printf("Native diagnostic workspace: diagnostics=%d performance=%d capacity=%u fixed_bytes=%zu; construction, complete payload, snapshot custody, partial replay, owner/ISR gates and repeated start PASS\n",RISC_SLEEP_DIAGNOSTICS,RISC_PERFORMANCE_TRACE,unsigned(RISC_PERFORMANCE_TRACE_CAPACITY),sizeof(replayStorage));
}
