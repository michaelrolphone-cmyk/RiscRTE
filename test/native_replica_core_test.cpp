#include "experimental/native_replica/NativeReplicaCore.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using C=RiscExperimental::NativeReplicaCore;
using Bytes=std::vector<uint8_t>;
struct Disk {
 struct PowerCut {};
 enum Mode{Before,After,Torn};
 Bytes root;std::map<uint64_t,Bytes> objects;uint64_t next=1;
 unsigned calls=0,cut=0;Mode mode=Before;bool crash=false;size_t capacity=16*1024*1024;
 bool begin(){++calls;if(cut==calls&&mode==Before&&crash)throw PowerCut{};return cut==calls&&mode==Before;}
 bool end()const{if(cut==calls&&mode!=Before&&crash)throw PowerCut{};return cut==calls&&mode!=Before;}
 void fault(unsigned n,Mode m,bool power=false){calls=0;cut=n;mode=m;crash=power;}
 C::Backend backend(){return {this,
  [](void*v,void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);if(d.root.empty())return int(C::Missing);if(d.root.size()!=n)return int(C::Io);memcpy(p,d.root.data(),n);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint32_t n,uint64_t*id){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);size_t used=0;for(auto&e:d.objects)used+=e.second.size();if(used>d.capacity||n>d.capacity-used)return int(C::NoSpace);*id=d.next++;d.objects[*id]=Bytes(n,0xa5);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id,uint32_t at,const void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);auto it=d.objects.find(id);if(it==d.objects.end()||at>it->second.size()||n>it->second.size()-at)return int(C::Io);const uint32_t written=d.cut==d.calls&&d.mode==Torn?n/2:n;memcpy(it->second.data()+at,p,written);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id,uint32_t at,void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);auto it=d.objects.find(id);if(it==d.objects.end()||at>it->second.size()||n>it->second.size()-at)return int(C::Io);memcpy(p,it->second.data()+at,n);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);if(!d.objects.count(id))return int(C::Io);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,const void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io); // Root is atomic even when immutable payload writes can tear.
   if(!(d.cut==d.calls&&d.mode==Torn))d.root.assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);
   return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id){auto&d=*static_cast<Disk*>(v);if(d.begin())return;if(d.root.size()==C::RootBytes){uint64_t active=0;for(unsigned i=0;i<8;++i)active|=uint64_t(d.root[24+i])<<(8*i);assert(active!=id);}d.objects.erase(id);}
 };}
};
struct Alloc {
 unsigned calls=0,fail=0,watchWipe=0,wiped=0;void*watched=nullptr;std::map<void*,size_t> live;size_t bytes=0,peak=0;
 C::Allocator api(){return {this,[](void*v,size_t n)->void*{auto&a=*static_cast<Alloc*>(v);if(++a.calls==a.fail)return nullptr;void*p=malloc(n);if(p){if(a.calls==a.watchWipe)a.watched=p;a.live[p]=n;a.bytes+=n;if(a.bytes>a.peak)a.peak=a.bytes;}return p;},[](void*v,void*p){auto&a=*static_cast<Alloc*>(v);auto i=a.live.find(p);assert(i!=a.live.end());if(p==a.watched){for(size_t k=0;k<i->second;++k)assert(!static_cast<uint8_t*>(p)[k]);++a.wiped;a.watched=nullptr;}a.bytes-=i->second;a.live.erase(i);free(p);}};}
 void discardVolatileAfterPowerCut(){for(auto&v:live)free(v.first);live.clear();bytes=0;}
 ~Alloc(){assert(live.empty()&&bytes==0);}
};
C::Limits limits(){return {4*1024*1024,4096,1024};}
C::Identity base(uint64_t gen,uint8_t lineage=1){C::Identity i{};i.known=true;i.generation=gen;i.lineage[0]=lineage;i.digest[0]=uint8_t(gen);return i;}
std::string get(C&c,const char*k){char v[2048];uint32_t n=0;assert(c.get(k,v,sizeof v,&n)==C::Ok);return std::string(v,n);}
void missing(C&c,const char*k){char v[4];uint32_t n=99;assert(c.get(k,v,sizeof v,&n)==C::Missing&&n==0);}
int put(C&c,const char*k,const char*v,const char*intent="explicit-save"){C::Mutation m{k,C::Put,v,uint32_t(strlen(v))};return c.apply(c.revision(),&m,1,intent,uint32_t(strlen(intent)));}
std::vector<uint64_t> pending(C&c){std::vector<uint64_t> out;uint64_t after=0,id=0;char intent[1024];uint32_t n;for(;;){int rc=c.pending(after,&id,intent,sizeof intent,&n);if(rc==C::Missing)return out;assert(rc==C::Ok&&n);out.push_back(id);after=id;}}
Disk initial(unsigned count=3){Disk d;Alloc a;{C c(6,d.backend(),limits(),a.api());std::vector<std::string> keys,values;std::vector<C::Value> rows;keys.reserve(count);values.reserve(count);rows.reserve(count);
 for(unsigned i=0;i<count;++i){char k[16];snprintf(k,sizeof k,"key%04u",i);keys.emplace_back(k);values.emplace_back(i%7==0?2048:32,char('a'+i%26));rows.push_back({keys.back().c_str(),values.back().data(),uint32_t(values.back().size())});}
 assert(c.initialize(rows.data(),rows.size(),base(0))==C::Ok);assert(c.pendingCount()==0);assert(get(c,"key0000").size()==2048);}
 d.calls=0;return d;}
