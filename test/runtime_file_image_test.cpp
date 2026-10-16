#include "bootstrap/RuntimeFileImage.h"
#include "runtime/storage/PosixFiles.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cstdio>
using namespace RiscBoot;
static bool allowed(){return true;}
struct Source {
  RiscStorage::PosixFiles unused{"/no-such-runtime-image-store"};
  risc_storage_files_v1 api=*unused.api();RuntimeFiles files;
  std::vector<uint8_t> data=std::vector<uint8_t>(100000,0x42);
  size_t offset=0,shortRead=4096;unsigned opens=0,reads=0,closes=0;
  bool missing=false,failRead=false,retainRead=false,failClose=false,oversize=false,grow=false;
  bool loseOpen=false,loseRead=false,loseClose=false;bool* safety=nullptr;
  Source(){
    api.context=this;
    api.open=[](void*c,const char*p,uint32_t f,uint32_t*h)->int32_t {
      auto&s=*static_cast<Source*>(c);++s.opens;assert(!strcmp(p,"/image.elf") && f==RISC_FILES_READ);
      if(s.loseOpen){*s.safety=false;return RISC_FILES_IO;}
      if(s.missing)return RISC_FILES_NOT_FOUND;s.offset=0;*h=1;return RISC_FILES_OK;
    };
    api.info=[](void*c,uint32_t h,uint64_t*n,uint64_t*o)->int32_t {
      auto&s=*static_cast<Source*>(c);assert(h==1);*n=s.data.size();*o=s.offset;return RISC_FILES_OK;
    };
    api.read=[](void*c,uint32_t h,void*p,uint32_t n,uint32_t*a)->int32_t {
      auto&s=*static_cast<Source*>(c);assert(h==1);++s.reads;*a=0;
      if(s.loseRead)*s.safety=false;
      if(s.retainRead)return RISC_FILES_RETAINED;if(s.failRead)return RISC_FILES_IO;
      if(s.oversize){*a=n+1;return RISC_FILES_OK;}
      const size_t count=std::min({size_t(n),s.shortRead,s.data.size()-s.offset});
      if(count)memcpy(p,s.data.data()+s.offset,count);s.offset+=count;*a=count;
      if(s.grow && !count){*a=1;*static_cast<uint8_t*>(p)=1;}
      return RISC_FILES_OK;
    };
    api.close=[](void*c,uint32_t h)->int32_t {auto&s=*static_cast<Source*>(c);assert(h==1);++s.closes;if(s.loseClose)*s.safety=false;return s.failClose?RISC_FILES_IO:RISC_FILES_OK;};
    assert(files.bind(&api,allowed,allowed));
  }
};
static unsigned allocations=0,frees=0;static bool oom=false;
struct Context {bool safe=true,timeout=false;size_t checkpoints=0,bytes=0;Source* retainAfterClose=nullptr;size_t timeoutAt=SIZE_MAX;bool loseOnFinalFailure=false;};
static RuntimeFileImage::Hooks hooks(Context&c){return {
 [](size_t n)->void*{++allocations;return oom?nullptr:malloc(n);},[](void*p){++frees;free(p);},
 [](void*p){return static_cast<Context*>(p)->safe;},
 [](void*p,size_t n){auto&c=*static_cast<Context*>(p);++c.checkpoints;c.bytes+=n;if(c.retainAfterClose && c.retainAfterClose->closes){if(c.loseOnFinalFailure){c.safe=false;return false;}c.retainAfterClose->files.retainCustody();}return !c.timeout && c.checkpoints<c.timeoutAt;},&c};}
int main(){
 for(int mode=0;mode<19;++mode){
  Source source;RuntimeFileSources::Entry entry{"/active",&source.files};RuntimeFileSources map(&entry,1);
  RuntimeFileView view(&map,"/active");RuntimeFileImage image;Context context;
  oom=mode==2||mode==8;source.failClose=mode==3||mode==8;source.failRead=mode==4;
  source.retainRead=mode==5;source.oversize=mode==6;source.grow=mode==7;source.shortRead=mode==1?17:4096;
  source.safety=&context.safe;
  if(mode==9)source.missing=true;
  if(mode==10)source.data.resize(51);
  if(mode==13)source.loseRead=true;
  if(mode==14)source.loseOpen=true;
  if(mode==15)source.loseClose=true;
  if(mode==16)context.retainAfterClose=&source;
  if(mode==17)context.timeoutAt=6;
  if(mode==18){context.retainAfterClose=&source;context.loseOnFinalFailure=true;}
  const unsigned before=allocations,freed=frees;
  const bool ok=image.read(view,"/active/image.elf",hooks(context),mode==12?123:0,mode==11?99999:8u*1024u*1024u);
  if(mode<2){assert(ok && image.size==source.data.size() && !memcmp(image.bytes,source.data.data(),image.size));assert(allocations==before+1 && frees==freed);free(image.bytes);assert(context.bytes==image.size && source.closes==1);}
  else {assert(!ok && !image.size);
    const bool retained=mode==3||mode==5||mode==6||mode==8||(mode>=13&&mode<=16)||mode==18;assert(image.retained==retained && map.retained()==retained);
    if(retained){assert(image.error!=ENOMEM && source.closes==(mode==3||mode==8||mode==15||mode==16||mode==18?1u:0u));const unsigned calls=source.opens+source.reads+source.closes;RuntimeFileImage again;assert(!again.read(view,"/active/image.elf",hooks(context)));assert(calls==source.opens+source.reads+source.closes);free(image.bytes);}
    else {assert(source.closes==(mode==9?0u:1u) && !image.bytes);if(mode==2)assert(image.error==ENOMEM);if(mode==17)assert(image.error==ETIMEDOUT);}
  }
 }
 oom=false;Source a,b;b.data.assign(99999,0x24);RuntimeFileSources::Entry entries[]={{"/active",&a.files},{"/staged",&b.files}};RuntimeFileSources map(entries,2);Context context;
 RuntimeFileImage image;assert(image.read(RuntimeFileView(&map,"/staged"),"/staged/image.elf",hooks(context)));assert(image.size==b.data.size() && image.bytes[0]==0x24 && !a.opens);free(image.bytes);
 RuntimeFileImage wrong;assert(!wrong.read(RuntimeFileView(&map,"/active"),"/staged/image.elf",hooks(context)));assert(!a.opens);
 RuntimeFileImage absent;assert(!absent.read(RuntimeFileView(nullptr,"/absent"),"/absent/image.elf",hooks(context)));
 RuntimeFileImage timeout;context.timeout=true;assert(!timeout.read(RuntimeFileView(&map,"/active"),"/active/image.elf",hooks(context)) && timeout.error==ETIMEDOUT && !a.opens);
 puts("Provider ELF snapshots: short reads, single allocation, exact source, absent provider, EOF, OOM, failed close and retained retry fence PASS");
}
