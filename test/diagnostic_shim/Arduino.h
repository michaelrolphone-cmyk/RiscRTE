#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
using TaskHandle_t=void*;
inline TaskHandle_t task=reinterpret_cast<void*>(1);
inline uint32_t now=1;
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return task;}
inline uint32_t millis(){return now;}
struct FakeSerial {
  bool connected=false;uint32_t timeout=999;size_t space=256,reads=0,writes=0;std::string input,output;
  explicit operator bool()const{return connected;}
  void setTxTimeoutMs(uint32_t ms){timeout=ms;}
  int availableForWrite(){return int(space);}
  int available(){return int(input.size());}
  int read(){++reads;if(input.empty())return -1;int n=input[0];input.erase(0,1);return n;}
  size_t write(const uint8_t* p,size_t n){assert(connected && timeout==0 && n<=space && n<=256);++writes;output.append((const char*)p,n);return n;}
};
inline FakeSerial Serial;
