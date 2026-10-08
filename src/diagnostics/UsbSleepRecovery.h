#pragma once
#include <cstdint>

namespace RiscDiagnostics {
// Owner-polled only: native sleep merely requests recovery. No host wait, timer,
// task, allocation or transport operation occurs in request(). Repeated sleeps
// coalesce while pending/detached. A failed begin retries only after a new sleep.
class UsbSleepRecovery {
 public:
  static constexpr uint32_t DetachMs=20;
  void reset(){state_=Ready;detachedAt_=0;}
  void request(){if(state_==Ready || state_==Failed)state_=Pending;}
  bool ready() const{return state_==Ready;}
  template<class Transport> bool poll(Transport& out){
    if(state_==Pending){out.end();detachedAt_=out.nowMs();state_=Detached;return false;}
    if(state_==Detached && uint32_t(out.nowMs()-detachedAt_)>=DetachMs){
      // begin() is attempted once, including partial-allocation failure. Keep
      // any partial driver resources bounded until the next explicit recovery.
      state_=out.begin()?Ready:Failed;
    }
    return ready();
  }
 private:
  enum State {Ready,Pending,Detached,Failed};
  State state_=Ready;
  uint32_t detachedAt_=0;
};
}