void basics(){
 Disk d=initial(1025);Alloc a;
 {C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);assert(get(c,"key1024").size()==32);assert(put(c,"custom","first")==C::Ok);const uint64_t r=c.revision();
  // A no-op value or deleting an absent key still records explicit intent.
  assert(put(c,"custom","first")==C::Ok&&c.revision()==r+1);
  C::Mutation group[]={{"custom",C::Put,"second",6},{"key0000",C::Delete,nullptr,0},{"not_present",C::Delete,nullptr,0}};
  assert(c.apply(c.revision(),group,3,"explicit-group",14)==C::Ok);missing(c,"key0000");assert(get(c,"custom")=="second");assert(pending(c)==(std::vector<uint64_t>{1,2,3}));
  C::Ticket t;assert(c.capture(&t)==C::Ok);assert(put(c,"custom","third")==C::Ok);
  assert(c.acknowledge(t,base(900))==C::Ok);assert(pending(c)==(std::vector<uint64_t>{4}));assert(get(c,"custom")=="third");assert(c.acknowledge(t,base(900))==C::Stale);
 }
 {C reboot(6,d.backend(),limits(),a.api());assert(reboot.load()==C::Ok);assert(get(reboot,"custom")=="third");missing(reboot,"key0000");assert(reboot.identity().generation==900);assert(pending(reboot)==(std::vector<uint64_t>{4}));
  C::Ticket t;assert(reboot.capture(&t)==C::Ok);assert(reboot.acknowledge(t,base(901))==C::Ok);assert(!reboot.pendingCount());}
 {C reboot(6,d.backend(),limits(),a.api());assert(reboot.load()==C::Ok);assert(get(reboot,"custom")=="third");assert(get(reboot,"key1024").size()==32);missing(reboot,"key0000");assert(!reboot.pendingCount());}
 puts("native replica: 1025 opaque keys, atomic PUT/DELETE groups, same-value intent, permanent replica after acknowledgement, newer pending operation and reboot PASS");
}
void reconciliation(){
 Disk d=initial();Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);assert(put(c,"local","changed")==C::Ok);
 C::Value remote[]={{"sd_only","restored",8},{"local","changed",7}};const uint64_t one[]={1};auto rev=c.revision();d.calls=0;
 assert(c.installResolved(rev,remote,2,base(1000),nullptr,0)==C::Conflict&&d.calls==0);assert(get(c,"local")=="changed"&&c.identity().known&&c.identity().generation==0);
 assert(put(c,"newer","preserved")==C::Ok);d.calls=0;assert(c.installResolved(rev,remote,2,base(1000),one,1)==C::Stale&&d.calls==0);assert(get(c,"newer")=="preserved");
 C::Value resolved[]={{"sd_only","restored",8},{"local","changed",7},{"newer","preserved",9}};const uint64_t ids[]={1,2};
 assert(c.installResolved(c.revision(),resolved,3,base(1000),ids,2)==C::Ok);assert(get(c,"sd_only")=="restored"&&get(c,"newer")=="preserved");assert(pending(c)==(std::vector<uint64_t>{1,2}));
 assert(c.installResolved(c.revision(),resolved,3,base(2000,2),ids,2)==C::Conflict);
 assert(c.installResolved(c.revision(),resolved,3,base(999),ids,2)==C::Conflict);
 auto fork=base(1000);fork.digest[1]=1;assert(c.installResolved(c.revision(),resolved,3,fork,ids,2)==C::Conflict);
 C reboot(6,d.backend(),limits(),a.api());assert(reboot.load()==C::Ok);assert(pending(reboot)==(std::vector<uint64_t>{1,2}));assert(get(reboot,"local")=="changed");
 C::Ticket t;assert(reboot.capture(&t)==C::Ok);assert(reboot.acknowledge(t,base(1001))==C::Ok);assert(get(reboot,"sd_only")=="restored"&&get(reboot,"local")=="changed");
 puts("native replica: selected known-base edits require explicit resolution; high SD generation does not discard intent; stale candidate rejects concurrent changes; foreign lineage rejected PASS");
}
void failureMatrix(){
 const Disk source=initial(33);unsigned totalCuts=0;
 // Fault every backend call, before and after, and tear candidate chunk writes.
 for(unsigned operation=0;operation<3;++operation){
  Disk prepared=source;{Alloc a;C c(6,prepared.backend(),limits(),a.api());assert(c.load()==C::Ok);assert(put(c,"before","kept")==C::Ok);}
  auto run=[&](C&c)->int{
   if(operation==0){C::Mutation group[]={{"before",C::Put,"new",3},{"atomic_peer",C::Put,"new",3},{"key0000",C::Delete,nullptr,0}};return c.apply(c.revision(),group,3,"intent-A",8);}
   if(operation==1){C::Ticket t;assert(c.capture(&t)==C::Ok);return c.acknowledge(t,base(100));}
   const uint64_t ids[]={1};C::Value rows[]={{"before","kept",4},{"remote_only","new",3}};return c.installResolved(c.revision(),rows,2,base(500),ids,1);
  };
  unsigned calls=0;{Disk baseline=prepared;Alloc a;C c(6,baseline.backend(),limits(),a.api());assert(c.load()==C::Ok);baseline.fault(0,Disk::Before);assert(run(c)==C::Ok);calls=baseline.calls;}
  for(unsigned power=0;power<2;++power)for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=calls;++cut){Disk d=prepared;Alloc a;
   try {C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);d.fault(cut,Disk::Mode(mode),power!=0);const int rc=run(c);assert(rc==C::Ok||rc==C::Io||rc==C::Uncertain);if(rc==C::Uncertain){uint32_t n;char b[8];assert(c.get("before",b,sizeof b,&n)==C::Uncertain);} } catch(const Disk::PowerCut&) {a.discardVolatileAfterPowerCut();}
   d.fault(0,Disk::Before);{C restored(6,d.backend(),limits(),a.api());assert(restored.load()==C::Ok);
    if(operation==0){const auto val=get(restored,"before");assert(val=="kept"||val=="new");if(val=="new"){assert(get(restored,"atomic_peer")=="new");missing(restored,"key0000");assert(pending(restored)==(std::vector<uint64_t>{1,2}));}else{missing(restored,"atomic_peer");assert(get(restored,"key0000").size()==2048);assert(pending(restored)==(std::vector<uint64_t>{1}));}}
    else if(operation==1){assert(get(restored,"before")=="kept"&&get(restored,"key0000").size()==2048);assert(restored.pendingCount()==0||restored.pendingCount()==1);assert(restored.identity().generation==(restored.pendingCount()==0?100:0));}
    else {assert(get(restored,"before")=="kept"&&pending(restored)==(std::vector<uint64_t>{1}));if(restored.identity().generation==500){assert(get(restored,"remote_only")=="new");missing(restored,"key0000");}else{missing(restored,"remote_only");assert(get(restored,"key0000").size()==2048);}}
   }++totalCuts;
  }
 }
 printf("native replica: %u deterministic error/power-cut before/after/torn backend interruption points across apply, acknowledgement and candidate switch PASS\n",totalCuts);
}
void failuresAndBounds(){
 Disk d=initial();Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);assert(put(c,"stable","original")==C::Ok);const Bytes root=d.root;const auto rev=c.revision();
 a.fail=a.calls+1;d.calls=0;assert(put(c,"stable","changed")==C::NoMemory&&d.calls==0);assert(d.root==root&&get(c,"stable")=="original"&&c.revision()==rev);a.fail=0;
 C::Ticket t;assert(c.capture(&t)==C::Ok);a.fail=a.calls+1;assert(c.acknowledge(t,base(1))==C::NoMemory&&d.root==root&&c.pendingCount()==1);a.fail=0;
 const uint64_t ids[]={1};C::Value row{"stable","candidate",9};a.fail=a.calls+1;assert(c.installResolved(c.revision(),&row,1,base(3),ids,1)==C::NoMemory&&d.root==root);a.fail=0;
 d.capacity=0;assert(put(c,"stable","changed")==C::NoSpace&&d.root==root&&get(c,"stable")=="original");d.capacity=16*1024*1024;
 C::Mutation duplicates[]={{"same",C::Put,"a",1},{"same",C::Delete,nullptr,0}};d.calls=0;assert(c.apply(c.revision(),duplicates,2,"intent",6)==C::Invalid&&d.calls==0);
 std::string hugeIntent(1025,'x');C::Mutation m{"valid",C::Put,"v",1};assert(c.apply(c.revision(),&m,1,hugeIntent.data(),hugeIntent.size())==C::Invalid);
 C::Mutation tooBig[]={{"a",C::Put,hugeIntent.data(),1025},{"b",C::Put,hugeIntent.data(),1025},{"c",C::Put,hugeIntent.data(),1025},{"d",C::Put,hugeIntent.data(),1025}};assert(c.apply(c.revision(),tooBig,4,"intent",6)==C::NoSpace&&d.root==root);
 {C wrong(1,d.backend(),limits(),a.api());assert(wrong.load()==C::Io);}
 {Disk bad=d;bad.root[0]^=1;C broken(6,bad.backend(),limits(),a.api());assert(broken.load()==C::Io);assert(broken.initialize(nullptr,0,{})==C::Conflict);}
 {Disk bad=d;uint64_t id=0;for(unsigned i=0;i<8;++i)id|=uint64_t(bad.root[24+i])<<(8*i);bad.objects.at(id).back()^=1;C broken(6,bad.backend(),limits(),a.api());assert(broken.load()==C::Io);}
 {C reboot(6,d.backend(),limits(),a.api());assert(reboot.load()==C::Ok);assert(reboot.acknowledge(t,base(5))==C::Stale);assert(get(reboot,"stable")=="original");}
 {Disk blank;C fresh(6,blank.backend(),limits(),a.api());assert(fresh.load()==C::Missing);char out[4];uint32_t n;assert(fresh.get("default",out,sizeof out,&n)==C::Uncertain);assert(fresh.initialize(nullptr,0,{})==C::Ok);missing(fresh,"default");}
 puts("native replica: allocation failures, storage exhaustion, bounded group/intent, corruption, wrong namespace, expired acknowledgement and explicit initialization PASS");
}

