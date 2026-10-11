#include "native_replica_sd_fixture.h"
#include "experimental/native_replica/CheckedSdReplicaReader.h"
using R=RiscExperimental::CheckedSdReplicaReader;
struct WipingAlloc {
 bool fail=false;unsigned allocations=0;size_t peak=0,live=0;std::map<void*,size_t> pointers;
 C::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&a=*static_cast<WipingAlloc*>(p);++a.allocations;if(a.fail)return nullptr;auto*out=malloc(n);if(out){a.pointers[out]=n;a.live+=n;a.peak=std::max(a.peak,a.live);memset(out,0xa7,n);}return out;},[](void*p,void*out){auto&a=*static_cast<WipingAlloc*>(p);size_t n=a.pointers.at(out);for(size_t i=0;i<n;++i)assert(static_cast<uint8_t*>(out)[i]==0);a.live-=n;a.pointers.erase(out);free(out);}};}
 ~WipingAlloc(){assert(!live&&pointers.empty());}
};
static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static Bytes selectedMap(const Files&f){const auto&idx=f.at(Index);C::Identity id{};id.generation=7;memcpy(id.digest,idx.data()+48,32);Bytes b;for(unsigned page=0;b.size()<word(idx.data()+12);++page){auto&p=f.at(pagePath(id,page));b.insert(b.end(),p.begin(),p.end());}return b;}
static void retryRead(Native&n,Files&f,unsigned count=100){C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;Sd s(f);Hash h;WipingAlloc a;R r(c,h.api(),s.guard(),a.api());assert(r.start(s.volume())==0);s.run(r);assert(r.state()==R::State::Verified&&!r.exitSafe());R::Candidate out;assert(r.take(&out)==0&&out.valid()&&out.namespaceId()==6&&out.count()==count&&r.exitSafe());assert(out.mapSize()==count*52&&out.identity().generation==7);assert(n.calls==calls&&c.pendingCount()==1&&get(c,"edited")=="local");}
static void basics(){
 Native n;Files f;seed(n,f,1025);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;const auto expected=selectedMap(f);WipingAlloc a;R::Candidate out;
 {Sd s(f);Hash h;R r(c,h.api(),s.guard(),a.api());assert(r.start(s.volume())==0);for(unsigned i=0;i<1000;++i)s.tick(r,false);assert(s.io==0);assert(r.take(&out)==C::Stale&&!out.valid());s.run(r);assert(r.state()==R::State::Verified&&s.ends&&!r.exitSafe());assert(r.take(&out)==0&&r.exitSafe()&&out.valid());assert(out.count()==1025&&out.mapSize()==expected.size()&&!memcmp(out.map(),expected.data(),expected.size()));assert(n.calls==calls&&c.pendingCount()==1);}
 // Candidate owns its bytes after all reader/hash/SD objects are destroyed.
 f.clear();edit(c,"newer");assert(out.mapSize()==expected.size()&&!memcmp(out.map(),expected.data(),expected.size()));assert(a.peak==c.imageCapacity()-C::Header-4);out.reset();assert(!a.live&&!out.valid());
 puts("SD reader: 1025 keys, owned immutable candidate after checked close, actual native-capacity allocation, no native I/O/mutation, post-export custody and wiping PASS");
}
static void malformed(){
 unsigned cases=0;
 for(unsigned mode=0;mode<22;++mode){Native n;Files f;seed(n,f);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Bytes b=selectedMap(f);unsigned count=100;auto id=c.identity();
  if(mode==0)b[0]=0; // empty key
  if(mode==1)b[0]='A';
  if(mode==2)memset(b.data(),'a',16); // no terminator
  if(mode==3)b[15]='x'; // nonzero trailing key padding
  if(mode==4)put32(b.data()+16,0);
  if(mode==5)put32(b.data()+16,2049);
  if(mode==6)put32(b.data()+16,UINT32_MAX);
  if(mode==7)memcpy(b.data()+52,b.data(),16); // duplicate canonical key
  if(mode==8)b.pop_back();
  if(mode==9)b.push_back(0);
  if(mode==10)--count;
  if(mode==11)++count;
  if(mode==12)count=UINT32_MAX;
  if(mode==13){b.clear();count=1;}
  if(mode==14){count=0;}
  id=identity(b,count);f.clear();save(f,b,count,id);
  if(mode==15)f[Index][80]^=1; // bad index CRC
  if(mode==16){id.lineage[0]=0;T::makeIndex(f[Index].data(),6,b.size(),count,id);}
  if(mode==17)T::makeIndex(f[Index].data(),1,b.size(),count,id);
  if(mode==18){id.digest[0]^=1;T::makeIndex(f[Index].data(),6,b.size(),count,id);for(unsigned p=0;p<2;++p){auto i=f.find(pagePath(identity(b,count),p));if(i!=f.end())f[pagePath(id,p)]=i->second;}}
  if(mode==19)f.at(pagePath(id,0))[30]^=1;
  if(mode==20)f.at(pagePath(id,0)).push_back(9);
  if(mode==21)f[Index].push_back(1);
  Sd s(f);Hash h;WipingAlloc a;R r(c,h.api(),s.guard(),a.api());const unsigned calls=n.calls;assert(r.start(s.volume())==0);s.run(r);assert(r.state()==R::State::Failed);assert(r.reason()==R::Reason::InvalidContent);R::Candidate out;assert(r.take(&out)==C::Stale&&!out.valid()&&r.exitSafe()&&!a.live);assert(n.calls==calls&&get(c,"edited")=="local"&&c.pendingCount()==1&&s.mutations==0);++cases;
 }
 printf("SD reader: %u valid-digest malformed map/index cases reject grammar, padding, duplicate keys, zero/oversize/truncated values, count, framing and SHA mismatches PASS\n",cases);
}
static void boundsAndAvailability(){
 for(unsigned mode=0;mode<8;++mode){Native n;Files f;seed(n,f);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;WipingAlloc a;
  if(mode==0){f.clear();}if(mode==1){s.available=false;}
  if(mode==2){s.fault=2;} // first index open-read returns error; metadata finds it
  if(mode==3){s.lease=false;}if(mode==4){a.fail=true;}
  C limited(6,n.api(),{512,4096,1024},C::heap()); // capacity binding does not load or adopt
  R r(mode==5?limited:c,h.api(),s.guard(),a.api());
  if(mode==6){auto id=c.identity();id.generation=UINT64_MAX;id.lineage[0]=88;Bytes b=selectedMap(f);f.clear();save(f,b,100,id);}
  if(mode==7)s.terminalBusy=true;
  int rc=r.start(s.volume());if(mode==3){assert(rc==C::Conflict&&r.reason()==R::Reason::CustodyLost);continue;}if(mode==4){assert(rc==C::NoMemory&&s.io==0);continue;}assert(rc==0);s.run(r);
  if(mode==6){R::Candidate out;assert(r.state()==R::State::Verified&&r.take(&out)==0&&out.identity().generation==UINT64_MAX&&out.identity().lineage[0]==88);}
  else {assert(r.state()==R::State::Failed);if(mode==0)assert(r.reason()==R::Reason::CheckpointMissing);if(mode==1)assert(r.reason()==R::Reason::MediaUnavailable);if(mode==2||mode==7)assert(r.reason()==R::Reason::ReadFailure);if(mode==5)assert(r.reason()==R::Reason::Capacity&&a.peak==512-C::Header-4);}
  assert(get(c,"edited")=="local"&&c.pendingCount()==1);
 }
 // An empty complete map is valid, including the minimum native image budget.
 Native n;Files f;auto id=identity({},0);save(f,{},0,id);C c(6,n.api(),{C::Header+4,4096,1024},C::heap());Sd s(f);Hash h;WipingAlloc a;R r(c,h.api(),s.guard(),a.api());assert(r.start(s.volume())==0);s.run(r);R::Candidate out;assert(r.take(&out)==0&&out.valid()&&out.mapSize()==0&&out.count()==0&&a.allocations==0);
 puts("SD reader: missing/unavailable/unreadable distinguishable, high/foreign generation gives no adoption authority, capacity/allocator failure and empty map PASS");
}
static void boundaries(){
 Native initial;Files original;seed(initial,original);unsigned steps=0;
 {C c(6,initial.api(),limits(),C::heap());assert(c.load()==0);Sd s(original);Hash h;R r(c,h.api(),s.guard());assert(r.start(s.volume())==0);steps=s.run(r);assert(r.state()==R::State::Verified);r.cancel();}
 unsigned cases=0;
 for(bool queued:{false,true})for(unsigned mode=0;mode<4;++mode)for(unsigned cut=0;cut<=steps;++cut){Native n=initial;Files f=original;
  {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;WipingAlloc a;R r(c,h.api(),s.guard(),a.api());assert(r.start(s.volume())==0);for(unsigned i=0;i<cut&&r.state()==R::State::Running;++i)s.tick(r);if(queued&&r.state()==R::State::Running)s.tick(r,false);
   if(mode==0){r.cancel();s.run(r);assert(r.state()==R::State::Cancelled&&r.exitSafe());}
   if(mode==1||mode==2){if(mode==1)s.available=false;else s.lease=false;if(r.state()==R::State::Verified){R::Candidate out;assert(r.take(&out)!=0&&!out.valid());}else s.run(r);assert(r.state()==R::State::Failed&&r.exitSafe());}
   assert(c.pendingCount()==1&&get(c,"edited")=="local");
  }unchanged(n);retryRead(n,f);++cases;
 }
 printf("SD reader: %u real queued/completed worker cancel/removal/export/restart boundaries preserve native intents and candidate custody PASS\n",cases);
}
static void faults(){
 Native initial;Files original;seed(initial,original);unsigned cases=0;
 for(bool backup:{false,true}){Files source=original;if(backup){source[std::string(Index)+".risc-replace"]=source[Index];source.erase(Index);}unsigned io=0;
  {C c(6,initial.api(),limits(),C::heap());assert(c.load()==0);Files f=source;Sd s(f);Hash h;R r(c,h.api(),s.guard());assert(r.start(s.volume())==0);s.run(r);assert(r.state()==R::State::Verified);io=s.io;r.cancel();}
  for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=io;++cut){Native n=initial;Files f=source;
   {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);s.fault=cut;s.crash=crash;s.after=after;Hash h;WipingAlloc a;R r(c,h.api(),s.guard(),a.api());assert(r.start(s.volume())==0);try{s.run(r);}catch(const Sd::Cut&){}assert(s.hit);assert(c.pendingCount()==1&&get(c,"edited")=="local");
    if(r.state()==R::State::Retained){auto before=s.io;r.cancel();for(unsigned i=0;i<5;++i)s.tick(r,false);R::Candidate out;assert(r.state()==R::State::Retained&&!r.exitSafe()&&r.take(&out)==C::Stale&&!out.valid()&&s.io==before);}
   }unchanged(n);retryRead(n,f);++cases;
  }
 }
 printf("SD reader: %u reached before/after read/recovery/close failures and power cuts, backup-only replacement recovery and sticky retention PASS\n",cases);
}
static void finalChecks(){
 for(unsigned mode=0;mode<4;++mode){Native n;Files f;seed(n,f);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;WipingAlloc a;R r(c,h.api(),s.guard(),a.api());assert(r.start(s.volume())==0);
  if(mode==0){while(s.operations.size()<20){s.tick(r);assert(r.state()==R::State::Running);}f[Index][32]^=1;s.run(r);assert(r.state()==R::State::Failed);}
  else {s.run(r);assert(r.state()==R::State::Verified);if(mode==1)s.available=false;if(mode==2)s.lease=false;if(mode==3)s.retained=true;R::Candidate out;assert(r.take(&out)!=0&&!out.valid());if(mode==3){r.cancel();assert(r.state()==R::State::Retained&&!r.exitSafe());}else assert(r.state()==R::State::Failed&&r.exitSafe());}
  assert(c.pendingCount()==1&&get(c,"edited")=="local");
 }
 puts("SD reader: changed-index recheck, late removal/export and late retained-close custody block candidate exposure PASS");
}
int main(){setbuf(stdout,nullptr);basics();malformed();boundsAndAvailability();boundaries();faults();finalChecks();puts("DISCONNECTED VERIFIED READER ONLY: no adoption, defaults, native mutation, migration, public ABI or hardware claim");}
