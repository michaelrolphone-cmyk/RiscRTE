#include "experimental/native_replica/NativeReplicaCore.h"
#undef NDEBUG
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
 unsigned calls=0,cut=0;Mode mode=Before;bool crash=false,twoBanks=true;unsigned retainedRootMode=0;size_t capacity=16*1024*1024;
 bool begin(){++calls;if(cut==calls&&mode==Before&&crash)throw PowerCut{};return cut==calls&&mode==Before;}
 bool end()const{if(cut==calls&&mode!=Before&&crash)throw PowerCut{};return cut==calls&&mode!=Before;}
 void fault(unsigned n,Mode m,bool power=false){calls=0;cut=n;mode=m;crash=power;}
 C::Backend backend(){return {this,
  [](void*v,void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);if(d.root.empty())return int(C::Missing);if(d.root.size()!=n)return int(C::Io);memcpy(p,d.root.data(),n);if(d.retainedRootMode==3)return int(C::Retained);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint32_t n,uint64_t*id){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);size_t used=0;for(auto&e:d.objects)used+=e.second.size();if(used>d.capacity||n>d.capacity-used)return int(C::NoSpace);*id=d.next++;
   if(d.twoBanks){uint64_t active=0;if(d.root.size()==C::RootBytes)for(unsigned i=0;i<8;++i)active|=uint64_t(d.root[24+i])<<(8*i);for(auto it=d.objects.begin();it!=d.objects.end();)if(it->first!=active)it=d.objects.erase(it);else ++it;}
   d.objects[*id]=Bytes(n,0xa5);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id,uint32_t at,const void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);auto it=d.objects.find(id);if(it==d.objects.end()||at>it->second.size()||n>it->second.size()-at)return int(C::Io);const uint32_t written=d.cut==d.calls&&d.mode==Torn?n/2:n;memcpy(it->second.data()+at,p,written);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id,uint32_t at,void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);auto it=d.objects.find(id);if(it==d.objects.end()||at>it->second.size()||n>it->second.size()-at)return int(C::Io);memcpy(p,it->second.data()+at,n);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io);if(!d.objects.count(id))return int(C::Io);return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,const void*p,uint32_t n){auto&d=*static_cast<Disk*>(v);if(d.begin())return int(C::Io); // Root is atomic even when immutable payload writes can tear.
   if(d.retainedRootMode==1)return int(C::Retained);
   if(!(d.cut==d.calls&&d.mode==Torn))d.root.assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);
   if(d.retainedRootMode==2)return int(C::Retained);
   return d.end()?int(C::Io):int(C::Ok);},
  [](void*v,uint64_t id){auto&d=*static_cast<Disk*>(v);if(d.begin())return;if(d.root.size()==C::RootBytes){uint64_t active=0;for(unsigned i=0;i<8;++i)active|=uint64_t(d.root[24+i])<<(8*i);assert(active!=id);}if(!d.twoBanks)d.objects.erase(id);}
 };}
};
struct Alloc {
 unsigned calls=0,fail=0;std::map<void*,size_t> live;size_t bytes=0,peak=0;
 C::Allocator api(){return {this,[](void*v,size_t n)->void*{auto&a=*static_cast<Alloc*>(v);if(++a.calls==a.fail)return nullptr;void*p=malloc(n);if(p){a.live[p]=n;a.bytes+=n;if(a.bytes>a.peak)a.peak=a.bytes;}return p;},[](void*v,void*p){auto&a=*static_cast<Alloc*>(v);auto i=a.live.find(p);assert(i!=a.live.end());a.bytes-=i->second;a.live.erase(i);free(p);}};}
 void discardVolatileAfterPowerCut(){for(auto&v:live)free(v.first);live.clear();bytes=0;}
 ~Alloc(){assert(live.empty()&&bytes==0);}
};
C::Limits limits(uint32_t bytes=65512){return {bytes,4096,1024};}
uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
void putWord(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
void putWide(uint8_t*p,uint64_t v){putWord(p,uint32_t(v));putWord(p+4,uint32_t(v>>32));}
uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
Bytes&selected(Disk&d){return d.objects.at(wide(d.root.data()+24));}
void repairChecksums(Disk&d,bool marker=true){auto&p=selected(d);if(marker)putWord(p.data()+C::Header+252,crc(p.data()+C::Header,252));putWord(p.data()+p.size()-4,crc(p.data(),p.size()-4));putWord(d.root.data()+32,word(p.data()+p.size()-4));putWord(d.root.data()+44,crc(d.root.data(),44));}
C::Identity identity(uint64_t generation,uint8_t lineage=3){C::Identity i{};i.known=true;i.generation=generation;i.lineage[0]=lineage;i.digest[0]=uint8_t(generation);return i;}
const uint8_t incarnation[16]={0x42,0x61,0x72,0x83,0x95,0x16,0xa7,0xb8,0xc9,0xda,0xeb,0xfc,0x0d,0x1e,0x2f,0x30}; // Synthetic native-owner input, not production entropy.
int put(C&c,const char*key,const char*value,const char*intent="synthetic-edit"){C::Mutation m{key,C::Put,value,uint32_t(strlen(value))};return c.apply(c.revision(),&m,1,intent,uint32_t(strlen(intent)));}
std::string get(C&c,const char*key){char buf[2048];uint32_t n=0;assert(c.get(key,buf,sizeof buf,&n)==C::Ok);return {buf,n};}
Bytes groups(Disk&d){const auto&p=selected(d);const unsigned h=p[6]=='2'?C::HeaderV2:C::Header;return {p.begin()+h+word(p.data()+104),p.end()-4};}
Bytes map(C&c){C::Snapshot s;assert(c.captureSnapshot(&s)==C::Ok);return {s.map(),s.map()+s.mapSize()};}
std::vector<uint64_t> ids(C&c){std::vector<uint64_t> result;uint64_t after=0,id=0;uint32_t n=0;char b[1024];for(;;){const int rc=c.pending(after,&id,b,sizeof b,&n);if(rc==C::Missing)return result;assert(rc==C::Ok);result.push_back(id);after=id;}}
C::ExportMarker arm(C&c){C::Snapshot s;C::ExportMarker m;assert(c.captureSnapshot(&s)==C::Ok);assert(c.prepareExport(s,identity(c.identity().generation+1),c.formatVersion()==1?incarnation:nullptr,&m)==C::Ok);return m;}
Disk source(bool large=false){Disk d;Alloc a;C c(6,d.backend(),limits(),a.api());std::string payload(2048,'x');C::Value rows[]={{"stable",payload.data(),2048},{"stable2",payload.data(),2048},{"stable3",payload.data(),2048}};assert(c.initialize(rows,large?3:1,identity(5))==C::Ok);assert(put(c,"edit","first","first-original")==C::Ok);assert(put(c,"edit","second","second-original")==C::Ok);return d;}
void compatibilityAndBounds(){
 static_assert(C::Header==128&&C::HeaderV2==384&&C::MarkerBytes==256&&C::RootBytes==48,"native layout");
 static_assert(sizeof(C::ExportMarker)==256,"opaque encoded handle only");
 {Disk legacy;Alloc heap;C unclassified(6,legacy.backend(),limits(),heap.api());assert(unclassified.initialize(nullptr,0,{})==C::Ok);assert(put(unclassified,"offline","saved")==C::Ok);C::Ticket t;assert(unclassified.capture(&t)==C::Ok);assert(unclassified.acknowledge(t,identity(5))==C::Conflict&&get(unclassified,"offline")=="saved");}
 Disk d;Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.initialize(nullptr,0,identity(4))==C::Ok);assert(c.formatVersion()==1&&c.headerSize()==128&&!c.exportPrepared());
 C::ExportMarker m;assert(!m.valid()&&c.preparedExport(&m)==C::Missing);assert(put(c,"offline","saved")==C::Ok&&c.formatVersion()==1);
 for(unsigned i=112;i<128;++i)assert(selected(d)[i]==0);
 C::Snapshot unknown;assert(c.captureSnapshot(&unknown)==C::Ok);assert(c.prepareExport(unknown,identity(1),incarnation,&m)==C::Conflict);
 C::Ticket ticket;assert(c.capture(&ticket)==C::Ok);assert(c.acknowledge(ticket,identity(5))==C::Ok&&c.formatVersion()==1);assert(get(c,"offline")=="saved");
 C::Snapshot s;assert(c.captureSnapshot(&s)==C::Ok&&s.headerSize()==128&&s.formatVersion()==1);const auto old=d.root;const auto rev=c.revision();uint8_t zero[16]{};
 assert(c.prepareExport(s,identity(6),nullptr,&m)==C::Invalid);assert(c.prepareExport(s,identity(6),zero,&m)==C::Invalid);
 assert(c.prepareExport(s,identity(7),incarnation,&m)==C::Conflict);assert(c.prepareExport(s,identity(6,9),incarnation,&m)==C::Conflict);
 assert(c.prepareExport(s,{},incarnation,&m)==C::Invalid&&d.root==old&&c.revision()==rev);
 assert(c.prepareExport(s,identity(6),incarnation,&m)==C::Ok&&m.valid()&&m.prefixCount()==0&&m.prefixBytes()==0&&m.through()==m.previousAck());
 assert(c.headerSize()==384&&c.formatVersion()==2&&c.revision()==rev+1&&c.sequence()==s.ticket().through);
 assert(selected(d).size()==s.imageSize()+256&&!memcmp(m.incarnation(),incarnation,16));
 assert(c.acknowledgeExport(m,identity(6))==C::Ok&&c.headerSize()==384);assert(c.acknowledgeExport(m,identity(6))==C::Stale);
 C::Snapshot v2;assert(c.captureSnapshot(&v2)==C::Ok&&v2.headerSize()==384&&v2.formatVersion()==2);uint8_t different[16]={99};
 assert(c.prepareExport(v2,identity(7),different,&m)==C::Conflict);assert(c.prepareExport(v2,identity(7),incarnation,&m)==C::Ok);assert(c.retireExport(m,identity(6))==C::Ok);
 assert(put(c,"offline","updated")==C::Ok&&get(c,"offline")=="updated");assert(c.capture(&ticket)==C::Ok&&c.acknowledge(ticket,identity(7))==C::Ok);
 assert(c.installResolved(c.revision(),nullptr,0,identity(8),nullptr,0)==C::Ok&&c.headerSize()==384&&!c.pendingCount());
 {C reboot(6,d.backend(),limits(),a.api());assert(reboot.load()==C::Ok&&reboot.formatVersion()==2&&!reboot.exportPrepared());C::ExportMarker empty=arm(reboot);assert(empty.count()==0&&empty.mapSize()==0&&empty.prefixCount()==0&&!memcmp(empty.incarnation(),incarnation,16));assert(reboot.retireExport(empty,identity(8))==C::Ok);}
 {Disk bad=d;memset(selected(bad).data()+112,0,16);repairChecksums(bad,false);C broken(6,bad.backend(),limits(),a.api());assert(broken.load()==C::Io);}
 // Explicit upgrade overhead is admitted against the same total image budget.
 Disk seed=source();const uint32_t oldSize=uint32_t(selected(seed).size());
 for(unsigned extra: {0u,255u,256u}){Disk copy=seed;C bounded(6,copy.backend(),limits(oldSize+extra),a.api());assert(bounded.load()==C::Ok);C::Snapshot cap;assert(bounded.captureSnapshot(&cap)==C::Ok);const Bytes root=copy.root;
  const int rc=bounded.prepareExport(cap,identity(6),incarnation,&m);assert(rc==(extra==256?C::Ok:C::NoSpace));if(extra<256)assert(copy.root==root&&bounded.formatVersion()==1&&!bounded.exportPrepared());}
 // A too-small v2 record and reserved v1 bytes are refused after outer CRC repair.
 {Disk bad=seed;selected(bad)[112]=1;repairChecksums(bad,false);C broken(6,bad.backend(),limits(),a.api());assert(broken.load()==C::Io);}
 {Disk bad;C fresh(6,bad.backend(),limits(),a.api());assert(fresh.initialize(nullptr,0,{})==C::Ok);selected(bad)[6]='2';repairChecksums(bad,false);C broken(6,bad.backend(),limits(),a.api());assert(broken.load()==C::Io);}
 puts("durable marker: entropy-free v1 compatibility, explicit v2 upgrade, empty prefix, exact target and 256-byte capacity overhead PASS");
}
void captureAndNewerEdits(){
 Disk d=source();Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);C::Snapshot old,s;C::ExportMarker m;
 assert(c.captureSnapshot(&old)==C::Ok&&c.captureSnapshot(&s)==C::Ok);const auto root=d.root;d.calls=0;
 assert(c.prepareExport(old,identity(6),incarnation,&m)==C::Stale&&d.calls==0);
 {C other(6,d.backend(),limits(),a.api());assert(other.load()==C::Ok);C::Snapshot own;assert(other.captureSnapshot(&own)==C::Ok);assert(c.prepareExport(own,identity(6),incarnation,&m)==C::Stale);}
 assert(put(c,"edit","third")==C::Ok);d.calls=0;assert(c.prepareExport(s,identity(6),incarnation,&m)==C::Stale&&d.calls==0);s.reset();assert(c.captureSnapshot(&s)==C::Ok);
 const auto beforeGroups=groups(d),beforeMap=Bytes(s.map(),s.map()+s.mapSize());const auto revision=c.revision();
 // Equal ticket/revision/map alone cannot stand in for exact group bytes.
 auto*capturedGroups=const_cast<uint8_t*>(s.map())+s.mapSize();capturedGroups[C::GroupHeader]^=1;d.calls=0;
 assert(c.prepareExport(s,identity(6),incarnation,&m)==C::Stale&&d.calls==0);capturedGroups[C::GroupHeader]^=1;
 assert(c.prepareExport(s,identity(6),incarnation,&m)==C::Ok);assert(m.namespaceId()==6&&m.capturedRevision()==revision&&m.through()==3&&m.previousAck()==0&&m.prefixCount()==3);
 assert(m.prefixBytes()==beforeGroups.size()&&m.mapSize()==beforeMap.size()&&m.count()==s.count());assert(groups(d)==beforeGroups&&map(c)==beforeMap);
 C::ExportMarker selectedMarker;assert(c.preparedExport(&selectedMarker)==C::Ok&&!memcmp(m.encoded(),selectedMarker.encoded(),256));
 d.calls=0;assert(c.acknowledge(s.ticket(),identity(6))==C::Busy);assert(c.installResolved(c.revision(),nullptr,0,identity(5),nullptr,0)==C::Busy);assert(c.prepareExport(s,identity(6),nullptr,&selectedMarker)==C::Busy&&d.calls==0);
 // More than two publications force repeated inactive-bank reuse.
 for(unsigned i=0;i<7;++i){assert(put(c,"newer","preserved")==C::Ok);assert(d.objects.size()<=2);C::ExportMarker current;assert(c.preparedExport(&current)==C::Ok&&!memcmp(m.encoded(),current.encoded(),256));const Bytes all=groups(d);assert(std::equal(beforeGroups.begin(),beforeGroups.end(),all.begin()));}
 const auto todaysMap=map(c),todaysGroups=groups(d);const auto fullRoot=d.root;const uint64_t through=c.sequence();
 {C reboot(6,d.backend(),limits(),a.api());assert(reboot.load()==C::Ok&&reboot.exportPrepared());C::Snapshot readOnly;assert(reboot.captureSnapshot(&readOnly)==C::Ok);assert(reboot.acknowledgeExport(m,identity(7))==C::Conflict&&d.root==fullRoot);assert(reboot.retireExport(m,identity(4))==C::Conflict&&d.root==fullRoot);
  assert(reboot.acknowledgeExport(m,identity(6))==C::Ok);assert(map(reboot)==todaysMap&&reboot.sequence()==through&&reboot.pendingCount()==7&&!reboot.exportPrepared());assert(groups(d)==Bytes(todaysGroups.begin()+m.prefixBytes(),todaysGroups.end()));assert(reboot.acknowledgeExport(m,identity(6))==C::Stale);
  C::ExportMarker next=arm(reboot);assert(next.previousAck()==3&&next.prefixCount()==7&&next.through()==10);assert(!memcmp(next.incarnation(),incarnation,16));assert(reboot.acknowledgeExport(m,identity(6))==C::Stale);assert(reboot.retireExport(m,identity(5))==C::Stale);
  const auto nextGroups=groups(d),nextMap=map(reboot);assert(reboot.retireExport(next,identity(6))==C::Ok&&groups(d)==nextGroups&&map(reboot)==nextMap&&reboot.pendingCount()==7&&reboot.identity().generation==6);
  // Retirement/rearm with unchanged frontier must issue a different handle.
  C::ExportMarker fresh=arm(reboot);assert(memcmp(next.encoded(),fresh.encoded(),256));assert(reboot.retireExport(next,identity(6))==C::Stale);assert(reboot.retireExport(fresh,identity(6))==C::Ok);
 }
 assert(d.root!=root);puts("durable marker: exact live capture CAS, reboot, current map preservation, original-prefix carry-forward, two-bank reuse and stale handles PASS");
}
void corruptions(){
 Disk seed=source();Alloc a;C::ExportMarker marker;{C c(6,seed.backend(),limits(),a.api());assert(c.load()==C::Ok);marker=arm(c);}
 unsigned checked=0;
 // Mutate each structural marker field and repair the enclosing checksums.
 for(unsigned offset: {0u,8u,12u,16u,32u,40u,48u,56u,60u,64u,68u,72u,76u,80u,88u,104u,136u,140u,144u,152u,200u,204u,208u}){
  Disk bad=seed;selected(bad)[C::Header+offset]^=1;repairChecksums(bad);C c(6,bad.backend(),limits(),a.api());assert(c.load()==C::Io);++checked;
 }
 {Disk bad=seed;selected(bad)[C::Header+252]^=1;repairChecksums(bad,false);C c(6,bad.backend(),limits(),a.api());assert(c.load()==C::Io);++checked;}
 for(unsigned offset: {8u,40u,48u,64u,112u}){Disk bad=seed;selected(bad)[offset]^=1;repairChecksums(bad);C c(6,bad.backend(),limits(),a.api());assert(c.load()==C::Io);++checked;}
 {Disk bad=seed;auto&p=selected(bad);const auto at=C::HeaderV2+word(p.data()+104);p[at+C::GroupHeader+word(p.data()+at+28)]^=1;repairChecksums(bad);C c(6,bad.backend(),limits(),a.api());assert(c.load()==C::Io);++checked;}
 {Disk bad=seed;auto&p=selected(bad);const auto at=C::HeaderV2+word(p.data()+104);putWide(p.data()+at,0);repairChecksums(bad);C c(6,bad.backend(),limits(),a.api());assert(c.load()==C::Io);++checked;}
 // A checksum-valid but incomplete membership claim cannot authorize dropping.
 {Disk bad=seed;auto&p=selected(bad);auto*m=p.data()+C::Header;const auto at=C::HeaderV2+word(p.data()+104);const auto bytes=C::GroupHeader+word(p.data()+at+24)+word(p.data()+at+28);putWord(m+56,1);putWord(m+60,bytes);putWord(m+200,crc(p.data()+at,bytes));repairChecksums(bad);C c(6,bad.backend(),limits(),a.api());assert(c.load()==C::Io);++checked;}
 // Different fresh native incarnation prevents an otherwise identical handle.
 {Disk twin=source();C c(6,twin.backend(),limits(),a.api());assert(c.load()==C::Ok);C::Snapshot s;C::ExportMarker other;uint8_t id[16];memcpy(id,incarnation,16);id[0]^=1;assert(c.captureSnapshot(&s)==C::Ok);assert(c.prepareExport(s,identity(6),id,&other)==C::Ok);const auto root=twin.root;assert(c.acknowledgeExport(marker,identity(6))==C::Stale&&twin.root==root);}
 // Corrupt only a copied handle; native selection remains intact.
 {C c(6,seed.backend(),limits(),a.api());assert(c.load()==C::Ok);C::ExportMarker bad=marker;const_cast<uint8_t*>(bad.encoded())[32]^=1;const auto root=seed.root;assert(c.retireExport(bad,identity(5))==C::Stale&&seed.root==root);assert(c.retireExport(marker,identity(5))==C::Ok);assert(c.preparedExport(&bad)==C::Missing);
  bool inactiveHasMarker=false;const auto active=wide(seed.root.data()+24);for(const auto&entry:seed.objects)if(entry.first!=active&&entry.second.size()>C::HeaderV2&&!memcmp(entry.second.data()+C::Header,marker.encoded(),256))inactiveHasMarker=true;assert(inactiveHasMarker);
  C reboot(6,seed.backend(),limits(),a.api());assert(reboot.load()==C::Ok&&reboot.preparedExport(&bad)==C::Missing&&reboot.acknowledgeExport(marker,identity(6))==C::Stale);
 }
 printf("durable marker: %u repaired-outer-checksum format, CRC, namespace/incarnation, base, map and original-prefix corruption refusals PASS\n",checked);
}
void allocationFailures(){
 for(unsigned operation=0;operation<3;++operation){Disk d=source();Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);C::ExportMarker m;if(operation)m=arm(c);C::Snapshot s;if(!operation)assert(c.captureSnapshot(&s)==C::Ok);const auto root=d.root;const auto beforeGroups=groups(d);const auto rev=c.revision();
  d.calls=0;a.fail=a.calls+1;const int rc=operation==0?c.prepareExport(s,identity(6),incarnation,&m):(operation==1?c.acknowledgeExport(m,identity(6)):c.retireExport(m,identity(5)));
  assert(rc==C::NoMemory&&d.calls==0&&d.root==root&&groups(d)==beforeGroups&&c.revision()==rev);a.fail=0;
  d.capacity=0;const int storage=operation==0?c.prepareExport(s,identity(6),incarnation,&m):(operation==1?c.acknowledgeExport(m,identity(6)):c.retireExport(m,identity(5)));assert(storage==C::NoSpace&&d.root==root&&groups(d)==beforeGroups);d.capacity=16*1024*1024;
 }
 puts("durable marker: arm/ack/retire allocation and storage admission failures leave selected root unchanged PASS");
}
void failureMatrix(){
 unsigned reached=0,retained=0;
 for(unsigned operation=0;operation<3;++operation){Disk seed=source(true);C::ExportMarker original;
  if(operation){Alloc a;C c(6,seed.backend(),limits(),a.api());assert(c.load()==C::Ok);original=arm(c);assert(put(c,"newer","preserved","third-original")==C::Ok);}
  const auto oldRoot=seed.root,oldGroups=groups(seed);const auto oldRevision=wide(selected(seed).data()+16);Bytes oldMap;
  {Alloc a;C c(6,seed.backend(),limits(),a.api());assert(c.load()==C::Ok);oldMap=map(c);}
  auto run=[&](C&c,C::Snapshot&s,C::ExportMarker&m){if(operation==0)return c.prepareExport(s,identity(6),incarnation,&m);if(operation==1)return c.acknowledgeExport(m,identity(6));return c.retireExport(m,identity(5));};
  unsigned calls=0;{Disk d=seed;Alloc a;C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);C::Snapshot s;C::ExportMarker m=original;if(!operation)assert(c.captureSnapshot(&s)==C::Ok);d.fault(0,Disk::Before);assert(run(c,s,m)==C::Ok);calls=d.calls;}
  auto verify=[&](Disk&d,Alloc&a){d.fault(0,Disk::Before);d.retainedRootMode=0;C reboot(6,d.backend(),limits(),a.api());assert(reboot.load()==C::Ok);assert(reboot.revision()==oldRevision||reboot.revision()==oldRevision+1);const bool newer=reboot.revision()!=oldRevision;assert(map(reboot)==oldMap);assert(reboot.sequence()==(operation?3:2));
   if(operation==0){assert(reboot.formatVersion()==(newer?2:1)&&reboot.exportPrepared()==newer&&groups(d)==oldGroups&&reboot.identity().generation==5);if(newer){C::ExportMarker m;assert(reboot.preparedExport(&m)==C::Ok&&m.through()==2&&m.prefixCount()==2);assert(reboot.acknowledgeExport(m,identity(6))==C::Ok);}}
   else if(operation==1){assert(reboot.exportPrepared()!=newer&&reboot.identity().generation==(newer?6:5));assert(ids(reboot)==(newer?std::vector<uint64_t>{3}:std::vector<uint64_t>{1,2,3}));assert(groups(d)==(newer?Bytes(oldGroups.begin()+original.prefixBytes(),oldGroups.end()):oldGroups));if(!newer)assert(reboot.acknowledgeExport(original,identity(6))==C::Ok);}
   else {assert(reboot.exportPrepared()!=newer&&reboot.identity().generation==5&&groups(d)==oldGroups&&ids(reboot)==(std::vector<uint64_t>{1,2,3}));if(!newer)assert(reboot.retireExport(original,identity(5))==C::Ok);}
  };
  for(unsigned power=0;power<2;++power)for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=calls;++cut){Disk d=seed;Alloc a;
   try {C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);C::Snapshot s;C::ExportMarker m=original;if(!operation)assert(c.captureSnapshot(&s)==C::Ok);d.fault(cut,Disk::Mode(mode),power!=0);const int rc=run(c,s,m);assert(rc==C::Ok||rc==C::Io||rc==C::Uncertain);if(rc==C::Uncertain){assert(!c.ready());assert(c.preparedExport(&m)==C::Uncertain);}}
   catch(const Disk::PowerCut&){a.discardVolatileAfterPowerCut();}
   verify(d,a);++reached;
  }
  // Retained before root switch, after switch, and during checked readback.
  for(unsigned mode=1;mode<=3;++mode){Disk d=seed;Alloc a;{C c(6,d.backend(),limits(),a.api());assert(c.load()==C::Ok);C::Snapshot s;C::ExportMarker m=original;if(!operation)assert(c.captureSnapshot(&s)==C::Ok);d.retainedRootMode=mode;const int rc=run(c,s,m);assert(rc==C::Retained&&!c.ready());const auto callsBefore=d.calls;assert(c.preparedExport(&m)==C::Uncertain&&c.acknowledgeExport(m,identity(6))==C::Uncertain&&c.retireExport(m,identity(5))==C::Uncertain&&d.calls==callsBefore);if(mode==1)assert(d.root==oldRoot);else assert(d.root!=oldRoot);}
   verify(d,a);++retained;
  }
 }
 printf("durable marker: %u before/after/torn error/power-cut arm/ack/retire cases and %u Retained root cases; complete old/new reload and safe completion PASS\n",reached,retained);
}
int main(){compatibilityAndBounds();captureAndNewerEdits();corruptions();allocationFailures();failureMatrix();puts("UNCONNECTED NATIVE CORE PROOF ONLY: synthetic IDs/data, no SD evidence implementation or production/hardware qualification");}