void publicationScratchFailures(){
 Disk d=initial();Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);
 assert(put(c,"stable","original")==C::Ok);const auto root=d.root;const auto objects=d.objects;const auto next=d.next;const auto bytes=a.bytes;
 auto unchanged=[&](){assert(d.calls==0&&d.root==root&&d.objects==objects&&d.next==next&&a.bytes==bytes&&get(c,"stable")=="original");};
 // The image allocation succeeds; only the second, verification allocation fails.
 a.fail=a.calls+2;d.calls=0;assert(put(c,"stable","changed")==C::NoMemory);unchanged();a.fail=0;
 C::Ticket ticket;assert(c.capture(&ticket)==C::Ok);a.fail=a.calls+2;d.calls=0;
 assert(c.acknowledge(ticket,base(7))==C::NoMemory);unchanged();a.fail=0;
 const uint64_t ids[]={1};C::Value row{"stable","candidate",9};a.fail=a.calls+2;d.calls=0;
 assert(c.installResolved(c.revision(),&row,1,base(8),ids,1)==C::NoMemory);unchanged();a.fail=0;
 a.watchWipe=a.calls+2;assert(put(c,"stable","changed")==C::Ok&&a.wiped==1&&!a.watched);
 {Disk blank;Alloc b;C fresh(6,blank.backend(),limits(),b.api());b.fail=2;
  assert(fresh.initialize(nullptr,0,{})==C::NoMemory&&blank.root.empty()&&blank.objects.empty()&&blank.next==1&&!b.bytes);}
 puts("Publication verification scratch: second-allocation failure preserves all native records before reserve; apply/acknowledge/install/initialize fail closed and successful release wipes every verification byte PASS");
}

