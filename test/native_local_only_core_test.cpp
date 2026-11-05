// Core-only policy tests. Disk is an immutable-object fault model, not target
// flash qualification; real receipt/collector integration remains a separate gate.
#define main external_core_regression_main
#include "native_replica_core_test.cpp"
#undef main
#include "experimental/native_replica/NativeBaselineAdmission.h"
using A=RiscExperimental::NativeBaselineAdmission;
using P=C::DurabilityPolicy;
struct LocalSources {
 Disk&disk;bool imported=false;uint8_t digest=1;
 A::Witness witness()const{A::Witness w;w.namespaceId=6;w.root=A::RootState::NeverInitialized;w.kind=imported?1:0;w.revision=7;memset(w.fingerprint,0x5a,32);return w;}
 A::Selection selection()const{auto w=witness();A::Selection s;s.kind=w.kind;s.revision=w.revision;memcpy(s.fingerprint,w.fingerprint,32);return s;}
 A::Guard guard(){return {this,[](void*){return true;},[](void*){return uint64_t(11);}};}
 A::Digest hash(){return {this,[](void*p){static_cast<LocalSources*>(p)->digest=1;return true;},[](void*p,const void*b,uint32_t n){auto&s=*static_cast<LocalSources*>(p);const auto*v=static_cast<const uint8_t*>(b);while(n--)s.digest=uint8_t(s.digest*33+*v++);return true;},[](void*p,uint8_t out[32]){memset(out,static_cast<LocalSources*>(p)->digest|1,32);return true;}};}
 A::Sources sources(P policy){
  A::Sources s{};s.context=this;
  s.observe=[](void*){return int(C::Ok);};
  s.freshInventory=[](void*p,uint32_t ns,A::Witness*out){auto&s=*static_cast<LocalSources*>(p);if(ns!=6||!s.disk.root.empty()||s.imported)return int(C::Conflict);*out=s.witness();return int(C::Ok);};
  s.materialize=[](void*p,uint32_t ns,const A::Selection&,const A::Rows&rows,A::Witness*out){auto&s=*static_cast<LocalSources*>(p);if(ns!=6||!s.imported||!s.disk.root.empty())return int(C::Conflict);int rc=rows.append(rows.context,"opaque.future","untouched",9);if(rc)return rc;*out=s.witness();return int(C::Ok);};
  s.recheck=[](void*p,uint32_t ns,const A::Witness&w){auto&s=*static_cast<LocalSources*>(p);auto now=s.witness();return ns==6&&s.disk.root.empty()&&w.kind==now.kind&&!memcmp(w.fingerprint,now.fingerprint,32)?int(C::Ok):int(C::Conflict);};
  s.freshInventoryVersion=A::FreshAbsenceV1;
  s.resumeFreshInventory=[](void*,uint32_t,const C::FreshInitReceipt&,A::Witness*){return int(C::Conflict);};
  s.durabilityPolicy=policy;return s;
 }
};
C::Backend localBackend(Disk&d){
 auto b=d.backend();b.supportsLocalOnlyV1=[](void*){return true;};
 b.readInitialization=[](void*,C::FreshInitReceipt*){return int(C::Missing);};
 b.beginFreshInitialization=[](void*,const C::Initialization&){return int(C::Ok);};
 b.endFreshInitialization=[](void*){};return b;
}
int genesis(C&c,Disk&d,bool imported=false,P sourcePolicy=P::LocalOnlyV1){
 LocalSources s{d,imported};A factory(&s,c,s.guard(),s.hash(),s.sources(sourcePolicy));
 C::Initialization admission;int rc=imported?factory.admitImported(s.selection(),&admission):factory.admitFreshEmpty(&admission);
 return rc?rc:imported?c.initializeImportedBaseline(admission):c.initializeFreshEmpty(admission);
}
Bytes selected(const Disk&d){uint64_t id=0;for(unsigned i=0;i<8;++i)id|=uint64_t(d.root[24+i])<<(8*i);return d.objects.at(id);}
void localBasics(bool imported){
 Disk d;C c(6,localBackend(d),{8192,4096,1024},C::heap(),P::LocalOnlyV1);
 assert(genesis(c,d,imported)==C::Ok);assert(c.formatVersion()==5&&c.headerSize()==512);
 assert(c.revision()==1&&!c.sequence()&&!c.pendingCount()&&!c.localProofCount());
 const auto baseline=c.baseline();const Bytes provenance(reinterpret_cast<const uint8_t*>(&baseline),reinterpret_cast<const uint8_t*>(&baseline)+sizeof baseline);
 for(unsigned i=1;i<=2048;++i){
  const uint64_t old=c.revision();assert(put(c,"ssid","same","save")==C::Ok);
  assert(c.revision()==old+1&&c.sequence()==i&&c.localCheckpointThrough()==i-1);
  assert(c.pendingCount()==0&&c.localProofCount()==1&&pending(c).empty());
  char intent[16];uint32_t size=0;uint64_t id=0;assert(c.localProof(&id,intent,sizeof intent,&size)==C::Ok&&id==i&&size==4&&!memcmp(intent,"save",4));
  C::Mutation m{"ssid",C::Put,"same",4};assert(c.apply(old,&m,1,"save",4)==C::Stale);
  const auto b=c.baseline();assert(!memcmp(&b,provenance.data(),provenance.size()));
  if(imported)assert(b.archiveObligation()&&get(c,"opaque.future")=="untouched");
  assert(selected(d).size()<1024&&d.objects.size()==1);
 }
 C reboot(6,localBackend(d),{8192,4096,1024},C::heap(),P::LocalOnlyV1);assert(reboot.load()==C::Ok&&reboot.sequence()==2048);
 const auto before=d.root;C::Ticket ticket;C::Snapshot snap;C::ExportMarker marker;
 assert(reboot.capture(&ticket)==C::Conflict&&reboot.captureSnapshot(&snap)==C::Conflict);
 assert(reboot.prepareExport(snap,base(1),nullptr,&marker)==C::Conflict);
 assert(reboot.preparedExport(&marker)==C::Conflict&&reboot.acknowledgeExport(marker,base(1))==C::Conflict);
 assert(reboot.retireExport(marker,base(1))==C::Conflict&&reboot.acknowledge(ticket,base(1))==C::Conflict);
 assert(reboot.installResolved(reboot.revision(),nullptr,0,base(1),nullptr,0)==C::Conflict&&d.root==before);
 C external(6,d.backend(),{8192,4096,1024},C::heap());assert(external.load()!=C::Ok&&d.root==before);
}
void policyRefusals(){
 Disk d;C c(6,localBackend(d),{8192,4096,1024},C::heap(),P::LocalOnlyV1);
 assert(genesis(c,d,false,P::ExternalVolume)==C::Conflict&&d.root.empty());
 assert(c.initialize(nullptr,0,{})==C::Conflict&&d.root.empty());
 C noBackend(6,d.backend(),{8192,4096,1024},C::heap(),P::LocalOnlyV1);
 assert(genesis(noBackend,d)!=C::Ok&&d.root.empty());
 for(bool imported:{false,true}){
  Disk old;C external(6,localBackend(old),{8192,4096,1024},C::heap());
  assert(genesis(external,old,imported,P::ExternalVolume)==C::Ok);
  const auto root=old.root;const auto objects=old.objects;
  C local(6,localBackend(old),{8192,4096,1024},C::heap(),P::LocalOnlyV1);
  assert(local.load()!=C::Ok&&genesis(local,old,imported)!=C::Ok);
  assert(old.root==root&&old.objects==objects);
 }
 Disk v1=initial(1);const auto v1root=v1.root;C localV1(6,localBackend(v1),limits(),C::heap(),P::LocalOnlyV1);
 assert(localV1.load()!=C::Ok&&v1.root==v1root);
 for(const char*magic:{"RTENFI1","RTENLI1","RTENSI1","RTENINIT"}){
  Disk old;old.root.resize(48);memcpy(old.root.data(),magic,8);const auto root=old.root;
  C local(6,localBackend(old),{8192,4096,1024},C::heap(),P::LocalOnlyV1);
  LocalSources s{old};A factory(&s,local,s.guard(),s.hash(),s.sources(P::LocalOnlyV1));C::Initialization a;
  assert(factory.admitFreshResume(&a)==C::Conflict&&factory.admitImportedResume(&a)==C::Conflict);
  assert(genesis(local,old)!=C::Ok&&old.root==root&&old.objects.empty());
 }
}
void localFaults(){
 Disk initialDisk;{C c(6,localBackend(initialDisk),{8192,4096,1024},C::heap(),P::LocalOnlyV1);assert(genesis(c,initialDisk,true)==C::Ok);assert(put(c,"ssid","old")==C::Ok);}
 unsigned operations=0;{Disk d=initialDisk;C c(6,localBackend(d),{8192,4096,1024},C::heap(),P::LocalOnlyV1);assert(c.load()==C::Ok);d.fault(0,Disk::Before);assert(put(c,"ssid","new")==C::Ok);operations=d.calls;}
 for(unsigned cut=1;cut<=operations;++cut)for(auto mode:{Disk::Before,Disk::After,Disk::Torn}){
  Disk d=initialDisk;{C c(6,localBackend(d),{8192,4096,1024},C::heap(),P::LocalOnlyV1);assert(c.load()==C::Ok);d.fault(cut,mode);(void)put(c,"ssid","new");}
  d.fault(0,Disk::Before);C reboot(6,localBackend(d),{8192,4096,1024},C::heap(),P::LocalOnlyV1);assert(reboot.load()==C::Ok);const auto value=get(reboot,"ssid");assert(value=="old"||value=="new");assert(get(reboot,"opaque.future")=="untouched"&&reboot.baseline().archiveObligation());
  assert(reboot.sequence()==(value=="old"?1u:2u)&&reboot.localCheckpointThrough()+1==reboot.sequence());
 }
 Disk full=initialDisk;C c(6,localBackend(full),{8192,4096,1024},C::heap(),P::LocalOnlyV1);assert(c.load()==C::Ok);const auto root=full.root;full.capacity=0;assert(put(c,"ssid","new")==C::NoSpace&&full.root==root&&get(c,"ssid")=="old");
}
int main(){localBasics(false);localBasics(true);policyRefusals();localFaults();puts("LocalOnly V5 core: bounded checkpoint, opaque provenance, policy refusal and fault model PASS");return 0;}
