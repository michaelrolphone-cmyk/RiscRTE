#include "diagnostics/Journal.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace RiscDiagnostics;
struct Wire {
  bool up=true;size_t space=64,partial=64,calls=0;std::string text;
  bool connected(){return up;}
  size_t writable(){return space;}
  size_t write(const uint8_t* p,size_t n){assert(n<=space && n<=64);++calls;n=n<partial?n:partial;text.append((const char*)p,n);return n;}
};
static void request(Replay& r,const Journal& j,const char* s="diag\n"){while(*s)r.input(*s++,j);}
static void drain(Replay& r,Wire& w){unsigned n=0;while(r.active() && ++n<10000)r.poll(w);assert(!r.active());}
int main(){
  Journal j;std::memset(&j,0xa5,sizeof(j));
  assert(!begin(j,true,9,3,0));assert(valid(j)&&j.boot==1&&j.eventCount==1);
  message(j,11,"APP ready");event(j,12,LightEnter);event(j,20,LightReturn,0,7);event(j,25,DeepEnter);
  assert(begin(j,true,0,8,3));assert(j.boot==2&&j.eventCount==5&&j.messageCount==1);
  assert(j.events[0].kind==BootFresh&&j.events[4].kind==BootRetained);
  // Software/watchdog/deep reset policy is supplied by the native adapter. A
  // validated image survives; power-on/brownout/unknown policy always discards.
  Journal saved=j;assert(!begin(j,false,0,1,0));assert(j.boot==1&&j.eventCount==1&&j.messageCount==0);
  for(size_t i=0;i<sizeof(saved);++i){j=saved;reinterpret_cast<unsigned char*>(&j)[i]^=1;assert(!valid(j));assert(!begin(j,true,0,3,0));assert(valid(j));}
  j=saved;changing(j);assert(!begin(j,true,0,3,0));assert(valid(j));
  begin(j,false,0,1,0);
  for(unsigned i=0;i<40;++i)event(j,i,LightEnter);
  for(unsigned i=0;i<30;++i)message(j,i,"heartbeat");
  assert(j.eventCount==16&&j.eventLost==25&&j.messageCount==8&&j.messageLost==22);
  const auto sleepFirst=j.events[j.eventHead].sequence;
  for(unsigned i=0;i<100;++i)message(j,i,"more heartbeat");
  assert(j.events[j.eventHead].sequence==sleepFirst&&j.eventLost==25);
  message(j,100,"bad\nline\r\x01");assert(std::strcmp(j.messages[(j.messageHead+7)%8].text,"bad?line??")==0);
  std::string longText(200,'x');message(j,101,longText.c_str());
  const auto& truncated=j.messages[(j.messageHead+7)%8];assert(std::strlen(truncated.text)==95&&truncated.text[94]=='~');
  Replay r;Wire w;
  for(const auto* invalid:{"erase\n","diag extra\n","di\nag\n","xdiag\n","DIAG\n","diagx\n","di\rag\n","diag\r\r\n"}){request(r,j,invalid);assert(!r.active());}
  request(r,j,"diag\r\n");assert(r.active());const Journal snapshot=j;
  message(j,999,"after snapshot");
  w.space=0;r.poll(w);assert(r.active()&&w.calls==0);
  w.space=64;w.partial=0;r.poll(w);assert(r.active()&&w.text.empty());
  w.partial=3;request(r,j);drain(r,w);
  assert(w.text.find("RTE_DIAG begin schema=1")!=std::string::npos&&w.text.find("RTE_DIAG end\n")!=std::string::npos);
  assert(w.text.find("after snapshot")==std::string::npos&&w.text.find("event_lost=25")!=std::string::npos);
  assert(w.text.find("text=bad?line??")!=std::string::npos);
  assert(valid(snapshot)&&valid(j));
  request(r,j);r.poll(w);w.up=false;r.poll(w);assert(!r.active());
  w.up=true;w.text.clear();request(r,j);drain(r,w);assert(w.text.find("after snapshot")!=std::string::npos);
  // Partial commands do not survive a disconnected session.
  request(r,j,"di");w.up=false;r.poll(w);w.up=true;request(r,j,"ag\n");assert(!r.active());
  // Corrupt retained input cannot be replayed; finite sequence/counter bounds.
  j.magic=0;request(r,j);assert(!r.active());j=snapshot;changing(j);j.next=UINT32_MAX;seal(j);
  const auto count=j.eventCount;event(j,0,DeepEnter);assert(j.eventCount==count&&j.next==UINT32_MAX&&valid(j));
  assert(!begin(j,true,0,3,0));assert(j.boot==1&&valid(j));
  changing(j);j.boot=UINT32_MAX;seal(j);assert(!begin(j,true,0,3,0));
  std::cout<<"diagnostic journal: retention, cold reset, all-byte corruption, interrupted commit, separate overflow, truncation, parser, partial writes, backpressure and reconnect passed\n";
}
