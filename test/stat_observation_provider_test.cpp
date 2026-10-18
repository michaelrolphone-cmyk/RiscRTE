#include "runtime/serial/Files.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#ifndef BASELINE
#include "diagnostics/Journal.h"
static RiscDiagnostics::SerialStatSnapshot snapshot{};
static unsigned captures;
static void capture(uint32_t sid,uint32_t seq,uint64_t ns,int32_t status,const RiscBoot::StatObservation& value){++captures;RiscDiagnostics::observeStat(snapshot,991,sid,seq,ns,status,value);}
#endif
extern "C" const risc_storage_volume_api_v1 *probe_start();
extern "C" void probe_mode(const char*);
extern "C" void probe_counts(unsigned*);
static bool yes(){return true;}
static uint32_t now(){return 1;}
static RiscBoot::RuntimeFiles files;
static RiscSerial::Files endpoint;
static unsigned counts[6];
static std::string command(unsigned seq,const std::string& cmd){
 const auto req="RTEF1 3b6e3f8a "+std::to_string(seq)+" "+cmd+"\n";
 for(auto c:req)endpoint.input(uint8_t(c));endpoint.checkpoint();
 std::string out;while(endpoint.outputSize()){out.append(endpoint.output(),endpoint.outputSize());endpoint.consumeOutput(endpoint.outputSize());}return out;
}
static std::string hex(const char*p){std::string out;const char*s="0123456789abcdef";while(*p){unsigned c=uint8_t(*p++);out+=s[c>>4];out+=s[c&15];}return out;}
int main(int argc,char**argv){
 assert(argc==3);const std::string mode=argv[1];const bool enabled=std::strcmp(argv[2],"on")==0;
 const auto*v=probe_start();if(mode!="unbound")assert(files.bindVolume(v,yes,yes));
 if(mode=="client-retained")files.retainCustody();
 RiscSerial::Namespace map[]={{11,"Internal",&files,true},{12,"SD",&files,true}};
 RiscSerial::Hooks hooks{yes,yes,now};
#ifndef BASELINE
 if(enabled)hooks.statObserved=capture;
#else
 (void)enabled;
#endif
 assert(endpoint.bind(map,2,hooks));
 assert(command(1,"HELLO").find(" 0 HELLO ")!=std::string::npos);
 assert(command(2,"NS 0").find(" 0 NS ")!=std::string::npos);
 assert(command(3,"NS 1").find(" 0 NS ")!=std::string::npos);
 const bool missing=mode=="healthy-missing"||mode=="post-failed-unavailable";
 assert(command(4,"PATH 0 "+hex(missing?"/Apps/alarms/missing.elf":"/Apps/alarms/alarms.elf")).find(" 0 PATH ")!=std::string::npos);
 probe_mode(mode.c_str());
 const auto wire=command(5,"STAT 12");probe_counts(counts);
 int expected=0;unsigned wanted[6]={2,1,0,3,3,0};
 if(mode=="unbound"||mode=="client-retained"){expected=mode=="unbound"?-3:-6;std::memset(wanted,0,sizeof(wanted));}
 else if(mode=="active-session"||mode=="sleep-prepared"||mode=="io-unavailable"||mode=="provider-retained"){
  expected=mode=="provider-retained"?-6:-3;std::memset(wanted,0,sizeof(wanted));wanted[0]=1;
 }else if(mode=="owner-refused"){expected=-6;unsigned temp[]={3,1,1,0,0,0};std::memcpy(wanted,temp,sizeof(temp));}
 else if(mode=="healthy-missing"){expected=1;wanted[0]=3;wanted[2]=1;}
 else if(mode=="post-unavailable")expected=-3;
 else if(mode=="post-failed-unavailable")expected=-6;
 const std::string expectedWire="RTEF1 3b6e3f8a 5 "+std::to_string(expected)+" STAT "+(expected==0?"6":"0")+" 0\n";
 if(wire!=expectedWire)std::fprintf(stderr,"wire=%s wanted=%s",wire.c_str(),expectedWire.c_str());
 assert(wire==expectedWire);assert(std::memcmp(counts,wanted,sizeof(counts))==0);assert(files.retained()==(expected==-6));
 std::printf("%s wire=%scounts=%u,%u,%u,%u,%u,%u retained=%d\n",mode.c_str(),wire.c_str(),counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],files.retained());
