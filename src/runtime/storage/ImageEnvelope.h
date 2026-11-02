#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace RiscStorage {
// Storage representation only. CRC32 detects corruption, never authenticity.
struct ImageEnvelope {
 static constexpr size_t HeaderBytes=32;
 static bool magic(const uint8_t* p,size_t n){return n>=8&&!memcmp(p,"RTEIMG1\0",8);}
 static uint32_t word(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 uint32_t decoded=0,payload=0,crc=0;
 bool parse(const uint8_t* p,size_t n,uint64_t physical,size_t maximum){
  if(n!=HeaderBytes||!magic(p,n)||word(p+8)!=HeaderBytes||word(p+12)!=1||word(p+28))return false;
  decoded=word(p+16);payload=word(p+20);crc=word(p+24);
  return decoded>=52&&decoded<=maximum&&payload&&uint64_t(payload)+HeaderBytes==physical&&
   uint64_t(payload)<=uint64_t(decoded)+decoded/255u+16u;
 }
 static uint32_t crcAdd(uint32_t state,const uint8_t* p,size_t n){
  for(size_t i=0;i<n;++i){state^=p[i];for(unsigned bit=0;bit<8;++bit)state=(state>>1)^(0xedb88320u&uint32_t(0-int32_t(state&1)));}
  return state;
 }
};
}
