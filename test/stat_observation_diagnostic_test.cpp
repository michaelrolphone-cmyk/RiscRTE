// Host adapter test using the production source and existing Arduino shim.
#include "ports/esp32s3/SleepDiagnostics.cpp"
#include <cassert>
#include <string>
static std::string dump(){Serial.output.clear();Serial.input="diag\n";for(unsigned i=0;i<2000;++i)RiscDiagnostics::poll();assert(Serial.output.find("RTE_DIAG end\n")!=std::string::npos);return Serial.output;}
int main(){
 using namespace RiscDiagnostics;
 auto d=RiscBoot::emptyStatObservation();
 serialStat(1,1,12,-3,d); // no owner yet: no write
 start();Serial.connected=true;assert(dump().find("RTE_DIAG stat ")==std::string::npos);
 now=700;serialStat(0x3b6e3f8a,5,12,-3,d);const auto original=statState();d.flags=99;
 auto text=dump();assert(text.find("current ns=12 session=3b6e3f8a sequence=5 status=-3 ms=700")!=std::string::npos);assert(text.find("flags=0 pre=-2147483648 metadata=-2147483648 post=-2147483648 final=-2147483648 state_calls=0")!=std::string::npos);
 task=reinterpret_cast<void*>(2);serialStat(1,2,11,0,d);task=reinterpret_cast<void*>(1);
 diagnosticIsr=true;serialStat(1,2,11,0,d);diagnosticIsr=false;assert(!std::memcmp(&original,&statState(),sizeof(original)));
 Serial.connected=false;poll();Serial.connected=true;assert(dump()==text); // disconnect does not clear accepted evidence
 // In-flight replay is an immutable copy, while a subsequent STAT updates live storage.
 Serial.output.clear();Serial.input="diag\n";poll();serialStat(1,8,11,-6,d);
 for(unsigned i=0;i<2000;++i)poll();assert(Serial.output==text);
 const auto first=statState().firstRetained;serialStat(2,9,12,0,d);serialStat(2,10,12,-6,d);
 assert(!std::memcmp(&first,&statState().firstRetained,sizeof(first)));
 const auto live=statState();for(unsigned i=0;i<1000;++i)poll();assert(!std::memcmp(&live,&statState(),sizeof(live)));
 text=dump();assert(text.find("first-retained ns=11 session=00000001 sequence=8 status=-6")!=std::string::npos&&text.find("current ns=12 session=00000002 sequence=10 status=-6")!=std::string::npos);
 start();assert(dump().find("RTE_DIAG stat ")==std::string::npos);
 std::printf("STAT production diagnostic adapter PASS: owner/ISR, synchronous copy, reconnect, immutable replay, no polling overwrite, first-retained, startup absence/reset; replay_bytes=%zu snapshot_bytes=%zu\n",sizeof(Replay),sizeof(SerialStatSnapshot));
}
