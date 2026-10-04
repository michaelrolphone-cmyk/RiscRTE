#include "StoreAudit.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
namespace RiscUpdate {
namespace {
bool safe(const char* name){
  if(!name || !*name || strlen(name)>192 || *name=='/')return false;
  const char* begin=name;
  for(const char* p=name;;++p){
    if(*p=='/' || !*p){size_t n=p-begin;if(!n || (n==1 && *begin=='.') || (n==2 && begin[0]=='.' && begin[1]=='.'))return false;
      if(!*p)return true;
      begin=p+1;
    }else if(!((*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') || *p=='_' || *p=='-' || *p=='.'))return false;
  }
}
bool namePath(char* out,size_t cap,const char* root,const char* name){int n=snprintf(out,cap,"%s/%s",root,name);return n>0 && size_t(n)<cap;}
}
bool auditStore(const char* active,const char* staged,const char* changedElf,const char* changedManifest,
                uint8_t* scratch,size_t capacity,void* context,uint32_t (*now)(void*),bool (*checkpoint)(void*)){
  if(!active || !staged || !safe(changedElf) || !safe(changedManifest) || !strcmp(changedElf,changedManifest) || !scratch || capacity<4096 || !now || !checkpoint)return false;
  const uint32_t start=now(context);uint32_t total=0;unsigned counts[2]{};bool changed[2][2]{};
  for(unsigned pass=0;pass<2;++pass){
    DIR* dir=opendir(pass?staged:active);if(!dir)return false;
    bool ok=true;
    for(;;){errno=0;dirent* item=readdir(dir);if(!item){if(errno)ok=false;break;}
      if(!strcmp(item->d_name,".") || !strcmp(item->d_name,".."))continue;
      if(++counts[pass]>128 || !safe(item->d_name) || uint32_t(now(context)-start)>30000u || !checkpoint(context)){ok=false;break;}
      bool skip=false;
      if(!strcmp(item->d_name,changedElf)){changed[pass][0]=true;skip=true;}
      if(!strcmp(item->d_name,changedManifest)){changed[pass][1]=true;skip=true;}
      char a[256],b[256];
      if(!namePath(a,sizeof(a),active,item->d_name) || !namePath(b,sizeof(b),staged,item->d_name)){ok=false;break;}
      struct stat sa{},sb{};
      if(stat(a,&sa) || stat(b,&sb) || !S_ISREG(sa.st_mode) || !S_ISREG(sb.st_mode) || sa.st_size<0 || sb.st_size<0){ok=false;break;}
      if(skip || pass)continue;
      if(sa.st_size!=sb.st_size || uint64_t(sa.st_size)>0x4f0000u-total){ok=false;break;}
      total+=uint32_t(sa.st_size);
      FILE* af=fopen(a,"rb");FILE* bf=fopen(b,"rb");
      if(!af || !bf){if(af)fclose(af);if(bf)fclose(bf);ok=false;break;}
      for(;;){size_t na=fread(scratch,1,2048,af),nb=fread(scratch+2048,1,2048,bf);
        if(na!=nb || memcmp(scratch,scratch+2048,na) || ferror(af) || ferror(bf)){ok=false;break;}
        if(!checkpoint(context) || uint32_t(now(context)-start)>30000u){ok=false;break;}
        if(!na)break;
      }
      if(fclose(af))ok=false;
      if(fclose(bf))ok=false;
      if(!ok)break;
    }
    if(closedir(dir))ok=false;
    if(!ok)return false;
  }
  return counts[0]==counts[1] && changed[0][0] && changed[0][1] && changed[1][0] && changed[1][1];
}
}
