#pragma once
#include <RiscKeyValueV1.h>
#include <cstdint>
#include <cstring>
namespace RiscStorage {
// Private installed-app settings only. SD is the complete checkpoint; native
// journal storage contains the latest uncheckpointed snapshot or a small anchor.
// The caller owns memory, serializes owner access, and performs SD I/O elsewhere.
class SdFirstKeyValue {
 public:
  static constexpr unsigned Keys=16,OwnerBytes=96,Header=128,EntryBytes=84,Bytes=Header+Keys*EntryBytes;
  enum Status {Cold=0,Clean=1,Dirty=2,Uncertain=3,LegacyNative=4};
  struct Entry {char key[16]{};uint32_t size=0;uint8_t value[64]{};};
  struct State {char owner[OwnerBytes]{};uint64_t generation=0;uint32_t count=0;Status status=Cold;Entry entries[Keys]{};};
  struct Workspace {State decoded{},candidate{};uint8_t bytes[Bytes]{};};
  struct Journal {
   void*context;
   int32_t(*read)(void*,uint32_t,void*,uint32_t,uint32_t*);
   int32_t(*write)(void*,uint32_t,const void*,uint32_t);
   int32_t(*legacy)(void*,uint32_t,const char*,void*,uint32_t,uint32_t*);
   int32_t(*legacyNamespace)(void*,uint32_t,bool*)=nullptr;
  };
  static bool keyValid(const char*key){
   if(!key||!*key)return false;
   for(unsigned i=0;i<16;++i){char c=key[i];if(!c)return true;if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;}
   return false;
  }
  static bool ownerValid(const char*owner){
   if(!owner||!*owner)return false;
   for(unsigned i=0;i<OwnerBytes;++i){char c=owner[i];if(!c)return true;if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;}
   return false;
  }
  static uint32_t crc(const uint8_t*p,unsigned n){uint32_t v=~0u;for(unsigned i=0;i<n;++i){v^=p[i];for(unsigned j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320u:0);}return ~v;}
  static void word(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
  static uint32_t word(const uint8_t*p){uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(p[i])<<(i*8);return n;}
  static unsigned encode(const State&s,uint32_t ns,bool anchor,uint8_t*out){
   const unsigned count=anchor?0:s.count,n=Header+count*EntryBytes;std::memset(out,0,n);
   std::memcpy(out,"RISCKV1",7);word(out+8,ns);word(out+12,anchor?1:0);
   word(out+16,uint32_t(s.generation));word(out+20,uint32_t(s.generation>>32));std::memcpy(out+24,s.owner,OwnerBytes);word(out+120,count);
   for(unsigned i=0;i<count;++i){auto*p=out+124+i*EntryBytes;std::memcpy(p,s.entries[i].key,16);word(p+16,s.entries[i].size);std::memcpy(p+20,s.entries[i].value,64);}
   word(out+n-4,crc(out,n-4));return n;
  }
  static bool decode(State&out,uint32_t ns,const char*owner,const void*data,unsigned n,bool&anchor){
   if(!data||n<Header||n>Bytes)return false;
   const auto*p=static_cast<const uint8_t*>(data);const unsigned count=word(p+120);
   if(std::memcmp(p,"RISCKV1",8)||word(p+8)!=ns||word(p+12)>1||count>Keys||n!=Header+count*EntryBytes||
      !std::memchr(p+24,0,OwnerBytes)||std::strcmp(reinterpret_cast<const char*>(p+24),owner)||word(p+n-4)!=crc(p,n-4))return false;
   anchor=word(p+12)!=0;if(anchor&&count)return false;
   out={};std::strcpy(out.owner,owner);out.generation=uint64_t(word(p+16))|(uint64_t(word(p+20))<<32);out.count=count;
   for(unsigned i=0;i<count;++i){const auto*entry=p+124+i*EntryBytes;auto&e=out.entries[i];
    std::memcpy(e.key,entry,16);e.size=word(entry+16);if(!keyValid(e.key)||!e.size||e.size>64)return false;
    for(unsigned j=0;j<i;++j)if(!std::strcmp(e.key,out.entries[j].key))return false;
    std::memcpy(e.value,entry+20,64);
   }
   return true;
  }
  static int32_t restore(State&s,Workspace&w,const Journal&journal,uint32_t ns,const char*owner,const void*sd,unsigned size,bool missing){
   if(!ownerValid(owner)||!journal.read||!journal.write||(!missing&&!sd)|| (missing&&size))return RISC_KEY_VALUE_INVALID;
   if(s.owner[0]&&std::strcmp(s.owner,owner))return RISC_KEY_VALUE_CONTEXT;
   State&card=w.candidate;card={};std::strcpy(card.owner,owner);bool anchor=false;
   uint32_t n=0;int rc=journal.read(journal.context,ns,w.bytes,Bytes,&n);
   if(rc!=RISC_KEY_VALUE_OK&&rc!=RISC_KEY_VALUE_NOT_FOUND)return rc;
   // Existing namespace storage keeps its original backend and unlimited key
   // count. Do not squeeze delivered data into the new bounded SD snapshot.
   if(rc==RISC_KEY_VALUE_NOT_FOUND&&journal.legacyNamespace){
    bool exists=false;const int found=journal.legacyNamespace(journal.context,ns,&exists);
    if(found!=RISC_KEY_VALUE_OK)return found;
    if(exists){card.status=LegacyNative;s=card;return RISC_KEY_VALUE_OK;}
   }
   if(!missing&&(!decode(card,ns,owner,sd,size,anchor)||anchor))return RISC_KEY_VALUE_IO;
   if(rc==RISC_KEY_VALUE_OK){
    if(!decode(w.decoded,ns,owner,w.bytes,n,anchor))return RISC_KEY_VALUE_IO;
    if(anchor){if(missing||card.generation!=w.decoded.generation)return RISC_KEY_VALUE_IO;}
    else {
     if(!missing&&(card.generation>w.decoded.generation||(card.generation==w.decoded.generation&&(n!=size||std::memcmp(sd,w.bytes,n)))))return RISC_KEY_VALUE_IO;
     card=w.decoded;card.status=Dirty;s=card;return RISC_KEY_VALUE_OK;
    }
   }
   if(rc==RISC_KEY_VALUE_NOT_FOUND){const unsigned bytes=encode(card,ns,!missing,w.bytes);rc=journal.write(journal.context,ns,w.bytes,bytes);if(rc!=RISC_KEY_VALUE_OK)return rc;}
   card.status=missing?Dirty:Clean;s=card;return RISC_KEY_VALUE_OK;
  }
  static int32_t put(State&s,Workspace&w,const Journal&journal,uint32_t ns,const char*key,const void*data,unsigned size){
   if(!keyValid(key)||!data||!size||size>64)return RISC_KEY_VALUE_INVALID;
   if(s.status==Cold||s.status==Uncertain)return RISC_KEY_VALUE_BUSY;
   if(s.status==LegacyNative)return RISC_KEY_VALUE_CONTEXT;
   unsigned slot=0;for(;slot<s.count;++slot)if(!std::strcmp(key,s.entries[slot].key))break;
   if(slot<s.count&&s.entries[slot].size==size&&!std::memcmp(s.entries[slot].value,data,size))return RISC_KEY_VALUE_OK;
   if(slot==Keys||s.generation==UINT64_MAX)return RISC_KEY_VALUE_IO;
   w.candidate=s;auto&e=w.candidate.entries[slot];e={};std::strcpy(e.key,key);e.size=size;std::memcpy(e.value,data,size);
   if(slot==s.count)++w.candidate.count;
   ++w.candidate.generation;w.candidate.status=Dirty;
   const unsigned n=encode(w.candidate,ns,false,w.bytes);const int rc=journal.write(journal.context,ns,w.bytes,n);
   if(rc==RISC_KEY_VALUE_OK)s=w.candidate;
   else if(rc!=RISC_KEY_VALUE_BUSY)s.status=Uncertain;
   return rc;
  }
  static int32_t get(State&s,Workspace&w,const Journal&journal,uint32_t ns,const char*key,void*out,unsigned capacity,uint32_t*size){
   if(size)*size=0;
   if(!size||!keyValid(key)||(!out&&capacity))return RISC_KEY_VALUE_INVALID;
   if(s.status==Cold||s.status==Uncertain)return RISC_KEY_VALUE_BUSY;
   if(s.status==LegacyNative)return RISC_KEY_VALUE_CONTEXT;
   for(unsigned i=0;i<s.count;++i)if(!std::strcmp(key,s.entries[i].key)){
    const auto&e=s.entries[i];*size=e.size;if(capacity<e.size)return RISC_KEY_VALUE_BUFFER_SMALL;
    std::memcpy(out,e.value,e.size);return RISC_KEY_VALUE_OK;
   }
   // Existing per-key NVS values are preserved and migrated lazily when read.
   // New journal storage is in a separate native namespace, never an app key.
   if(!journal.legacy)return RISC_KEY_VALUE_NOT_FOUND;
   uint8_t value[64]{};uint32_t n=0;int rc=journal.legacy(journal.context,ns,key,value,sizeof(value),&n);
   if(rc!=RISC_KEY_VALUE_OK)return rc;
   if(!n||n>sizeof(value))return RISC_KEY_VALUE_IO;
   rc=put(s,w,journal,ns,key,value,n);if(rc!=RISC_KEY_VALUE_OK)return rc;
   *size=n;if(capacity<n)return RISC_KEY_VALUE_BUFFER_SMALL;std::memcpy(out,value,n);return RISC_KEY_VALUE_OK;
  }
  static int32_t snapshot(const State&s,uint32_t ns,void*out,unsigned capacity,uint32_t*size,uint64_t*generation){
   if(size)*size=0;
   if(!out||!size||!generation)return RISC_KEY_VALUE_INVALID;
   if(s.status==Cold||s.status==Uncertain)return RISC_KEY_VALUE_BUSY;
   if(s.status==LegacyNative)return RISC_KEY_VALUE_CONTEXT;
   const unsigned n=Header+s.count*EntryBytes;if(capacity<n){*size=n;return RISC_KEY_VALUE_BUFFER_SMALL;}
   *size=encode(s,ns,false,static_cast<uint8_t*>(out));*generation=s.generation;return RISC_KEY_VALUE_OK;
  }
  static int32_t acknowledge(State&s,Workspace&w,const Journal&journal,uint32_t ns,uint64_t generation){
   if(s.status==Cold||s.status==Uncertain||s.generation!=generation)return RISC_KEY_VALUE_BUSY;
   const unsigned n=encode(s,ns,true,w.bytes);int rc=journal.write(journal.context,ns,w.bytes,n);
   if(rc==RISC_KEY_VALUE_OK)s.status=Clean;else if(rc!=RISC_KEY_VALUE_BUSY)s.status=Uncertain;return rc;
  }
};
}
