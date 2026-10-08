#pragma once
#define FakeSerial BaseFakeSerial
#define Serial BaseSerial
#include "../diagnostic_shim/Arduino.h"
#undef FakeSerial
#undef Serial
struct FakeSerial : BaseFakeSerial {
  size_t writeLimit=256,zeroAfter=SIZE_MAX;
  std::string pending;
  size_t setTxBufferSize(size_t n){space=n;return n;}
  void (*onWrite)()=nullptr;
  explicit operator bool(){
    if(connected && !pending.empty()){output+=pending;space+=pending.size();pending.clear();}
    return BaseFakeSerial::operator bool();
  }
  size_t write(const uint8_t* p,size_t n){
    assert(n<=64);
    if(onWrite){auto callback=onWrite;onWrite=nullptr;callback();}
    if(!connected){
      ++calls;assert(timeout==0 && n<=space);
      n=writes>=zeroAfter?0:std::min(n,writeLimit);++writes;
      pending.append(reinterpret_cast<const char*>(p),n);space-=n;return n;
    }
    return BaseFakeSerial::write(p,writes>=zeroAfter?0:std::min(n,writeLimit));
  }
};
inline FakeSerial Serial;
