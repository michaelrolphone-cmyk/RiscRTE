#pragma once
#include <cstdint>
namespace RiscControl {
// AXP2101 IRQ status2 bits are histories, not a readable switch level.
// Never turn silence at boot, a bus failure, or an unobserved sleep interval
// into evidence of a continuous hold. The native broker serializes access.
class CrownRecovery final {
 public:
  static constexpr uint32_t HoldMs=5000, MaxGapMs=250;
  bool sample(uint8_t events,uint32_t now,bool valid=true) {
    if(fired_)return false;
    if(!valid){cancel();seen_=false;return false;}
    if(seen_ && uint32_t(now-last_)>MaxGapMs)cancel();
    seen_=true;last_=now;
    // Release wins if both edges coalesced. Never reconstruct their order.
    if(events&0x09u){cancel();return false;}
    if((events&0x06u) && !held_){held_=true;since_=now;}
    // Long status without a witnessed down edge (e.g. held at boot) starts
    // a fresh five-second observation; it never backdates the timer.
    if(held_ && uint32_t(now-since_)>=HoldMs){fired_=true;return true;}
    return false;
  }
  void cancel(){held_=false;since_=0;}
  bool held()const{return held_;}
  bool fired()const{return fired_;}
 private:
  uint32_t since_=0,last_=0;
  bool held_=false,seen_=false,fired_=false;
};
// Native lifetime owns the request. Neither reading it nor app event ACK can
// dismiss it. Only a completed host cleanup ends this bounded exit interval.
class CrownExit final {
 public:
  static constexpr uint32_t CleanupMs=3000;
  void begin(bool child){child_=child;pending_=false;since_=0;}
  void end(){begin(false);}
  void press(uint32_t now){if(child_&&!pending_){pending_=true;since_=now;}}
  bool pending()const{return pending_;}
  bool expired(uint32_t now)const{return pending_&&uint32_t(now-since_)>=CleanupMs;}
 private:
  uint32_t since_=0;bool child_=false,pending_=false;
};
// Separate native and legacy consumers: application ACK cannot erase host
// navigation/recovery state, and native polling does not steal app events.
class CrownEvents final {
 public:
  bool observe(uint8_t events,uint32_t now,bool valid=true){
    uint8_t delivered=events;
    if(valid && waking_){suppressWakeRelease_=(events&0x06u) && !(events&0x09u);waking_=false;}
    if(valid && suppressWakeRelease_ && (events&0x09u)){delivered&=uint8_t(~0x08u);suppressWakeRelease_=false;}
    if(valid){legacy_|=delivered&0x0f;if(delivered&0x08u)++shortSequence_;}
    return recovery_.sample(events,now,valid);
  }
  uint8_t legacy()const{return legacy_;}
  void acknowledge(uint8_t mask){legacy_&=uint8_t(~mask);}
  uint32_t shortSequence()const{return shortSequence_;}
  void discontinuity(){recovery_.sample(0,0,false);}
  void waking(){discontinuity();waking_=true;}
 private:
  CrownRecovery recovery_{};
  uint32_t shortSequence_=0;
  uint8_t legacy_=0;
  bool waking_=false,suppressWakeRelease_=false;
};
}
