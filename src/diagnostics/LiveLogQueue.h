#pragma once
#include <cstddef>
#include <cstdint>
namespace RiscDiagnostics {
// Owner-task RAM only. Whole lines are admitted atomically; overflow rejects
// the newest line. Keep the current line until its final byte is accepted so
// a disconnect can replay it, prefixed by a framing newline. Accepted complete
// lines are not durable: USB enqueue is not host receipt acknowledgement.
template<size_t Capacity> class LiveLogQueue {
  static_assert(Capacity>=256,"Live log queue must hold one maximum line");
  uint8_t bytes_[Capacity]{};
  size_t head_=0,used_=0,sent_=0;
  bool boundary_=false;
 public:
  size_t size()const{return used_;}
  bool partial()const{return sent_!=0 || boundary_;}
  bool append(const char* text,size_t size){
    if(!size || size>256 || size>Capacity-used_ || text[size-1]!='\n')return false;
    for(size_t i=0;i<size;++i)bytes_[(head_+used_+i)%Capacity]=uint8_t(text[i]);
    used_+=size;return true;
  }
  void disconnected(){if(sent_){sent_=0;boundary_=true;}}
  template<class Transport> void poll(Transport& transport){
    const auto available=transport.writable();if(!available)return;
    if(boundary_){const uint8_t nl='\n';if(transport.write(&nl,1)==1)boundary_=false;return;}
    if(!used_)return;
    const size_t start=(head_+sent_)%Capacity;
    size_t count=0;
    while(count<64 && count<available && count<used_-sent_ && start+count<Capacity){
      const auto byte=bytes_[start+count++];if(byte=='\n')break;
    }
    if(!count)return;
    size_t written=transport.write(bytes_+start,count);if(written>count)written=count;
    if(!written)return;
    sent_+=written;
    if(bytes_[(head_+sent_-1)%Capacity]=='\n'){
      head_=(head_+sent_)%Capacity;used_-=sent_;sent_=0;
    }
  }
};
}
