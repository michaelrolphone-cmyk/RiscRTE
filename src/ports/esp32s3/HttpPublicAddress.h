#pragma once
#include <cstdint>
namespace RiscCpu { namespace HttpPublicAddress {
// Network-order address converted with ntohl. Deny special-use IPv4 ranges;
// native DNS currently connects IPv4 only. Never re-resolve after this check.
inline bool ipv4(uint32_t ip){
 const struct Range{uint32_t base,mask;} ranges[]={
  {0x00000000u,0xff000000u},{0x0a000000u,0xff000000u},
  {0x64400000u,0xffc00000u},{0x7f000000u,0xff000000u},
  {0xa9fe0000u,0xffff0000u},{0xac100000u,0xfff00000u},
  {0xc0000000u,0xffffff00u},{0xc0000200u,0xffffff00u},
  {0xc0586300u,0xffffff00u},{0xc0a80000u,0xffff0000u},
  {0xc6120000u,0xfffe0000u},{0xc6336400u,0xffffff00u},
  {0xcb007100u,0xffffff00u},{0xe0000000u,0xe0000000u}
 };for(auto r:ranges)if((ip&r.mask)==r.base)return false;return true;
}
}}
