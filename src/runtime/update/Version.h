#pragma once
#include <cstdint>
namespace RiscUpdate {
// Canonical three-part numeric versions. Each component is bounded BEFORE
// multiplication; never use sscanf's implementation-defined overflow behavior.
inline bool parseVersion(const char* input,uint32_t (&parts)[3]) {
  if(!input)return false;
  const char* s=input;
  for(unsigned i=0;i<3;++i){
    parts[i]=0;const char* begin=s;
    while(*s>='0' && *s<='9'){
      uint32_t digit=uint32_t(*s-'0');
      if(parts[i]>(UINT32_MAX-digit)/10u)return false;
      parts[i]=parts[i]*10u+digit;++s;
      if(s-input>=32)return false;
    }
    if(s==begin || (s-begin>1 && *begin=='0'))return false;
    if(i<2){if(*s!='.')return false;++s;}
    else if(*s)return false;
  }
  return true;
}
inline int compareVersion(const uint32_t (&a)[3],const uint32_t (&b)[3]) {
  for(unsigned i=0;i<3;++i)if(a[i]!=b[i])return a[i]>b[i]?1:-1;
  return 0;
}
}
