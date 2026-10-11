#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
using TaskHandle_t=void*;
inline TaskHandle_t task=reinterpret_cast<void*>(1);
inline uint32_t now=1;
inline bool diagnosticIsr=false;
inline bool xPortInIsrContext(){return diagnosticIsr;}
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return task;}
inline uint32_t millis(){return now;}
struct FakeSerial {
  enum Failure {None,Mutex,Rx,Tx,Interrupt};
  bool connected=false,host=false,rxReady=true,txReady=true,mutexReady=true;
  Failure failure=None;
  uint32_t timeout=999,endDelay=0;size_t space=256,reads=0,writes=0,begins=0,ends=0,calls=0;
  std::string input,output;
  explicit operator bool(){++calls;return connected;}
  void setTxTimeoutMs(uint32_t ms){++calls;timeout=ms;}
  int availableForWrite(){++calls;return txReady && mutexReady?int(space):0;}
  int available(){++calls;return rxReady?int(input.size()):-1;}
  int read(){++calls;++reads;if(input.empty())return -1;int n=input[0];input.erase(0,1);return n;}
  size_t write(const uint8_t* p,size_t n){++calls;assert(connected && timeout==0 && n<=space && n<=256);++writes;output.append((const char*)p,n);return n;}
  void end(){++calls;++ends;connected=false;rxReady=txReady=mutexReady=false;input.clear();now+=endDelay;}
  void begin(unsigned long baud){
    ++calls;++begins;assert(baud==115200);
    // Mirror the pinned allocation order and partial-success behavior. Actual
    // interrupt allocation failure calls end(); other allocation failures leave
    // already-created objects allocated and return no success value.
    mutexReady=failure!=Mutex;rxReady=failure!=Rx;txReady=failure!=Tx;
    if(failure==Interrupt){end();return;}
    connected=host;
  }
};
inline FakeSerial Serial;
