#pragma once
#include <cstddef>
#include <cstdint>
namespace RiscCpu { namespace HciBounds {
constexpr size_t MaxPayload=1028, QueueDepth=4;
constexpr uint32_t MaxWaitMs=20;
inline bool acl(const uint8_t* p,size_t n){return p && n>=4 && n<=MaxPayload && n==4u+p[2]+(size_t(p[3])<<8);}
inline bool tx(uint8_t type,const uint8_t* p,size_t n){
  return type==2?acl(p,n):type==1 && p && n>=3 && n<=258 && n==3u+p[2];
}
inline bool rx(uint8_t type,const uint8_t* p,size_t n){
  return type==2?acl(p,n):type==4 && p && n>=2 && n<=257 && n==2u+p[1];
}
}}
