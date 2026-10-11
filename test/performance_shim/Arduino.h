#pragma once
#define FakeSerial BaseFakeSerial
#define Serial BaseSerial
#include "../diagnostic_shim/Arduino.h"
#undef FakeSerial
#undef Serial
// Extend the existing production-adapter shim with genuine short/zero writes.
struct FakeSerial : BaseFakeSerial {
  size_t writeLimit=256;
  size_t write(const uint8_t* p,size_t n){return BaseFakeSerial::write(p,std::min(n,writeLimit));}
};
inline FakeSerial Serial;
