#pragma once
#include <stdbool.h>
#include <stddef.h>
/* RTECFG is deliberately a FAT 8.3 name: it has no alternate long-name
 * short alias. Reject complete components, including case and trailing-dot
 * or space aliases, before listing, opening, copying or mutating a path. */
static inline bool risc_protected_settings_path(const char *path) {
 if(!path)return false;
 const char *part=path;
 for(const char *p=path;;++p) {
  if(*p=='/' || *p=='\\' || !*p) {
   size_t n=(size_t)(p-part);
   while(n && (part[n-1]=='.' || part[n-1]==' '))--n;
   if(n==6) {
    static const char name[]="rtecfg";
    size_t i=0;
    for(;i<n;++i) {
     unsigned char c=(unsigned char)part[i];
     if(c>='A' && c<='Z')c=(unsigned char)(c+'a'-'A');
     if(c!=(unsigned char)name[i])break;
    }
    if(i==n)return true;
   }
   if(!*p)return false;
   part=p+1;
  }
 }
}
