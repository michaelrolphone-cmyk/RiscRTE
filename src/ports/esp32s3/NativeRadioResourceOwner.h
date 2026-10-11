#pragma once
#include <cstdint>
namespace RiscCpu {
// Reentrant owner-task adapter over the worker's nonblocking resource lease.
// All function pointers are immutable firmware functions, never provider/app
// callbacks. Worker access is exclusively through the underlying atomic lease.
class NativeRadioResourceOwner {
  bool (*owner_)()=nullptr;
  bool (*take_)()=nullptr;
  void (*give_)()=nullptr;
  bool (*phase_)()=nullptr;
  bool (*available_)()=nullptr;
  uint32_t depth_=0; // accessed only after owner_ proves the firmware owner
 public:
  void configure(bool(*owner)(),bool(*take)(),void(*give)(),bool(*phase)(),bool(*available)()){
    owner_=owner;take_=take;give_=give;phase_=phase;available_=available;
  }
  bool enter(){
    if(!owner_ || !owner_() || !take_ || !give_ || depth_==UINT32_MAX)return false;
    if(!depth_ && !take_())return false;
    ++depth_;return true;
  }
  void leave(){
    if(!owner_ || !owner_() || !depth_)return;
    if(!--depth_)give_();
  }
  bool heldReady()const{return owner_ && owner_() && depth_ && phase_ && phase_();}
  bool ready()const{
    if(!owner_ || !owner_())return false;
    return depth_?heldReady():available_ && available_();
  }
};
}
