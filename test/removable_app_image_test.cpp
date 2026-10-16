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
  bool refreshReady=false,present=true,busy=false,closeOk=true,allocationOk=true;
  unsigned opens=0,closes=0,refreshes=0,readyCalls=0,errorCalls=0,loseAt=0;bool safe=true;
  risc_storage_volume_api_v1 api{};
  Card() {
    api.api_version=1;api.struct_size=sizeof(api);api.context=this;
    api.refresh=[](void*c){auto&card=*static_cast<Card*>(c);++card.refreshes;if(card.loseAt==2)card.safe=false;if(card.refreshReady)card.present=true;return true;};
    api.ready=[](void* c){auto& x=*static_cast<Card*>(c);++x.readyCalls;if(x.loseAt==1 || (x.loseAt==3 && x.readyCalls==2))x.safe=false;return x.present;};
    api.file_open_read=[](void* c,const char*,uint64_t* n)->uint32_t {
      auto& x=*static_cast<Card*>(c);++x.opens;if(x.loseAt==4)x.safe=false;*n=x.length;return x.busy?0:1;
    };
    api.file_read=[](void* c,uint32_t f,void* p,size_t n)->size_t {
      auto& x=*static_cast<Card*>(c);assert(f==1 && n<=4096);++x.reads;if(x.loseAt==5)x.safe=false;
      if(!x.present || x.offset>=x.data.size())return 0;
      n=std::min(n,std::min(x.limit,x.data.size()-x.offset));
      memcpy(p,x.data.data()+x.offset,n);x.offset+=n;return n;
    };
    api.file_close=[](void* c,uint32_t f,bool commit){
      auto& x=*static_cast<Card*>(c);assert(f==1 && !commit);++x.closes;if(x.loseAt==7)x.safe=false;return x.closeOk;
    };
  }
  RemovableAppImage::Hooks hooks() {
    return {[](size_t n)->void*{return malloc(n);},free,[](void*p){return static_cast<Card*>(p)->safe;},
      [](void* p,size_t){auto& c=*static_cast<Card*>(p);return ++c.checkpoints<c.failAt;},this};
  }
};
int main() {
  for(size_t limit:{size_t(4096),size_t(37)}) {
    Card c;c.limit=limit;RemovableAppImage s;
    assert(s.read(&c.api,false,"/Apps/example/app.elf",c.hooks()));
    assert(c.refreshes==0);
    assert(s.size==c.data.size() && !memcmp(s.bytes,c.data.data(),s.size));
    c.data.assign(c.data.size(),0);c.present=false;
    assert(s.bytes[0]==0x5a && c.closes==1 && c.opens==1);free(s.bytes);
  }
  { Card c;RemovableAppImage first,second;
    assert(first.read(&c.api,false,"/Drivers/one.elf",c.hooks()));c.offset=0;
    assert(second.read(&c.api,false,"/Drivers/two.elf",c.hooks()));
    assert(c.refreshes==0 && c.opens==2 && c.closes==2);free(first.bytes);free(second.bytes);
  }
  { Card c;c.present=false;c.refreshReady=true;RemovableAppImage first;
    assert(first.read(&c.api,false,"/Drivers/one.elf",c.hooks()));assert(c.refreshes==1);free(first.bytes);
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
    assert(!s.read(&c.api,false,"/Apps/example/app.elf",h));
    assert((!s.bytes || mode==7) && !s.size && s.closeRetained==(mode==7));
    if(s.closeRetained){free(s.bytes);delete s.retainedSession;}
    assert(c.closes==(mode<2?0u:1u));
  }
  for(unsigned stage=1;stage<=7;++stage) {
    Card c;c.loseAt=stage;if(stage==2||stage==3){c.present=false;c.refreshReady=true;}
    risc_storage_volume_api_v1_ext ext{};ext.base=c.api;ext.base.struct_size=sizeof(ext);
    ext.handle_error=[](void*p,uint32_t,bool)->uint32_t{auto&c=*static_cast<Card*>(p);++c.errorCalls;if(c.loseAt==6)c.safe=false;return 0;};
    RemovableAppImage image;assert(!image.read(&ext.base,false,"/Apps/example/app.elf",c.hooks()));
    assert(image.closeRetained && image.retainedSession);
    assert(c.closes==(stage==7?1u:0u));
    if(stage<=3)assert(!c.opens && !c.reads && !c.errorCalls);
    if(stage==1)assert(!c.refreshes && c.readyCalls==1);
    if(stage==2)assert(c.refreshes==1 && c.readyCalls==1);
    if(stage==3)assert(c.refreshes==1 && c.readyCalls==2);
    if(stage==4)assert(c.opens==1 && !c.reads && !c.errorCalls);
    if(stage==5)assert(c.reads==1 && !c.errorCalls);
    if(stage==6)assert(c.reads==1 && c.errorCalls==1);
    free(image.bytes);delete image.retainedSession; // Test-only process teardown; never provider cleanup.
  }
  puts("Removable image: short reads, snapshot isolation, absent/busy media, bounds, truncation, timeout, OOM and close custody PASS");
}
