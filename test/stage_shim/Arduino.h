#pragma once
#define FakeSerial BaseFakeSerial
#define Serial BaseSerial
#include "../diagnostic_shim/Arduino.h"
#undef FakeSerial
#undef Serial
struct FakeSerial : BaseFakeSerial {
  size_t writeLimit=256,zeroAfter=SIZE_MAX;
  void (*onWrite)()=nullptr;
  size_t write(const uint8_t* p,size_t n){
    assert(n<=64);
    if(onWrite){auto callback=onWrite;onWrite=nullptr;callback();}
    return BaseFakeSerial::write(p,writes>=zeroAfter?0:std::min(n,writeLimit));
  }
};
inline FakeSerial Serial;
