#pragma once
#include <RiscKeyValueV2.h>
#include "SdFirstKeyValue.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(ESP_PLATFORM) && defined(RISC_PAIRED_BANKS)
#include <esp_heap_caps.h>
#endif
namespace RiscStorage {
// Owner-task state. No filesystem calls: copied SD work is performed by the
// catalog worker. Each accepted change has an independent native recovery row.
// Allocation failure and missing checkpoints fail closed, never create defaults.
class ScalableSettings {
 public:
 struct Backend {
  void* context;
  int32_t (*anchorRead)(void*,uint32_t,void*,uint32_t,uint32_t*);
  int32_t (*anchorWrite)(void*,uint32_t,const void*,uint32_t);
  // Cursor is a lexicographic key, not a live NVS iterator. Empty starts a walk.
  int32_t (*next)(void*,uint32_t,bool delta,const char*after,char key[16],void*,uint32_t,uint32_t*);
  int32_t (*deltaWrite)(void*,uint32_t,const char*,const void*,uint32_t);
  int32_t (*deltaErase)(void*,uint32_t,const char*);
  int32_t (*legacySnapshot)(void*,uint32_t,void*,uint32_t,uint32_t*)=nullptr;
 };
 enum Status {Cold=0,Clean=1,Dirty=2,Uncertain=3,Restoring=5};
 struct Entry {Entry*next;uint32_t size;char key[16];uint8_t value[1];};
 static constexpr uint32_t Header=128,ValueMax=RISC_KEY_VALUE_V2_BLOB_MAX,DeltaHeader=8;
 struct State {
  Entry*head=nullptr;char owner[96]{};uint64_t generation=0;Status status=Cold;uint32_t count=0;
  uint32_t restoreStage=0,restoreSize=0,restoreCrc=0;uint64_t restoreGeneration=0;bool restoreMissing=false,restoreDirty=false;char cursor[16]{};
  ~State(){clear();} State()=default;State(const State&)=delete;State&operator=(const State&)=delete;
  void clear(){while(head){auto*p=head;head=p->next;std::free(p);}count=0;status=Cold;generation=0;owner[0]=0;restoreStage=0;cursor[0]=0;}
 };
 static void*allocate(size_t n){
#if defined(ESP_PLATFORM) && defined(RISC_PAIRED_BANKS)
  return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  return std::malloc(n);
#endif
 }
 struct Collection {
  State*items=nullptr;size_t count=0;
  ~Collection(){clear();}Collection()=default;Collection(const Collection&)=delete;Collection&operator=(const Collection&)=delete;
  void clear(){for(size_t i=0;i<count;++i)items[i].~State();std::free(items);items=nullptr;count=0;}
  bool create(size_t n){clear();if(!n||n>SIZE_MAX/sizeof(State))return false;items=static_cast<State*>(allocate(n*sizeof(State)));if(!items)return false;count=n;for(size_t i=0;i<n;++i)new(items+i)State();return true;}
  explicit operator bool()const{return items!=nullptr;}
  State&operator[](size_t i){return items[i];}const State&operator[](size_t i)const{return items[i];}
 };
 static bool key(const char*s){if(!s||!*s)return false;for(unsigned i=0;i<16;++i){char c=s[i];if(!c)return true;if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;}return false;}
 static bool owner(const char*s){return s&&*s&&std::strlen(s)<96&&!std::strchr(s,'/')&&!std::strchr(s,'\\')&&!std::strstr(s,"..");}
 static uint32_t word(const uint8_t*p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v;}
 static void word(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void wide(uint8_t*p,uint64_t v){word(p,uint32_t(v));word(p+4,uint32_t(v>>32));}
 static uint32_t crc(const void*data,uint32_t n){auto*p=static_cast<const uint8_t*>(data);uint32_t v=~0u;while(n--){v^=*p++;for(unsigned i=0;i<8;++i)v=(v>>1)^((v&1)?0xedb88320u:0);}return ~v;}
 static Entry**find(State&s,const char*k){auto**p=&s.head;while(*p&&std::strcmp((*p)->key,k)<0)p=&(*p)->next;return p;}
 static int32_t assign(State&s,const char*k,const void*v,uint32_t n){
  if(!key(k)||!v||!n||n>ValueMax)return RISC_KEY_VALUE_INVALID;
  auto**slot=find(s,k);auto*old=(*slot&&!std::strcmp((*slot)->key,k))?*slot:nullptr;
  auto*e=static_cast<Entry*>(allocate(sizeof(Entry)+n-1));if(!e)return RISC_KEY_VALUE_BUSY;
  e->next=old?old->next:*slot;e->size=n;std::strcpy(e->key,k);std::memcpy(e->value,v,n);*slot=e;
  if(old)std::free(old);else ++s.count;return 0;
 }
 static int32_t get(State&s,const char*k,void*out,uint32_t cap,uint32_t*n){
  if(n){*n=0;}if(!n||!key(k)||(!out&&cap))return RISC_KEY_VALUE_INVALID;
  if(s.status!=Clean&&s.status!=Dirty)return RISC_KEY_VALUE_BUSY;
  auto**p=find(s,k);if(!*p||std::strcmp((*p)->key,k))return RISC_KEY_VALUE_NOT_FOUND;
  *n=(*p)->size;if(cap<*n)return RISC_KEY_VALUE_BUFFER_SMALL;std::memcpy(out,(*p)->value,*n);return 0;
 }
 static int32_t put(State&s,const Backend&b,uint32_t ns,const char*k,const void*v,uint32_t n){
  if(!key(k)||!v||!n||n>ValueMax||!b.deltaWrite)return RISC_KEY_VALUE_INVALID;
  if(s.status!=Clean&&s.status!=Dirty)return RISC_KEY_VALUE_BUSY;
  auto**slot=find(s,k);auto*old=(*slot&&!std::strcmp((*slot)->key,k))?*slot:nullptr;
  if(old&&old->size==n&&!std::memcmp(old->value,v,n))return 0;
  if(s.generation==UINT64_MAX)return RISC_KEY_VALUE_IO;
  auto*e=static_cast<Entry*>(allocate(sizeof(Entry)+n-1));if(!e)return RISC_KEY_VALUE_BUSY;
  e->next=old?old->next:*slot;e->size=n;std::strcpy(e->key,k);std::memcpy(e->value,v,n);
  uint8_t row[DeltaHeader+ValueMax];wide(row,s.generation+1);std::memcpy(row+DeltaHeader,v,n);
  int rc=b.deltaWrite(b.context,ns,k,row,n+DeltaHeader);
  if(rc){std::free(e);if(rc!=RISC_KEY_VALUE_BUSY)s.status=Uncertain;return rc;}
  *slot=e;if(old)std::free(old);else ++s.count;++s.generation;s.status=Dirty;return 0;
 }
 static void header(uint8_t*out,uint32_t ns,const State&s,uint32_t count,uint32_t size){
  std::memset(out,0,Header);std::memcpy(out,"RISCKV2",7);word(out+8,ns);wide(out+16,s.generation);std::strcpy(reinterpret_cast<char*>(out+24),s.owner);word(out+120,count);word(out+124,size);
 }
 static bool validHeader(const uint8_t*p,uint32_t ns,const char*id){return !std::memcmp(p,"RISCKV2",8)&&word(p+8)==ns&&!word(p+12)&&std::memchr(p+24,0,96)&&!std::strcmp(reinterpret_cast<const char*>(p+24),id);}
 // Caller owns returned memory; this immutable byte snapshot may be written in
 // any number of <=4096-byte copied worker operations while later writes occur.
 static int32_t snapshot(const State&s,uint32_t ns,uint8_t**out,uint32_t*size,uint64_t*generation){
  if(!out||!size||!generation){return RISC_KEY_VALUE_INVALID;}*out=nullptr;*size=0;
  if(s.status!=Clean&&s.status!=Dirty)return RISC_KEY_VALUE_BUSY;
  uint64_t total=Header+4;for(auto*p=s.head;p;p=p->next)total+=20+p->size;
  if(total>UINT32_MAX)return RISC_KEY_VALUE_IO;
  auto*bytes=static_cast<uint8_t*>(allocate(size_t(total)));if(!bytes)return RISC_KEY_VALUE_BUSY;
  header(bytes,ns,s,s.count,uint32_t(total));uint32_t at=Header;
  for(auto*p=s.head;p;p=p->next){std::memset(bytes+at,0,16);std::strcpy(reinterpret_cast<char*>(bytes+at),p->key);word(bytes+at+16,p->size);std::memcpy(bytes+at+20,p->value,p->size);at+=20+p->size;}
  word(bytes+at,crc(bytes,at));*out=bytes;*size=uint32_t(total);*generation=s.generation;return 0;
 }
 static int32_t advanceRestore(State&s,const Backend&b,uint32_t ns){
  char k[16]{};uint8_t row[DeltaHeader+ValueMax];uint32_t n=0;
  int rc=b.next(b.context,ns,s.restoreStage==2,s.cursor,k,row,sizeof row,&n);
  if(rc==RISC_KEY_VALUE_BUSY)return rc;
  if(rc==RISC_KEY_VALUE_NOT_FOUND){
   if(s.restoreStage==1){s.restoreStage=2;s.cursor[0]=0;return RISC_KEY_VALUE_BUSY;}
   s.restoreStage=0;s.status=s.restoreDirty?Dirty:Clean;return 0;
  }
  if(rc||!key(k)||std::strcmp(k,s.cursor)<=0){s.status=Uncertain;return rc?rc:RISC_KEY_VALUE_IO;}
  if(s.restoreStage==1){auto**slot=find(s,k);if(!*slot||std::strcmp((*slot)->key,k))rc=assign(s,k,row,n);}
  else {
   if(n<=DeltaHeader||n>sizeof row||!wide(row)){s.status=Uncertain;return RISC_KEY_VALUE_IO;}
   const uint64_t g=wide(row);if(g>s.restoreGeneration){rc=assign(s,k,row+DeltaHeader,n-DeltaHeader);if(g>s.generation)s.generation=g;s.restoreDirty=true;}
  }
  if(rc){if(rc!=RISC_KEY_VALUE_BUSY)s.status=Uncertain;return rc;}
  std::strcpy(s.cursor,k);return RISC_KEY_VALUE_BUSY;
 }
 static int32_t restore(State&s,const Backend&b,uint32_t ns,const char*id,const void*sd,uint32_t size,bool missing){
  if(!owner(id)||!b.anchorRead||!b.anchorWrite||!b.next||(missing&&size)||(!missing&&!sd))return RISC_KEY_VALUE_INVALID;
  if(s.owner[0]&&std::strcmp(s.owner,id))return RISC_KEY_VALUE_CONTEXT;
  const uint32_t inputCrc=!missing&&size>=4?word(static_cast<const uint8_t*>(sd)+size-4):0;
  if(s.status==Restoring&&s.restoreStage&&s.restoreMissing==missing&&s.restoreSize==size&&s.restoreCrc==inputCrc)return advanceRestore(s,b,ns);
  s.clear();std::strcpy(s.owner,id);s.status=Restoring;s.restoreMissing=missing;s.restoreSize=size;s.restoreCrc=inputCrc;
  uint8_t anchor[Header+4]{};uint32_t n=0;int rc=b.anchorRead(b.context,ns,anchor,sizeof(anchor),&n);
  if(rc!=0&&rc!=RISC_KEY_VALUE_NOT_FOUND)return rc;
  const bool anchored=rc==0;
  if(anchored&&(n!=sizeof(anchor)||!validHeader(anchor,ns,id)||word(anchor+Header)!=crc(anchor,Header)))return RISC_KEY_VALUE_IO;
  if(anchored&&missing)return RISC_KEY_VALUE_IO; // never fall back to stale original NVS
  bool legacyFormat=!missing&&size>=8&&!std::memcmp(sd,"RISCKV1",8);
  bool legacyJournal=false;uint8_t oldJournal[SdFirstKeyValue::Bytes];uint32_t oldSize=0;
  if(!anchored&&(missing||legacyFormat)&&b.legacySnapshot){rc=b.legacySnapshot(b.context,ns,oldJournal,sizeof oldJournal,&oldSize);if(rc!=0&&rc!=RISC_KEY_VALUE_NOT_FOUND)return rc;legacyJournal=rc==0;}
  if(!missing&&!legacyFormat){
   auto*p=static_cast<const uint8_t*>(sd);
   if(size<Header+4||!validHeader(p,ns,id)||word(p+124)!=size||word(p+size-4)!=crc(p,size-4))return RISC_KEY_VALUE_IO;
   if(anchored&&wide(p+16)<wide(anchor+16))return RISC_KEY_VALUE_IO;
   s.generation=wide(p+16);uint32_t at=Header;
   for(uint32_t i=0;i<word(p+120);++i){
    if(at>size-4||size-4-at<20||!std::memchr(p+at,0,16))return RISC_KEY_VALUE_IO;
    uint32_t len=word(p+at+16);if(!len||len>ValueMax||len>size-4-at-20)return RISC_KEY_VALUE_IO;
    const char*k=reinterpret_cast<const char*>(p+at);auto**slot=find(s,k);if(*slot&&!std::strcmp((*slot)->key,k))return RISC_KEY_VALUE_IO;
    rc=assign(s,k,p+at+20,len);if(rc)return rc;at+=20+len;
   }
   if(at!=size-4)return RISC_KEY_VALUE_IO;
  }

  if(legacyFormat||legacyJournal){
   SdFirstKeyValue::State old{};bool isAnchor=false;
   if(legacyFormat&&!SdFirstKeyValue::decode(old,ns,id,sd,size,isAnchor))return RISC_KEY_VALUE_IO;
   if(isAnchor)return RISC_KEY_VALUE_IO;
   if(legacyJournal){SdFirstKeyValue::State journal{};if(!SdFirstKeyValue::decode(journal,ns,id,oldJournal,oldSize,isAnchor))return RISC_KEY_VALUE_IO;
    if(isAnchor){if(!legacyFormat||old.generation!=journal.generation)return RISC_KEY_VALUE_IO;}
    else{if(legacyFormat&&(old.generation>journal.generation||(old.generation==journal.generation&&(oldSize!=size||std::memcmp(sd,oldJournal,size)))))return RISC_KEY_VALUE_IO;old=journal;}
   }
   for(unsigned i=0;i<old.count;++i){rc=assign(s,old.entries[i].key,old.entries[i].value,old.entries[i].size);if(rc)return rc;}
   // Old snapshot generations are a separate epoch. New per-key deltas start at one.
   s.generation=0;
  }
  s.restoreGeneration=s.generation;s.restoreDirty=missing||legacyFormat||legacyJournal;
  s.restoreStage=(missing||legacyFormat||legacyJournal)?1:2;s.cursor[0]=0;
  return advanceRestore(s,b,ns);
 }
 // Call after exact SD readback and checked worker close. A concurrent newer
 // write keeps its delta. One record is cleaned per call; BUSY means retry.
 static int32_t acknowledge(State&s,const Backend&b,uint32_t ns,uint64_t generation){
  if(s.status!=Clean&&s.status!=Dirty)return RISC_KEY_VALUE_BUSY;
  if(generation>s.generation||!b.deltaErase)return RISC_KEY_VALUE_INVALID;
  uint8_t anchor[Header+4];header(anchor,ns,s,0,Header+4);wide(anchor+16,generation);word(anchor+Header,crc(anchor,Header));
  uint8_t prior[Header+4];uint32_t priorSize=0;int rc=b.anchorRead(b.context,ns,prior,sizeof prior,&priorSize);
  if(rc!=0&&rc!=RISC_KEY_VALUE_NOT_FOUND)return rc;
  if(rc==0&&(priorSize!=sizeof prior||!validHeader(prior,ns,s.owner)||word(prior+Header)!=crc(prior,Header))){s.status=Uncertain;return RISC_KEY_VALUE_IO;}
  if(rc==0&&wide(prior+16)>generation)return RISC_KEY_VALUE_BUSY;
  if(rc!=0||std::memcmp(prior,anchor,sizeof anchor))rc=b.anchorWrite(b.context,ns,anchor,sizeof(anchor));
  if(rc){if(rc!=RISC_KEY_VALUE_BUSY)s.status=Uncertain;return rc;}
  char after[16]{},k[16]{};uint8_t delta[DeltaHeader+ValueMax];uint32_t n=0;
  for(;;){rc=b.next(b.context,ns,true,after,k,delta,sizeof(delta),&n);if(rc==RISC_KEY_VALUE_NOT_FOUND)break;if(rc)return rc;
   if(n<=DeltaHeader||n>sizeof(delta)||!key(k)||std::strcmp(k,after)<=0)return RISC_KEY_VALUE_IO;
   if(wide(delta)<=generation){rc=b.deltaErase(b.context,ns,k);return rc?rc:RISC_KEY_VALUE_BUSY;}std::strcpy(after,k);
  }
  s.status=s.generation==generation?Clean:Dirty;return 0;
 }
};
}
