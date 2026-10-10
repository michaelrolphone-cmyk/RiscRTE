#include "bootstrap/RemovableAppImage.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdio>
using RiscBoot::RemovableAppImage;
struct Card {
  std::vector<uint8_t> data=std::vector<uint8_t>(100000,0x5a);
  size_t offset=0,limit=4096,reads=0,checkpoints=0,failAt=SIZE_MAX;
  uint64_t length=100000;
  bool present=true,busy=false,closeOk=true,allocationOk=true;
  unsigned opens=0,closes=0;
  risc_storage_volume_api_v1 api{};
  Card() {
    api.api_version=1;api.struct_size=sizeof(api);api.context=this;
    api.refresh=[](void*){return true;};
    api.ready=[](void* c){return static_cast<Card*>(c)->present;};
    api.file_open_read=[](void* c,const char*,uint64_t* n)->uint32_t {
      auto& x=*static_cast<Card*>(c);++x.opens;*n=x.length;return x.busy?0:1;
    };
    api.file_read=[](void* c,uint32_t f,void* p,size_t n)->size_t {
      auto& x=*static_cast<Card*>(c);assert(f==1 && n<=4096);++x.reads;
      if(!x.present || x.offset>=x.data.size())return 0;
      n=std::min(n,std::min(x.limit,x.data.size()-x.offset));
      memcpy(p,x.data.data()+x.offset,n);x.offset+=n;return n;
    };
    api.file_close=[](void* c,uint32_t f,bool commit){
      auto& x=*static_cast<Card*>(c);assert(f==1 && !commit);++x.closes;return x.closeOk;
    };
  }
  RemovableAppImage::Hooks hooks() {
    return {[](size_t n)->void*{return malloc(n);},free,
      [](void* p){auto& c=*static_cast<Card*>(p);return ++c.checkpoints<c.failAt;},this};
  }
};
int main() {
  for(size_t limit:{size_t(4096),size_t(37)}) {
    Card c;c.limit=limit;RemovableAppImage s;
    assert(s.read(&c.api,"/Apps/example/app.elf",c.hooks()));
    assert(s.size==c.data.size() && !memcmp(s.bytes,c.data.data(),s.size));
    c.data.assign(c.data.size(),0);c.present=false;
    assert(s.bytes[0]==0x5a && c.closes==1 && c.opens==1);free(s.bytes);
  }
  for(unsigned mode=0;mode<8;++mode) {
    Card c;RemovableAppImage s;auto h=c.hooks();
    if(mode==0)c.present=false;
    if(mode==1)c.busy=true;
    if(mode==2)c.length=RemovableAppImage::MaxBytes+1;
    if(mode==3)c.length=51;
    if(mode==4)c.data.resize(12000);
    if(mode==5)c.failAt=4;
    if(mode==6)h.allocate=[](size_t)->void*{return nullptr;};
    if(mode==7)c.closeOk=false;
    assert(!s.read(&c.api,"/Apps/example/app.elf",h));
    assert(!s.bytes && !s.size && s.closeRetained==(mode==7));
    assert(c.closes==(mode<2?0u:1u));
  }
  puts("Removable image: short reads, snapshot isolation, absent/busy media, bounds, truncation, timeout, OOM and close custody PASS");
}
