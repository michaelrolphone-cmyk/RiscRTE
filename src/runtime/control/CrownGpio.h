#pragma once
#include <cstdint>
namespace RiscControl {
// Convert a sampled switch level to the existing crown event contract.
// Initial held levels can establish a fresh five-second hold, but their
// release never becomes a click. Gaps discard all inferred edge history.
class CrownGpio final {
 public:
  static constexpr uint32_t DebounceMs=20, ShortMs=600, MaxGapMs=250;
  void reset(){seen_=stable_=candidate_=armed_=false;since_=pressed_=last_=0;}
  uint8_t sample(bool down,uint32_t now){
    if(!seen_ || uint32_t(now-last_)>MaxGapMs){
      reset();seen_=true;candidate_=down;since_=now;last_=now;
      return 0;
    }
    last_=now;
    if(down!=candidate_){candidate_=down;since_=now;}
    if(uint32_t(now-since_)>=DebounceMs){
      if(candidate_!=stable_){
        stable_=candidate_;
        if(stable_)pressed_=now;
        else {const bool click=armed_ && uint32_t(now-pressed_)<ShortMs;armed_=true;return click?9:1;}
      } else if(!stable_)armed_=true;
    }
    // A raw release immediately breaks hold evidence; debounce may delay a
    // click, but must never stretch a sub-five-second hold across the limit.
    if(stable_ && !down)return 1;
    return stable_?4:0;
  }
 private:
  bool seen_=false,stable_=false,candidate_=false,armed_=false;
  uint32_t since_=0,pressed_=0,last_=0;
};
}
