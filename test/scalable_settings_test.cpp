#include "runtime/storage/ScalableSettings.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
using S=RiscStorage::ScalableSettings;using Bytes=std::vector<uint8_t>;
std::map<std::string,Bytes> original,deltas;Bytes anchor;int failure=0;bool persisted=false;
S::Backend backend={nullptr,
 [](void*,uint32_t,void*p,uint32_t cap,uint32_t*n)->int32_t{if(anchor.empty())return RISC_KEY_VALUE_NOT_FOUND;assert(cap>=anchor.size());memcpy(p,anchor.data(),anchor.size());*n=anchor.size();return 0;},
 [](void*,uint32_t,const void*p,uint32_t n)->int32_t{anchor.assign((const uint8_t*)p,(const uint8_t*)p+n);return 0;},
 [](void*,uint32_t,bool delta,const char*after,char key[16],void*p,uint32_t cap,uint32_t*n)->int32_t{auto&m=delta?deltas:original;auto i=m.upper_bound(after);if(i==m.end())return RISC_KEY_VALUE_NOT_FOUND;if(cap<i->second.size())return RISC_KEY_VALUE_BUFFER_SMALL;strcpy(key,i->first.c_str());*n=i->second.size();memcpy(p,i->second.data(),*n);return 0;},
 [](void*,uint32_t,const char*k,const void*p,uint32_t n)->int32_t{if(!failure||persisted)deltas[k]=Bytes((const uint8_t*)p,(const uint8_t*)p+n);return failure;},
 [](void*,uint32_t,const char*k)->int32_t{deltas.erase(k);return 0;}};
int restore(S::State&s,const S::Backend&b,uint32_t ns,const char*id,const void*p,uint32_t n,bool missing){int rc;unsigned steps=0;do{rc=S::restore(s,b,ns,id,p,n,missing);assert(++steps<10000);}while(rc==RISC_KEY_VALUE_BUSY);return rc;}
Bytes snap(S::State&s,uint64_t&g){uint8_t*p;uint32_t n;assert(S::snapshot(s,3000,&p,&n,&g)==0);Bytes b(p,p+n);free(p);return b;}
void ack(S::State&s,uint64_t g){int rc;unsigned i=0;do{rc=S::acknowledge(s,backend,3000,g);assert(++i<10000);}while(rc==RISC_KEY_VALUE_BUSY);assert(!rc);}
void verify(S::State&s,const char*k,const Bytes&v){Bytes out(2048);uint32_t n;assert(!S::get(s,k,out.data(),out.size(),&n));out.resize(n);assert(out==v);}
std::vector<uint8_t> legacyJournal;
int main(){
 for(unsigned i=0;i<513;++i){char key[16];snprintf(key,sizeof key,"key%04u",i);original[key]=Bytes(i%2048+1,uint8_t(i));}
 const auto before=original;S::State s;assert(!restore(s,backend,3000,"app",nullptr,0,true)&&s.count==513);for(auto&v:original)verify(s,v.first.c_str(),v.second);
 uint64_t g;auto disk=snap(s,g);assert(disk.size()>4096);ack(s,g);assert(s.status==S::Clean&&original==before);
 S::State cold;assert(restore(cold,backend,3000,"app",nullptr,0,true)==RISC_KEY_VALUE_IO); // no stale native fallback
 assert(!restore(cold,backend,3000,"app",disk.data(),disk.size(),false));
 // Remove/export SD: accepted writes only touch the durable per-key journal.
 for(unsigned i=513;i<1027;++i){char key[16];snprintf(key,sizeof key,"key%04u",i);assert(!S::put(cold,backend,3000,key,"new",3));}
 S::State resumed;assert(!restore(resumed,backend,3000,"app",disk.data(),disk.size(),false)&&resumed.count==1027);
 auto next=snap(resumed,g);assert(!S::put(resumed,backend,3000,"concurrent","safe",4));ack(resumed,g);assert(resumed.status==S::Dirty&&deltas.count("concurrent"));
 S::State restart;assert(!restore(restart,backend,3000,"app",next.data(),next.size(),false));verify(restart,"concurrent",Bytes{'s','a','f','e'});
 failure=RISC_KEY_VALUE_IO;persisted=true;assert(S::put(restart,backend,3000,"uncertain","saved",5)==failure&&restart.status==S::Uncertain);
 failure=0;assert(!restore(restart,backend,3000,"app",next.data(),next.size(),false));verify(restart,"uncertain",Bytes{'s','a','v','e','d'});
 auto corrupt=next;corrupt.back()^=1;assert(restore(s,backend,3000,"app",corrupt.data(),corrupt.size(),false)==RISC_KEY_VALUE_IO);
 assert(original==before);
 // Import a pending v1 snapshot over untouched native migration originals.
 anchor.clear();deltas.clear();S::State migrated;RiscStorage::SdFirstKeyValue::State old{};strcpy(old.owner,"app");old.generation=9;old.count=1;strcpy(old.entries[0].key,"key0000");old.entries[0].size=7;memcpy(old.entries[0].value,"pending",7);
 legacyJournal.resize(RiscStorage::SdFirstKeyValue::Bytes);legacyJournal.resize(RiscStorage::SdFirstKeyValue::encode(old,3000,false,legacyJournal.data()));
 auto compatibility=backend;compatibility.legacySnapshot=[](void*,uint32_t,void*p,uint32_t cap,uint32_t*n)->int32_t{assert(cap>=legacyJournal.size());*n=legacyJournal.size();memcpy(p,legacyJournal.data(),*n);return 0;};
 assert(!restore(migrated,compatibility,3000,"app",nullptr,0,true)&&migrated.count==513);verify(migrated,"key0000",Bytes{'p','e','n','d','i','n','g'});
 auto migratedDisk=snap(migrated,g);ack(migrated,g);assert(!S::put(migrated,compatibility,3000,"key0000","newer",5));
 S::State again;assert(!restore(again,compatibility,3000,"app",migratedDisk.data(),migratedDisk.size(),false));verify(again,"key0000",Bytes{'n','e','w','e','r'});
 assert(original==before);puts("Scalable settings: 513 migrated opaque values, 1027 keys, absent-media journal, concurrent checkpoint, cold recovery, uncertain persistence and CRC rejection PASS");
}
