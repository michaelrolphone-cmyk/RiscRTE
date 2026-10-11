#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "Checkpoint.h"

// Owner-task-only, allocation-free flight recorder. Sleep events and recent app
// text have separate rings, so heartbeat chatter cannot evict sleep evidence.
namespace RiscDiagnostics {
constexpr uint32_t Magic=0x52444a31u;
constexpr unsigned EventCapacity=16, MessageCapacity=8, TextSize=96;
enum Kind : uint32_t { BootFresh=1, BootRetained, LightEnter, LightReturn, DeepEnter };
struct Event {uint32_t sequence,boot,ms,kind; int32_t detail; uint32_t cause;};
struct Message {uint32_t sequence,boot,ms; char text[TextSize];};
struct Journal {
  uint32_t magic,checksum,boot,next,eventCount,eventHead,messageCount,messageHead,eventLost,messageLost;
  Event events[EventCapacity];
  Message messages[MessageCapacity];
};
static_assert(sizeof(Journal)==1288,"retained journal budget changed");
inline uint32_t checksum(const Journal& j){
  const auto* bytes=reinterpret_cast<const unsigned char*>(&j);
  uint32_t hash=2166136261u;
  for(size_t i=offsetof(Journal,boot);i<sizeof(j);++i)hash=(hash^bytes[i])*16777619u;
  return hash;
}
inline bool valid(const Journal& j){
  return j.magic==Magic && j.boot && j.next && j.eventCount<=EventCapacity &&
    j.eventHead<EventCapacity && j.messageCount<=MessageCapacity && j.messageHead<MessageCapacity &&
    j.checksum==checksum(j);
}
inline void changing(Journal& j){j.magic=0;std::atomic_signal_fence(std::memory_order_seq_cst);}
inline void seal(Journal& j){j.checksum=checksum(j);std::atomic_signal_fence(std::memory_order_seq_cst);j.magic=Magic;}
inline void lost(uint32_t& value){if(value!=UINT32_MAX)++value;}
inline void event(Journal& j,uint32_t ms,Kind kind,int32_t detail=0,uint32_t cause=0){
  changing(j);
  if(j.next==UINT32_MAX){lost(j.eventLost);seal(j);return;}
  j.events[j.eventHead]={j.next++,j.boot,ms,uint32_t(kind),detail,cause};
  j.eventHead=(j.eventHead+1)%EventCapacity;
  if(j.eventCount<EventCapacity)++j.eventCount;else lost(j.eventLost);
  seal(j);
}
inline bool begin(Journal& j,bool mayRetain,uint32_t ms,int32_t reset,uint32_t wake){
  const bool retained=mayRetain && valid(j) && j.boot!=UINT32_MAX && j.next!=UINT32_MAX;
  if(!retained){std::memset(&j,0,sizeof(j));j.next=1;}
  changing(j);++j.boot;seal(j);
  event(j,ms,retained?BootRetained:BootFresh,reset,wake);
  return retained;
}
inline void message(Journal& j,uint32_t ms,const char* text){
  if(!text)return;
  changing(j);
  if(j.next==UINT32_MAX){lost(j.messageLost);seal(j);return;}
  auto& m=j.messages[j.messageHead];m={};m.sequence=j.next++;m.boot=j.boot;m.ms=ms;
  unsigned i=0;
  for(;i<TextSize-1 && text[i];++i){const unsigned char ch=text[i];m.text[i]=ch>=32 && ch<=126?char(ch):'?';}
  if(i==TextSize-1 && text[i])m.text[TextSize-2]='~';
  j.messageHead=(j.messageHead+1)%MessageCapacity;
  if(j.messageCount<MessageCapacity)++j.messageCount;else lost(j.messageLost);
  seal(j);
}
inline const char* kindName(uint32_t kind){
  switch(kind){case BootFresh:return "boot-fresh";case BootRetained:return "boot-retained";
    case LightEnter:return "light-enter";case LightReturn:return "light-return";case DeepEnter:return "deep-enter";}
  return "unknown";
}
// A read-only, exact-line command. No reset, erase, wake, configuration, arbitrary
// input forwarding or command execution. Repeated requests cannot restart a dump.
class Replay {
 public:
  void disconnect(){used_=0;discard_=false;cr_=false;active_=false;pending_=offset_=index_=0;}
  void input(char ch,const Journal& current,const Checkpoint* checkpoint=nullptr){
    if(ch=='\r'){if(cr_)discard_=true;cr_=true;return;}
    if(ch=='\n'){
      if(!discard_ && used_==4 && std::memcmp(command_,"diag",4)==0 && !active_ && valid(current)){
        snapshot_=current;checkpoint_=checkpoint?*checkpoint:Checkpoint{};
        active_=true;index_=pending_=offset_=0;
      }
      used_=0;discard_=false;cr_=false;return;
    }
    if(cr_)discard_=true;
    if(used_<sizeof(command_))command_[used_++]=ch;else discard_=true;
  }
  bool active() const{return active_;}
  // At most one 64-byte, preflight-capacity-bounded write per poll. Preserve
  // partial-write offsets and abandon only the in-flight snapshot on disconnect.
  template<class Transport> void poll(Transport& out){
    if(!out.connected()){disconnect();return;}
    if(!active_)return;
    if(!pending_)prepare();
    if(!pending_)return;
    const size_t available=out.writable();
    size_t count=pending_-offset_;
    if(count>64)count=64;
    if(count>available)count=available;
    if(!count)return;
    size_t written=out.write(reinterpret_cast<const uint8_t*>(line_)+offset_,count);
    if(written>count)written=count;
    offset_+=written;
    if(offset_==pending_){pending_=offset_=0;++index_;if(index_>snapshot_.eventCount+snapshot_.messageCount+checkpointLines()+1)active_=false;}
  }
 private:
  size_t checkpointLines() const {
    return checkpoint_.sequence?1+(checkpoint_.applicationLength+95)/96+(checkpoint_.length+95)/96:0;
  }
  void prepare(){
    int length=0;
    if(index_==0)length=std::snprintf(line_,sizeof(line_),"RTE_DIAG begin schema=1 events=%u messages=%u event_lost=%lu message_lost=%lu\n",
      unsigned(snapshot_.eventCount),unsigned(snapshot_.messageCount),(unsigned long)snapshot_.eventLost,(unsigned long)snapshot_.messageLost);
    else if(index_<=snapshot_.eventCount){
      const auto& e=snapshot_.events[(snapshot_.eventHead+EventCapacity-snapshot_.eventCount+index_-1)%EventCapacity];
      length=std::snprintf(line_,sizeof(line_),"RTE_DIAG seq=%lu boot=%lu ms=%lu event=%s detail=%ld cause=%lu\n",
        (unsigned long)e.sequence,(unsigned long)e.boot,(unsigned long)e.ms,kindName(e.kind),(long)e.detail,(unsigned long)e.cause);
    }else if(index_<=snapshot_.eventCount+snapshot_.messageCount){
      const auto& m=snapshot_.messages[(snapshot_.messageHead+MessageCapacity-snapshot_.messageCount+index_-snapshot_.eventCount-1)%MessageCapacity];
      length=std::snprintf(line_,sizeof(line_),"RTE_DIAG seq=%lu boot=%lu ms=%lu text=%.*s\n",
        (unsigned long)m.sequence,(unsigned long)m.boot,(unsigned long)m.ms,int(TextSize),m.text);
    }else if(index_<=snapshot_.eventCount+snapshot_.messageCount+checkpointLines()){
      const size_t part=index_-snapshot_.eventCount-snapshot_.messageCount-1;
      const size_t appParts=(checkpoint_.applicationLength+95)/96;
      if(!part)length=std::snprintf(line_,sizeof(line_),
        "RTE_DIAG checkpoint seq=%lu boot=%lu ms=%lu invocation=%llu length=%lu truncated=%u\n",
        (unsigned long)checkpoint_.sequence,(unsigned long)checkpoint_.boot,(unsigned long)checkpoint_.ms,
        (unsigned long long)checkpoint_.invocation,(unsigned long)checkpoint_.length,unsigned(checkpoint_.truncated));
      else {
        const bool app=part<=appParts;
        const size_t offset=(app?part-1:part-appParts-1)*96;
        size_t count=(app?checkpoint_.applicationLength:checkpoint_.length)-offset;
        if(count>96)count=96;
        length=std::snprintf(line_,sizeof(line_),"RTE_DIAG checkpoint %s offset=%lu text=%.*s\n",
          app?"application":"data",(unsigned long)offset,int(count),(app?checkpoint_.application:checkpoint_.text)+offset);
      }
    }else length=std::snprintf(line_,sizeof(line_),"RTE_DIAG end\n");
    if(length>0 && size_t(length)<sizeof(line_))pending_=size_t(length);
    else disconnect();
  }
  Journal snapshot_{};
  Checkpoint checkpoint_{};
  char command_[4]{},line_[192]{};
  size_t used_=0,pending_=0,offset_=0,index_=0;
  bool discard_=false,active_=false,cr_=false;
};
}