void creationAndConcurrentAckCuts(){
 std::string value(2048,'x');C::Value rows[]={{"first",value.data(),uint32_t(value.size())},{"second",value.data(),uint32_t(value.size())},{"third",value.data(),uint32_t(value.size())}};
 unsigned createCalls=0;
 {Disk d;Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.initialize(rows,3,{})==C::Ok);createCalls=d.calls;}
 unsigned cuts=0;
 for(unsigned power=0;power<2;++power)for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=createCalls;++cut){Disk d;Alloc a;
  try {C c(6,d.backend(),limits(),a.api());d.fault(cut,Disk::Mode(mode),power!=0);int rc=c.initialize(rows,3,{});assert(rc==C::Ok||rc==C::Io||rc==C::Uncertain);}catch(const Disk::PowerCut&){a.discardVolatileAfterPowerCut();}
  d.fault(0,Disk::Before);C reboot(6,d.backend(),limits(),a.api());int rc=reboot.load();assert(rc==C::Ok||rc==C::Missing);if(rc==C::Ok){assert(get(reboot,"first")==value&&get(reboot,"second")==value&&get(reboot,"third")==value);}++cuts;
 }
 Disk source=initial();{Alloc a;C c(6,source.backend(),limits(),a.api());assert(c.load()==C::Ok);assert(put(c,"edit","first")==C::Ok);}
 unsigned ackCalls=0;
 {Disk d=source;Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);C::Ticket t;assert(c.capture(&t)==C::Ok);assert(put(c,"edit","newer")==C::Ok);d.fault(0,Disk::Before);assert(c.acknowledge(t,base(100))==C::Ok);ackCalls=d.calls;}
 for(unsigned power=0;power<2;++power)for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=ackCalls;++cut){Disk d=source;Alloc a;
  try {C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);C::Ticket t;assert(c.capture(&t)==C::Ok);assert(put(c,"edit","newer")==C::Ok);d.fault(cut,Disk::Mode(mode),power!=0);int rc=c.acknowledge(t,base(100));assert(rc==C::Ok||rc==C::Io||rc==C::Uncertain);}catch(const Disk::PowerCut&){a.discardVolatileAfterPowerCut();}
  d.fault(0,Disk::Before);C reboot(6,d.backend(),limits(),a.api());assert(reboot.load()==C::Ok&&get(reboot,"edit")=="newer");const auto ids=pending(reboot);assert(ids==(std::vector<uint64_t>{2})||ids==(std::vector<uint64_t>{1,2}));assert(get(reboot,"key0000").size()==2048);++cuts;
 }
 printf("native replica: %u creation and concurrent-newer-ack error/power-cut interruption points PASS\n",cuts);
}

int main(){basics();reconciliation();failureMatrix();publicationScratchFailures();creationAndConcurrentAckCuts();failuresAndBounds();puts("UNCONNECTED HOST CORE ONLY: no .90 routing/ABI change, SD/NVS integration or Wi-Fi semantic reconciliation qualified");}
