#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main existing_storage_test_main
#include "storage_files_adapter_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "bootstrap/RuntimeFiles.h"
#include "diagnostics/SerialStat.h"
#include <cassert>
static bool own=true,safe=true;
static bool owner(){return own;}
static bool boundary(){return safe;}
static RiscBoot::StatObservation dirty(){return {UINT32_MAX,9,9,9,9,9};}
static void empty(const RiscBoot::StatObservation&d,uint32_t flags){assert(d.flags==flags&&d.stateCalls==0&&d.preState==INT32_MIN&&d.postState==INT32_MIN&&d.finalState==INT32_MIN&&d.metadataStatus==INT32_MIN);}
int main(){
 using namespace RiscBoot;
 auto st=statBuffer();StatObservation d=dirty();RuntimeFiles unbound;
 assert(unbound.stat("/file",&st,&d)==-3);empty(d,0);
 unbound.retainCustody();d=dirty();assert(unbound.stat("/file",&st,&d)==-6);empty(d,StatRetained); // bound was not evaluated
 GenericProvider p;RuntimeFiles files;
 // Existing ABI prefix is sufficient. There is no suffix/size requirement.
 auto shortApi=p.api;--shortApi.struct_size;assert(!files.bind(&shortApi,owner,boundary));
 assert(files.bind(&p.api,owner,boundary));
 d=dirty();assert(files.stat(nullptr,&st,&d)==-1);empty(d,StatBound);assert(p.totalCalls()==0);
 own=false;d=dirty();assert(files.stat("/file",&st,&d)==-7);empty(d,StatBound);assert(p.totalCalls()==0);own=true;
 safe=false;d=dirty();assert(files.stat("/file",&st,&d)==-4);empty(d,StatBound);assert(p.totalCalls()==0);safe=true;
 assert(files.suspendSleep());d=dirty();assert(files.stat("/file",&st,&d)==-4);empty(d,StatBound);assert(files.resumeSleep());
 d=dirty();assert(files.stat("/file",&st,&d)==0&&st.size==6);assert(d.flags==(StatBound|StatMetadata)&&d.stateCalls==0&&d.metadataStatus==0&&d.preState==INT32_MIN&&d.postState==INT32_MIN&&d.finalState==INT32_MIN&&p.totalCalls()==1);
 const auto previous=d;assert(files.stat("/file",&st)==0&&p.totalCalls()==2);assert(!std::memcmp(&d,&previous,sizeof(d))); // no retained output pointer
 d=dirty();assert(files.stat("/absent",&st,&d)==1&&p.totalCalls()==3&&d.metadataStatus==1&&d.stateCalls==0);
 p.results["stat"]=INT32_MIN;d=dirty();assert(files.stat("/file",&st,&d)==-2&&p.totalCalls()==4&&d.metadataStatus==INT32_MIN&&(d.flags&StatMetadata));
 p.results["stat"]=-6;d=dirty();assert(files.stat("/file",&st,&d)==-6&&files.retained()&&p.totalCalls()==5&&d.metadataStatus==-6);
 d=dirty();assert(files.stat("/file",&st,&d)==-6&&p.totalCalls()==5);empty(d,StatRetained);
 // All captures are copied values; first retained survives later accepted calls.
 RiscDiagnostics::SerialStatSnapshot snapshot{};
 assert(RiscDiagnostics::statLines(snapshot)==0);
 RiscDiagnostics::observeStat(snapshot,10,1,2,12,-6,d);const auto first=snapshot.firstRetained;d=dirty();
 RiscDiagnostics::observeStat(snapshot,20,3,4,11,0,d);assert(!std::memcmp(&snapshot.firstRetained,&first,sizeof(first)));
 for(unsigned i=0;i<100;++i)RiscDiagnostics::observeStat(snapshot,30+i,3,5+i,12,-6,d);
 assert(!std::memcmp(&snapshot.firstRetained,&first,sizeof(first)));
 // Maximum-width values remain inside the existing replay line buffer.
 snapshot.current={UINT64_MAX,{UINT32_MAX,INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN,UINT32_MAX},UINT32_MAX,UINT32_MAX,INT32_MIN,UINT32_MAX};snapshot.firstRetained=snapshot.current;
 for(unsigned i=0;i<4;++i){char line[192];const int n=RiscDiagnostics::statLine(snapshot,i,line,sizeof(line));assert(n>0&&n<int(sizeof(line))&&line[n-1]=='\n');std::printf("line[%u]=%d bytes\n",i,n);}
 std::printf("STAT unit PASS: initialized sentinels, existing ABI prefix, omitted capture, no surviving pointer, context/sleep/invalid/retained entry, generic metadata status, first-retained immutable\n");
}
