#pragma once
#include <RiscStorageFilesV1.h>
#include <cstring>
namespace RiscStorage {
inline bool validFlags(uint32_t flags) {
    return !(flags&~63u) && (flags&(RISC_FILES_READ|RISC_FILES_WRITE)) &&
      (!(flags&(RISC_FILES_CREATE|RISC_FILES_TRUNCATE|RISC_FILES_APPEND)) || (flags&RISC_FILES_WRITE)) &&
      (!(flags&RISC_FILES_EXCLUSIVE) || (flags&RISC_FILES_CREATE));
  }
inline bool validPath(const char* path) {
    if(!path || path[0]!='/')return false;
    size_t n=strnlen(path,RISC_STORAGE_FILES_PATH_MAX);if(!n || n>=RISC_STORAGE_FILES_PATH_MAX)return false;
    if(n==1)return true;
    // Reject malformed UTF-8, overlong forms, surrogate code points and values
    // outside Unicode. Names are never normalized or silently reinterpreted.
    for(size_t i=1;i<n;) {
      unsigned char lead=static_cast<unsigned char>(path[i++]);
      if(lead<0x80)continue;
      unsigned remaining=0;uint32_t code=0,minimum=0;
      if(lead>=0xc2 && lead<=0xdf){remaining=1;code=lead&31u;minimum=0x80;}
      else if(lead>=0xe0 && lead<=0xef){remaining=2;code=lead&15u;minimum=0x800;}
      else if(lead>=0xf0 && lead<=0xf4){remaining=3;code=lead&7u;minimum=0x10000;}
      else return false;
      if(n-i<remaining)return false;
      while(remaining--){unsigned char next=static_cast<unsigned char>(path[i++]);if((next&0xc0)!=0x80)return false;code=(code<<6)|(next&63u);}
      if(code<minimum || code>0x10ffff || (code>=0xd800 && code<=0xdfff))return false;
    }
    size_t start=1;
    for(size_t i=1;i<=n;++i) {
      unsigned char c=static_cast<unsigned char>(path[i]);if((c && c<32) || c==127 || c=='\\')return false;
      if(!c || c=='/') {
        size_t count=i-start;if(!count || (count==1 && path[start]=='.') || (count==2 && path[start]=='.' && path[start+1]=='.'))return false;
        start=i+1;
      }
    }
    return true;
  }
}
