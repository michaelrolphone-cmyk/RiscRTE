#pragma once
#include "NativeReplicaCore.h"
#include <cstdio>
#include <cstring>
namespace RiscExperimental {
// Internal staging-name extension v2. This grammar conveys no authority: only
// replica.index commits a map, and a writer must still prove exact owned bytes.
class SdReplicaStage final {
 public:
 using C=NativeReplicaCore;
 static constexpr size_t NameBytes=125,PathBytes=142;
 static bool makePath(char*out,size_t capacity,uint32_t ns,const C::Identity&id){
  if(!out||capacity<PathBytes||!ns||ns>INT32_MAX||!id.known)return false;
  uint8_t nonzero=0;for(uint8_t b:id.lineage)nonzero|=b;if(!nonzero)return false;
  std::snprintf(out,capacity,"/RTECFG/%08X/",unsigned(ns));char*n=out+17;const char*hex="0123456789abcdef";std::memcpy(n,".nr2-",5);
  for(unsigned i=0;i<16;++i){n[5+2*i]=hex[id.lineage[i]>>4];n[6+2*i]=hex[id.lineage[i]&15];}n[37]='-';
  for(unsigned i=0;i<16;++i)n[38+i]=hex[(id.generation>>(60-4*i))&15];
  n[54]='-';
  for(unsigned i=0;i<32;++i){n[55+2*i]=hex[id.digest[i]>>4];n[56+2*i]=hex[id.digest[i]&15];}std::memcpy(n+119,".next",6);return true;
 }
 static bool parseName(const char*n,C::Identity*out){
  if(!n||!out||std::strlen(n)!=NameBytes-1||std::memcmp(n,".nr2-",5)||n[37]!='-'||n[54]!='-'||std::strcmp(n+119,".next"))return false;
  C::Identity id{};uint8_t nonzero=0;
  for(unsigned i=5;i<119;++i){if(i==37||i==54)continue;const unsigned c=uint8_t(n[i]);if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;const uint8_t d=uint8_t(c<='9'?c-'0':c-'a'+10);
   if(i<37){id.lineage[(i-5)/2]=uint8_t((id.lineage[(i-5)/2]<<4)|d);nonzero|=d;}else if(i<54)id.generation=(id.generation<<4)|d;else id.digest[(i-55)/2]=uint8_t((id.digest[(i-55)/2]<<4)|d);
  }
  if(!nonzero)return false;
  id.known=true;*out=id;return true;
 }
};
}