#ifndef BASELINE
 if(enabled){
  assert(captures==1);const auto&r=snapshot.current;const auto&d=r.decision;
  assert(r.nameSpace==12&&r.session==0x3b6e3f8a&&r.sequence==5&&r.status==expected&&r.ms==991);
  assert(d.stateCalls==counts[0]);
  const int absent=INT32_MIN;
  if(mode=="unbound"||mode=="client-retained")assert(d.flags==(mode=="unbound"?0u:2u)&&d.preState==absent&&d.metadataStatus==absent&&d.postState==absent&&d.finalState==absent);
  else if(mode=="active-session"||mode=="sleep-prepared"||mode=="io-unavailable"||mode=="provider-retained")assert(d.flags==1&&d.preState==(mode=="provider-retained"?-2:1)&&d.metadataStatus==absent&&d.postState==absent&&d.finalState==absent);
  else if(mode=="owner-refused")assert(d.flags==13&&d.preState==0&&d.metadataStatus==-2&&d.postState==0&&d.finalState==0);
  else if(mode=="healthy-missing")assert(d.flags==29&&d.preState==0&&d.metadataStatus==1&&d.postState==0&&d.finalState==0);
  else assert(d.flags==5&&d.preState==0&&d.metadataStatus==(missing?1:0)&&d.postState==(expected?1:0)&&d.finalState==absent);
  if(expected==-6)assert(std::memcmp(&snapshot.current,&snapshot.firstRetained,sizeof(r))==0);
  else assert(!snapshot.firstRetained.nameSpace);
 }
 else assert(captures==0&&!snapshot.current.nameSpace);
#endif
 // A cached duplicate is not a second provider observation, including retained results.
 const auto duplicate=command(5,"STAT 12");assert(duplicate==wire);unsigned after[6];probe_counts(after);assert(std::memcmp(after,counts,sizeof(after))==0);
#ifndef BASELINE
 assert(captures==unsigned(enabled));const auto preserved=snapshot;
#endif
 endpoint.disconnect();endpoint.checkpoint();probe_counts(after);assert(std::memcmp(after,counts,sizeof(after))==0);
#ifndef BASELINE
 assert(std::memcmp(&snapshot,&preserved,sizeof(snapshot))==0);
 if(enabled){
  // Replay after disconnect uses an owned copy. Mutating its source or reusing
  // stack storage after callback return cannot change an in-flight dump.
  RiscDiagnostics::Journal journal{};RiscDiagnostics::begin(journal,false,0,1,0);
  RiscDiagnostics::Replay replay;for(char c:std::string("diag\n"))replay.input(c,journal,nullptr,&snapshot);
  snapshot={};
  struct Wire{std::string out;bool connected(){return true;}size_t writable(){return 64;}size_t write(const uint8_t*p,size_t n){assert(n<=64);n=n>3?3:n;out.append((const char*)p,n);return n;}} transport;
  for(unsigned i=0;replay.active()&&i<10000;++i)replay.poll(transport);assert(!replay.active());
  assert(transport.out.find("RTE_DIAG stat current ns=12 session=3b6e3f8a sequence=5 status="+std::to_string(expected))!=std::string::npos);
  assert(transport.out.find("RTE_DIAG stat current flags="+std::to_string(preserved.current.decision.flags))!=std::string::npos);
  assert((transport.out.find("RTE_DIAG stat first-retained ns=12")!=std::string::npos)==(expected==-6));
  assert(transport.out.find("RTE_DIAG end\n")!=std::string::npos);assert(captures==1);
 }
#endif
}
