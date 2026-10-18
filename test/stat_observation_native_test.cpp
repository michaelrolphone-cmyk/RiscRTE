// Fresh native owner/transport test. ESP-IDF host shims; no hardware model claim.
#include "ports/esp32s3/NativeSerialFiles.cpp"
#include "ports/esp32s3/SleepDiagnostics.cpp"
#include <cassert>
#include <cstring>
#include <string>
#include <algorithm>
namespace {void* currentTask=reinterpret_cast<void*>(1);bool inIsr=false,connected=true;uint64_t ticks=1000;size_t partial=64,space=64,reads=0,writes=0;std::string input,output;bool owned(){return currentTask==reinterpret_cast<void*>(1);}bool safe(){return true;}}
extern "C" {
TaskHandle_t xTaskGetCurrentTaskHandle(){return currentTask;}
bool xPortInIsrContext(){return inIsr;}
int64_t esp_timer_get_time(){return int64_t(++ticks);}
int esp_reset_reason(){return ESP_RST_POWERON;}
int esp_sleep_get_wakeup_cause(){return 0;}
}
namespace RiscPlatform {
Console console;
Console::operator bool()const{return connected;}
int Console::available(){return int(input.size());}
int Console::read(){++reads;if(input.empty())return -1;int c=input.front();input.erase(0,1);return c;}
int Console::availableForWrite()const{return int(space);}
size_t Console::write(const uint8_t*p,size_t n){assert(connected&&n<=space&&n<=64);++writes;n=std::min(n,partial);output.append(reinterpret_cast<const char*>(p),n);return n;}
size_t Console::setTxBufferSize(size_t n){return n;}
}
static unsigned captures;
static void capture(uint32_t sid,uint32_t seq,uint64_t ns,int32_t rc,const RiscBoot::StatObservation&d){++captures;RiscDiagnostics::serialStat(sid,seq,ns,rc,d);}
static void step(){const auto r=reads,w=writes,n=output.size();RiscDiagnostics::poll();assert(reads-r<=16&&writes-w<=1&&output.size()-n<=64);}
static std::string command(const char*request){
 namespace N=RiscCpu::NativeSerialFiles;
 for(const char*p=request;*p;++p)N::input(uint8_t(*p));assert(N::checkpoint());
 std::string s;while(N::outputSize()){s.append(N::output(),N::outputSize());N::consumeOutput(N::outputSize());}return s;
}
static std::string dump(){input="diag\n";output.clear();for(unsigned i=0;i<20000&&output.find("RTE_DIAG end\n")==std::string::npos;++i)step();assert(output.find("RTE_DIAG end\n")!=std::string::npos);return output;}
int main(){
 namespace N=RiscCpu::NativeSerialFiles;using namespace RiscDiagnostics;
 start();RiscBoot::RuntimeFiles files;RiscSerial::Namespace namespaces[]={{11,"Internal",&files,true},{12,"SD",&files,true}};N::Admission admission{namespaces,2,safe};assert(N::prepare(&admission,owned,safe,nullptr,capture));
 assert(dump().find("RTE_DIAG stat ")==std::string::npos);
 assert(command("RTEF1 3b6e3f8a 1 HELLO\n").find(" 0 HELLO ")!=std::string::npos);
 assert(command("RTEF1 3b6e3f8a 2 NS 0\n").find(" 0 NS ")!=std::string::npos);
 assert(command("RTEF1 3b6e3f8a 3 NS 1\n").find(" 0 NS ")!=std::string::npos);
 assert(command("RTEF1 3b6e3f8a 4 PATH 0 2f417070732f616c61726d732f616c61726d732e656c66\n").find(" 0 PATH ")!=std::string::npos);
 const auto wire=command("RTEF1 3b6e3f8a 5 STAT 12\n");assert(wire=="RTEF1 3b6e3f8a 5 -3 STAT 0 0\n"&&captures==1);
 assert(command("RTEF1 3b6e3f8a 5 STAT 12\n")==wire&&captures==1);
 const auto saved=statState();assert(saved.current.nameSpace==12&&saved.current.decision.preState==INT32_MIN&&!saved.firstRetained.nameSpace);
 connected=false;step();connected=true;const auto expected=dump();assert(captures==1&&!std::memcmp(&saved,&statState(),sizeof(saved)));
 assert(expected.find("current ns=12 session=3b6e3f8a sequence=5 status=-3")!=std::string::npos);
 for(auto limit:{size_t(1),size_t(7),size_t(63)}){partial=limit;assert(dump()==expected);}partial=64;
 currentTask=reinterpret_cast<void*>(2);serialStat(1,1,11,0,saved.current.decision);currentTask=reinterpret_cast<void*>(1);
 inIsr=true;serialStat(1,1,11,0,saved.current.decision);inIsr=false;assert(!std::memcmp(&saved,&statState(),sizeof(saved)));
 input="diag\n";output.clear();step();serialStat(2,6,11,-6,saved.current.decision);
 for(unsigned i=0;i<20000&&output.find("RTE_DIAG end\n")==std::string::npos;++i)step();assert(output==expected);
 const auto first=statState().firstRetained;serialStat(3,7,12,0,saved.current.decision);for(unsigned i=0;i<100;++i)step();assert(!std::memcmp(&first,&statState().firstRetained,sizeof(first)));
 std::printf("Native STAT PASS: callback identity/copy, duplicate, disconnect diag retrieval, bounded partial output, owner/ISR, immutable replay and first-retained; external=%d native_state=%zu endpoint_workspace=%zu diagnostic_workspace=%zu\n",CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY,sizeof(N::state),sizeof(N::workspaceStorage),sizeof(replayStorage));
}
