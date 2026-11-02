#include "runtime_file_image_fixture.inc"
#include <fstream>
#include <iterator>
#include <string>
struct EnvelopeContext;
static EnvelopeContext* current=nullptr;
struct EnvelopeSource:Source {
 risc_storage_files_v1 original;
 EnvelopeContext* context=nullptr;
 EnvelopeSource();
};
struct EnvelopeContext {
 EnvelopeSource* source=nullptr;
 bool safe=true,timeout=false,observeFail=false,oom=false;
 size_t events=0,loseAt=0,allocated=0,allocs=0,freed=0;
 std::vector<uint8_t> observed;
 void event(){assert(safe);if(++events==loseAt)safe=false;}
};
static void sourceEvent(EnvelopeSource&s){if(s.context)s.context->event();}
EnvelopeSource::EnvelopeSource(){
 original=api;
 api.open=[](void*c,const char*p,uint32_t f,uint32_t*h)->int32_t{auto&s=*static_cast<EnvelopeSource*>(c);auto rc=s.original.open(c,p,f,h);sourceEvent(s);return rc;};
 api.read=[](void*c,uint32_t h,void*p,uint32_t n,uint32_t*a)->int32_t{auto&s=*static_cast<EnvelopeSource*>(c);auto rc=s.original.read(c,h,p,n,a);sourceEvent(s);return rc;};
 api.info=[](void*c,uint32_t h,uint64_t*n,uint64_t*o)->int32_t{auto&s=*static_cast<EnvelopeSource*>(c);auto rc=s.original.info(c,h,n,o);sourceEvent(s);return rc;};
 api.close=[](void*c,uint32_t h)->int32_t{auto&s=*static_cast<EnvelopeSource*>(c);auto rc=s.original.close(c,h);sourceEvent(s);return rc;};
}
static RuntimeFileImage::Hooks envelopeHooks(EnvelopeContext& c){
 current=&c;
 return {
 [](size_t n)->void*{auto&c=*current;c.event();++c.allocs;c.allocated=n;if(c.oom)return nullptr;auto*p=static_cast<uint8_t*>(malloc(n+16));assert(p);memset(p,0xa5,n+16);return p+8;},
 [](void*p){auto&c=*current;assert(c.safe);auto*base=static_cast<uint8_t*>(p)-8;for(unsigned i=0;i<8;++i)assert(base[i]==0xa5&&base[c.allocated+8+i]==0xa5);++c.freed;free(base);},
 [](void*p)->bool{return static_cast<EnvelopeContext*>(p)->safe;},
 [](void*p,size_t)->bool{auto&c=*static_cast<EnvelopeContext*>(p);c.event();return !c.timeout;},&c,
 [](void*p,const uint8_t*bytes,size_t n)->bool{auto&c=*static_cast<EnvelopeContext*>(p);c.event();c.observed.insert(c.observed.end(),bytes,bytes+n);return !c.observeFail;}
 };
}
static std::vector<uint8_t> readBytes(const char* path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static unsigned envelopeChecks;
static bool runImage(const std::vector<uint8_t>& stored,const std::vector<uint8_t>& raw,bool allow,size_t chunk=4096,size_t loseAt=0,unsigned fault=0,size_t* events=nullptr){
 EnvelopeSource source;source.data=stored;source.shortRead=chunk;
 EnvelopeContext context;context.source=&source;context.loseAt=loseAt;
 source.context=&context;source.safety=&context.safe;
 context.oom=fault==1;source.failClose=fault==2;source.retainRead=fault==3;source.oversize=fault==4;source.grow=fault==5;
 context.observeFail=fault==6;context.timeout=fault==7;
 RuntimeFileSources::Entry entry{"/active",&source.files};RuntimeFileSources map(&entry,1);
 RuntimeFileImage image;auto hooks=envelopeHooks(context);
 bool ok=image.read(RuntimeFileView(&map,"/active"),"/active/image.elf",hooks,
                    fault==8?stored.size()+1:stored.size(),fault==9?51:2u*1024u*1024u,allow);
 ++envelopeChecks;if(events)*events=context.events;
 if(ok){
  assert(!loseAt&&!fault&&image.size==raw.size()&&image.physicalSize==stored.size());
  assert(!memcmp(image.bytes,raw.data(),raw.size())&&context.observed==stored);
  assert(context.allocs==1&&context.allocated==raw.size()&&!context.freed&&source.closes==1);
  assert(image.encoded==RiscStorage::ImageEnvelope::magic(stored.data(),stored.size()));
  hooks.free(image.bytes);
 }else{
  assert(!image.size);
  if(loseAt==1){assert(!source.opens&&!image.retained&&!image.bytes&&!context.freed&&context.events==1);return false;}
  if(loseAt||fault==2||fault==3||fault==4){
   assert(image.retained&&map.retained()&&!context.freed);
   if(loseAt)assert(context.events==loseAt);
   size_t before=context.events;RuntimeFileImage retry;
   assert(!retry.read(RuntimeFileView(&map,"/active"),"/active/image.elf",hooks,0,2u*1024u*1024u,true));
   assert(before==context.events);
   // Test-only release after invocation termination; production owns retained buffer.
   if(image.bytes){context.safe=true;hooks.free(image.bytes);}
  }else assert(!image.retained&&!image.bytes);
 }
 return ok;
}
int main(int argc,char**argv){
 assert(argc>=3&&(argc-1)%2==0);inherited_raw_image_main();
 std::vector<uint8_t> sample,canonical;
 for(int i=1;i<argc;i+=2){
  auto stored=readBytes(argv[i]),raw=readBytes(argv[i+1]);assert(raw.size()>=52);
  for(size_t chunk:{size_t(1),size_t(17),size_t(4096)})assert(runImage(stored,raw,true,chunk));
  assert(runImage(raw,raw,true));assert(runImage(raw,raw,false));
  assert(!runImage(stored,raw,false)); // default-off/pinned/provider path
  if(sample.empty()&&stored.size()>52&&stored.size()<8192){sample=stored;canonical=raw;}
 }
 assert(!sample.empty());
 size_t events=0;assert(runImage(sample,canonical,true,17,0,0,&events));
 for(size_t at=1;at<=events;++at)assert(!runImage(sample,canonical,true,17,at));
 for(unsigned fault=1;fault<=9;++fault)assert(!runImage(sample,canonical,true,17,0,fault));
 for(size_t offset:{size_t(8),size_t(12),size_t(16),size_t(20),size_t(24),size_t(28)}){
  auto bad=sample;bad[offset]^=1;assert(!runImage(bad,canonical,true));
 }
 for(size_t n:{size_t(0),size_t(7),size_t(31),size_t(32),sample.size()-1}){
  auto shortFile=sample;shortFile.resize(n);assert(!runImage(shortFile,canonical,true));
 }
 auto extra=sample;extra.push_back(0);assert(!runImage(extra,canonical,true));
 printf("Actual RuntimeFileImage envelope: %u checks; raw unchanged, same-open physical observation, exact decoded bytes, every custody boundary PASS\n",envelopeChecks);
 return 0;
}
