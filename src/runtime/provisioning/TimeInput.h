#pragma once
#include <cstring>
namespace RiscProvision {
inline bool timeServer(const char* name){
 if(!name||!*name||strlen(name)>63||*name=='.'||name[strlen(name)-1]=='.')return false;
 for(const char* p=name;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'))return false;
 return true;
}
}
