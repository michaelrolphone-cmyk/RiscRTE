#include "native_rebase_roundtrip_fixture.h"
#include "native_fresh_source_fixture.h"
#include "experimental/native_replica/NativeBaselineAdmission.h"
#include "experimental/native_replica/CheckedNativeExportRecovery.h"
using A=RiscExperimental::NativeBaselineAdmission;
A::Digest baselineHash(Hash&h){auto d=h.api();return {d.context,d.begin,d.update,d.finish};}
struct Source {
 Nf::FreshNativeFacts facts;C::FreshInitReceipt selectedReceipt;
 bool retainOnLease=false,retainOnEpoch=false;
 AppStore legacy;uint64_t epoch=11,revision=7;bool nativeJournal=false,held=true,retained=false,reentry=false,duplicate=false,changeEpoch=false;unsigned callbacks=0,reentries=0;int fail=0;C*core=nullptr;A*factory=nullptr;
 bool nativeArtifacts()const{auto ns=Nf::nvs.find("rnr00000006");if(ns!=Nf::nvs.end()&&!ns->second.empty())return true;for(auto&e:std::filesystem::recursive_directory_iterator(Nf::rootPath))if(e.is_regular_file())return true;return false;}
 bool outsideArtifacts()const{for(auto&e:std::filesystem::directory_iterator(Nf::rootPath))if(e.path().filename()!="n00000006")return true;return false;}
 A::RootState rootState()const{auto ns=Nf::nvs.find("rnr00000006");if(ns==Nf::nvs.end()||!ns->second.count("root"))return A::RootState::NeverInitialized;const auto&b=ns->second.at("root");return b.size()==48&&!memcmp(b.data(),"RTENINIT",8)?A::RootState::InterruptedInit:A::RootState::Selected;}
 A::Witness witness(bool imported)const{A::Witness w;w.namespaceId=6;w.root=imported?rootState():A::RootState::NeverInitialized;w.kind=imported?1:0;w.revision=revision+uint64_t(nativeJournal);auto b=legacy.map();
  if(!imported){Bytes inventory(64);memcpy(inventory.data(),"synthetic-native-baseline-owner-v1",34);put32(inventory.data()+36,6);put32(inventory.data()+40,A::FreshAbsenceV1);put32(inventory.data()+44,uint32_t(w.revision));put32(inventory.data()+48,uint32_t(w.revision>>32));put32(inventory.data()+52,legacy.rows.size());put32(inventory.data()+56,b.size());inventory.insert(inventory.end(),b.begin(),b.end());b=std::move(inventory);}
  auto id=identity(b,legacy.rows.size(),revision);memcpy(w.fingerprint,id.digest,32);return w;}
 A::Selection selection()const{auto w=witness(true);A::Selection s;s.kind=w.kind;s.revision=w.revision;memcpy(s.fingerprint,w.fingerprint,32);return s;}
 void touch(){assert(Nf::onWorker&&Nf::lease&&factory&&!factory->exitSafe());++callbacks;if(reentry){assert(factory&&core);C::Initialization nested;C::Ticket t;assert(factory->admitFreshEmpty(&nested)==C::Busy);assert(core->load()==C::Busy&&core->capture(&t)==C::Busy);C::Mutation m{"reentrant",C::Put,"x",1};assert(core->apply(0,&m,1,"x",1)==C::Busy);++reentries;}}
 A::Guard guard(){return {this,[](void*p){auto&s=*static_cast<Source*>(p);if(s.retainOnLease){s.retained=true;s.held=false;}return Nf::onWorker&&Nf::lease&&s.held;},[](void*p){auto&s=*static_cast<Source*>(p);if(s.retainOnEpoch){s.retained=true;++s.epoch;}return s.epoch;}};}
 A::Sources api(){return {this,
  [](void*p){auto&s=*static_cast<Source*>(p);return s.retained||s.facts.inspector.retained()?int(C::Retained):int(C::Ok);},
  [](void*p,uint32_t ns,A::Witness*out){auto&s=*static_cast<Source*>(p);s.touch();if(s.fail)return s.fail;if(ns!=6||s.nativeJournal||s.nativeArtifacts()||s.outsideArtifacts()||!s.legacy.rows.empty())return int(C::Conflict);int rc=s.facts.absent();if(rc)return rc;*out=s.witness(false);if(s.changeEpoch)++s.epoch;return int(C::Ok);},
  [](void*p,uint32_t ns,const A::Selection&,const A::Rows&rows,A::Witness*out){auto&s=*static_cast<Source*>(p);s.touch();if(s.fail)return s.fail;if(ns!=6)return int(C::Conflict);for(auto&r:s.legacy.rows){int rc=rows.append(rows.context,r.first.c_str(),r.second.data(),r.second.size());if(rc)return rc;if(s.duplicate){(void)rows.append(rows.context,r.first.c_str(),r.second.data(),r.second.size());break;}}*out=s.witness(true);if(s.changeEpoch)++s.epoch;return int(C::Ok);},
  [](void*p,uint32_t ns,const A::Witness&prior){auto&s=*static_cast<Source*>(p);s.touch();if(s.fail)return s.fail;if(!prior.kind){if(s.nativeJournal||s.outsideArtifacts()||!s.legacy.rows.empty())return int(C::Conflict);int rc=s.facts.hasReceipt()?s.facts.resume(s.selectedReceipt):s.facts.absent();if(rc)return rc;}auto now=s.witness(prior.kind!=0);if(s.changeEpoch)++s.epoch;return ns==now.namespaceId&&prior.root==now.root&&prior.kind==now.kind&&prior.revision==now.revision&&!memcmp(prior.fingerprint,now.fingerprint,32)?int(C::Ok):int(C::Conflict);},
  A::FreshAbsenceV1,
  [](void*p,uint32_t ns,const C::FreshInitReceipt&r,A::Witness*out){auto&s=*static_cast<Source*>(p);s.touch();if(s.fail)return s.fail;if(ns!=6||s.nativeJournal||s.outsideArtifacts()||!s.legacy.rows.empty())return int(C::Conflict);int rc=s.facts.resume(r);if(rc)return rc;s.selectedReceipt=r;*out=s.witness(false);if(s.changeEpoch)++s.epoch;return int(C::Ok);}
 };}
};
void bind(Source&s,A&factory,C&core){s.factory=&factory;s.core=&core;}
int fresh(Nf::Fixture&f,Source&s){Hash hash;A factory(&s,f.core,s.guard(),baselineHash(hash),s.api());bind(s,factory,f.core);C::Initialization a;int rc=nativeCall(f,[&]{return s.facts.hasReceipt()?factory.admitFreshResume(&a):factory.admitFreshEmpty(&a);});return rc?rc:nativeCall(f,[&]{return f.core.initializeFreshEmpty(a);});}
int imported(Nf::Fixture&f,Source&s){Hash hash;A factory(&s,f.core,s.guard(),baselineHash(hash),s.api());bind(s,factory,f.core);C::Initialization a;auto selection=s.selection();int rc=nativeCall(f,[&]{return factory.admitImported(selection,&a);});return rc?rc:nativeCall(f,[&]{return f.core.initializeImportedBaseline(a);});}
Bytes provenance(C&core){auto b=core.baseline();return Bytes(reinterpret_cast<const uint8_t*>(&b),reinterpret_cast<const uint8_t*>(&b)+sizeof b);}
void nativeOnlyFresh(){
 for(unsigned mode=0;mode<7;++mode){resetProcess();Nf::empty();Nf::Fixture f;Source source;auto hooks=source.api();hooks.materialize=nullptr;
  A factory(&source,f.core,source.guard(),A::Digest{},hooks,C::Allocator{});bind(source,factory,f.core);C::Initialization a;
  if(mode==1)source.legacy.rows["native.legacy"]={'v'};
  if(mode==2)source.nativeJournal=true;
  if(mode==4){FILE*file=fopen((Nf::rootPath+"/unselected-bank").c_str(),"wb");assert(file);assert(fputc(1,file)!=EOF&&fclose(file)==0);}
  if(mode==6)Nf::nvs["rnr00000006"]["next"]=Bytes(24,1);
  const auto initial=Nf::nvs;
  const int rc=nativeCall(f,[&]{return factory.admitFreshEmpty(&a);});
  if(mode==1||mode==2||mode==4||mode==6){assert(rc==C::Conflict&&!a.valid()&&Nf::nvs==initial);continue;}
  assert(rc==C::Ok);if(mode==3)source.nativeJournal=true;
  if(mode==5){FILE*file=fopen((Nf::rootPath+"/late-bank").c_str(),"wb");assert(file);assert(fputc(1,file)!=EOF&&fclose(file)==0);}
  const int initialized=nativeCall(f,[&]{return f.core.initializeFreshEmpty(a);});
  if(mode==3||mode==5)assert(initialized==C::Conflict&&Nf::nvs.empty());
  else {assert(initialized==C::Ok&&f.core.ready()&&f.core.baseline().origin()==C::Origin::FreshEmpty);assert(nativeCall(f,[&]{return Nf::put(f.core,"offline","usable");})==C::Ok);}
 }
 puts("baseline provenance: FreshEmpty uses native inventory with no SD provider, import/digest hooks or entropy; existing and newly appeared native legacy rows/journals/banks and orphan metadata block publication PASS");
}
void freshAdoption(){resetProcess();Nf::empty();Source source;Bytes origin,expected,pending;
 {Nf::Fixture f;assert(fresh(f,source)==C::Ok&&f.core.formatVersion()==3&&f.core.headerSize()==480&&f.core.baseline().origin()==C::Origin::FreshEmpty&&!f.core.identity().known);origin=provenance(f.core);AppStore local;auto before=credential("synthetic-new","synthetic-first"),after=credential("synthetic-new","synthetic-second");appEdit(f,local,M::Save,0,before);appEdit(f,local,M::Save,0,after,&before);auto forget=credential("synthetic-absent","");M::Intent intent{};assert(M::make(M::Forget,forget,nullptr,&intent));C::Mutation deletion{"absent",C::Delete,nullptr,0};assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&deletion,1,intent.bytes,M::IntentBytes);})==C::Ok);
  C::Ticket t;assert(f.core.capture(&t)==C::Ok);const auto root=nativeRoot();assert(f.core.acknowledge(t,identity({},0,9))==C::Conflict&&nativeRoot()==root&&f.core.pendingCount()==3);
  auto remoteMap=seedProfiles(20);auto map=remoteMap.map();auto id=identity(map,remoteMap.rows.size(),9);Files files;save(files,map,remoteMap.rows.size(),id);Sd sd(files);Reader::Candidate selected;acquire(f.core,sd,selected);R receiver(f.core,6,{},C::heap());assert(receiver.begin(remote(selected))==C::Ok);
  for(unsigned i=0;i<3;++i){Bridge bridge(receiver);W stage;assert(prepare(receiver,i,bridge,stage)==W::Ready);assert(stage.commit(bridge.target())==W::Committed);}pending=intents(f.core);assert(nativeCall(f,[&]{return receiver.finish(receiver.viewRevision(),receiver.frontier());})==C::Ok&&intents(f.core)==pending&&f.core.headerSize()==480&&provenance(f.core)==origin);
  Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.startDurable(f.core,sd.volume());})==C::Invalid&&f.core.headerSize()==480&&provenance(f.core)==origin);
  const uint8_t incarnation[16]={'T','E','S','T','O','N','L','Y',3};T durable(hash.api(),sd.guard());assert(nativeCall(f,[&]{return durable.startDurable(f.core,sd.volume(),incarnation);})==C::Ok);assert(nativeCall(f,[&]{return Nf::put(f.core,"newer.opaque","kept");})==C::Ok);expected=mapOf(f.core);drain(sd,durable);assert(durable.state()==T::State::Verified);assert(nativeCall(f,[&]{return durable.acknowledgeNative();})==C::Ok&&f.core.pendingCount()==1&&provenance(f.core)==origin&&mapOf(f.core)==expected);
  C::Snapshot snapshot;C::ExportMarker marker;assert(f.core.captureSnapshot(&snapshot)==C::Ok&&snapshot.formatVersion()==3&&snapshot.headerSize()==480&&snapshot.baseline().origin()==C::Origin::FreshEmpty);auto next=f.core.identity();++next.generation;assert(nativeCall(f,[&]{return f.core.prepareExport(snapshot,next,nullptr,&marker);})==C::Ok);assert(nativeCall(f,[&]{return f.core.retireExport(marker,marker.base());})==C::Ok&&provenance(f.core)==origin&&mapOf(f.core)==expected);
 }
 {Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok&&provenance(reboot.core)==origin&&mapOf(reboot.core)==expected&&reboot.core.pendingCount()==1);}
 puts("baseline provenance: entropy-free FreshEmpty, actual Wi-Fi Save/update/Forget adoption, all original groups retained, v3 durable marker/newer edit/ack/retire/reboot preserve exact origin PASS");
}
void v3Recovery(){using Recovery=RiscExperimental::CheckedNativeExportRecovery;
 for(bool published:{false,true}){resetProcess();Nf::empty();Files files;auto base=identity({},0,20);save(files,{},0,base);Bytes origin,latest,allPending;
  {Nf::Fixture f;Source source;assert(fresh(f,source)==C::Ok);origin=provenance(f.core);assert(nativeCall(f,[&]{return f.core.installResolved(f.core.revision(),nullptr,0,base,nullptr,0);})==C::Ok);assert(nativeCall(f,[&]{return Nf::put(f.core,"captured","before");})==C::Ok);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());const uint8_t incarnation[16]={'T','E','S','T','O','N','L','Y',4};assert(nativeCall(f,[&]{return writer.startDurable(f.core,sd.volume(),incarnation);})==C::Ok);
   if(published)drain(sd,writer);
   writer.cancel();drain(sd,writer);assert(writer.exitSafe()&&f.core.exportPrepared());
  }
  {Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok&&reboot.core.formatVersion()==3&&provenance(reboot.core)==origin);assert(nativeCall(reboot,[&]{return Nf::put(reboot.core,"after.reboot","newer");})==C::Ok);latest=mapOf(reboot.core);allPending=intents(reboot.core);Sd sd(files);Hash hash;bool quiet=true;Recovery r(reboot.core,hash.api(),sd.guard(),{&quiet,[](void*p){return *static_cast<bool*>(p);}});assert(nativeCall(reboot,[&]{return r.start(sd.volume());})==C::Ok);drain(sd,r);assert(r.state()==Recovery::State::Verified&&r.decision()==(published?Recovery::Decision::AcknowledgePrefix:Recovery::Decision::RetireUnpublished));assert(nativeCall(reboot,[&]{return r.completeNative();})==C::Ok&&r.exitSafe());assert(provenance(reboot.core)==origin&&mapOf(reboot.core)==latest&&reboot.core.pendingCount()==(published?1u:2u));if(!published)assert(intents(reboot.core)==allPending);assert(!reboot.core.exportPrepared());}
 }
 puts("baseline provenance: v3 published-prefix acknowledgement and exact-old-base retirement recover after reboot/newer edits with byte-identical provenance PASS");
}
void importedGate(){resetProcess();Nf::empty();Source source;source.legacy.rows["untouched.a"]={'a'};source.legacy.rows["untouched.b"]={'b'};source.legacy.rows["edited"]={'o'};const auto original=source.legacy.rows;Bytes origin,pending,map;
 {Nf::Fixture f;assert(imported(f,source)==C::Ok&&f.core.baseline().origin()==C::Origin::ImportedLegacy&&f.core.baseline().archiveObligation());origin=provenance(f.core);assert(nativeCall(f,[&]{return Nf::put(f.core,"edited","new");})==C::Ok);pending=intents(f.core);map=mapOf(f.core);const auto root=nativeRoot();AppStore selected;selected.rows["edited"]={'r'};auto bytes=selected.map();auto id=identity(bytes,1,100);R receiver(f.core,6,{},C::heap());assert(receiver.begin({6,1,uint32_t(bytes.size()),bytes.data(),id})==C::Conflict);C::Value row{"edited","r",1};uint64_t resolved=1;assert(f.core.installResolved(f.core.revision(),&row,1,id,&resolved,1)==C::Conflict);C::Ticket ticket;assert(f.core.capture(&ticket)==C::Ok&&f.core.acknowledge(ticket,id)==C::Conflict);assert(nativeRoot()==root&&mapOf(f.core)==map&&intents(f.core)==pending&&source.legacy.rows==original&&provenance(f.core)==origin);assert(fresh(f,source)==C::Conflict);}
 {Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok&&mapOf(reboot.core)==map&&intents(reboot.core)==pending&&provenance(reboot.core)==origin);}
 resetProcess();Nf::empty();Source empty;Nf::Fixture f;assert(imported(f,empty)==C::Ok&&f.core.baseline().origin()==C::Origin::ImportedLegacy&&!f.core.pendingOnlyAdoptionAllowed());
 puts("baseline provenance: imported untouched rows and later intents cannot be discarded by receiver, direct installResolved, volatile acknowledgement or FreshEmpty initializer; empty imported origin remains imported PASS");
}
void oldUnknownAndPermits(){
 for(bool remove:{false,true}){resetProcess();Nf::empty();Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.initialize(nullptr,0,{});})==C::Ok);if(remove){assert(nativeCall(f,[&]{return Nf::put(f.core,"deleted","once");})==C::Ok);C::Mutation d{"deleted",C::Delete,nullptr,0};assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&d,1,"delete",6);})==C::Ok);}assert(f.core.baseline().origin()==C::Origin::Unclassified&&!f.core.pendingOnlyAdoptionAllowed()&&mapOf(f.core).empty());C::Ticket t;assert(f.core.capture(&t)==C::Ok&&f.core.acknowledge(t,identity({},0,99))==C::Conflict);Source s;assert(fresh(f,s)==C::Conflict);}
 {resetProcess();Nf::empty();Native n;C core(6,n.api(),{65512,4096,1024},C::heap());C::Value row{"existing","v",1};assert(core.initialize(&row,1,{})==C::Ok);n.root.clear();assert(core.initialize(nullptr,0,identity({},0,77))==C::Conflict&&core.ready()&&!core.identity().known);uint32_t size=0;char value=0;assert(core.get("existing",&value,1,&size)==C::Ok&&value=='v');}
 unsigned cases=0;
 for(unsigned mode=0;mode<10;++mode){resetProcess();Nf::empty();Nf::Fixture f;Source s;Hash hash;A factory(&s,f.core,s.guard(),baselineHash(hash),s.api());bind(s,factory,f.core);C::Initialization a;
  if(mode==0){s.held=false;assert(nativeCall(f,[&]{return factory.admitFreshEmpty(&a);})==C::Busy&&!a.valid());}
  else if(mode==1){s.changeEpoch=true;assert(nativeCall(f,[&]{return factory.admitFreshEmpty(&a);})==C::Stale&&!a.valid());}
  else if(mode==2){s.reentry=true;assert(nativeCall(f,[&]{return factory.admitFreshEmpty(&a);})==C::Ok);assert(f.core.initializeImportedBaseline(a)==C::Invalid&&a.valid());assert(nativeCall(f,[&]{return f.core.initializeFreshEmpty(a);})==C::Ok&&s.reentries>=2&&!a.valid());assert(f.core.initializeFreshEmpty(a)==C::Invalid);}
  else if(mode==3){assert(nativeCall(f,[&]{return factory.admitFreshEmpty(&a);})==C::Ok);C::Initialization newer;assert(nativeCall(f,[&]{return factory.admitFreshEmpty(&newer);})==C::Ok);assert(nativeCall(f,[&]{return f.core.initializeFreshEmpty(a);})==C::Stale);assert(nativeCall(f,[&]{return f.core.initializeFreshEmpty(newer);})==C::Ok);}
  else {assert(nativeCall(f,[&]{return factory.admitFreshEmpty(&a);})==C::Ok);if(mode==4)++s.epoch;if(mode==5)s.legacy.rows["appeared"]={'x'};if(mode==6)s.held=false;if(mode==7){s.retained=true;s.held=false;}if(mode==8){Nf::Fixture wrong(1);assert(wrong.core.initializeFreshEmpty(a)==C::Invalid&&a.valid());}if(mode==9){C::Value row{"existing","x",1};assert(nativeCall(f,[&]{return f.core.initialize(&row,1,{});})==C::Ok);}
   const int rc=nativeCall(f,[&]{return f.core.initializeFreshEmpty(a);});assert(rc==(mode==4?C::Stale:mode==6?C::Busy:mode==7?C::Retained:mode==8?C::Ok:C::Conflict));if(mode==7)assert(!factory.exitSafe());
  }
  ++cases;
 }
 resetProcess();puts("baseline provenance: unclassified empty/deleted maps stay unclassified; purpose/core/serial/lease/source/root binding, one-shot use and source callback reentry refusals PASS");assert(cases==10);
}
uint32_t imageCrc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned j=0;j<8;++j)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
uint64_t imageWide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
Bytes&selectedImage(Native&n){return n.objects.at(imageWide(n.root.data()+24));}
void repairImage(Native&n,bool record=true){auto&image=selectedImage(n);if(record)put32(image.data()+C::HeaderV2+92,imageCrc(image.data()+C::HeaderV2,92));put32(image.data()+image.size()-4,imageCrc(image.data(),image.size()-4));put32(n.root.data()+32,word(image.data()+image.size()-4));put32(n.root.data()+44,imageCrc(n.root.data(),44));}
// Selected synthetic bytes qualify format decoding only. The minimal Native
// backend deliberately has no Fresh receipt capability; actual admission and
// interruption recovery run through the checked fixture above and below.
Native provenanceImage(bool imported){Native n;C core(6,n.api(),{65512,4096,1024},C::heap());
 if(!imported){auto image=Nf::FreshNativeFacts::canonical();n.objects[n.next++]=image;n.root=Bytes(48);memcpy(n.root.data(),"RTENROOT",8);put32(n.root.data()+8,6);put32(n.root.data()+12,image.size());put32(n.root.data()+16,1);put32(n.root.data()+24,1);put32(n.root.data()+32,imageCrc(image.data(),480));put32(n.root.data()+44,imageCrc(n.root.data(),44));assert(core.load()==C::Ok&&core.baseline().origin()==C::Origin::FreshEmpty);return n;}
 Source source;source.legacy.rows["baseline"]={'v'};Hash hash;A factory(&source,core,source.guard(),baselineHash(hash),source.api());bind(source,factory,core);C::Initialization admission;auto selection=source.selection();assert(factory.admitImported(selection,&admission)==C::Ok&&core.initializeImportedBaseline(admission)==C::Ok);return n;
}
void framingAndCapacity(){resetProcess();Nf::empty();const Native freshImage=provenanceImage(false),importImage=provenanceImage(true);unsigned corruptions=0;
 for(unsigned mode=0;mode<22;++mode){Native n=mode<14?freshImage:importImage;auto&image=selectedImage(n);auto*p=image.data()+C::HeaderV2;
  switch(mode){case 0:image[6]='4';break;case 1:p[0]^=1;break;case 2:put32(p+8,7);break;case 3:put32(p+12,3);break;case 4:put32(p+88,4);break;case 5:put32(p+20,1);break;case 6:put32(p+16,21);break;case 7:p[24]=1;break;case 8:p[56]=1;break;case 9:p[92]^=1;break;case 10:put32(image.data()+108,image.size()+1);break;case 11:put32(image.data()+96,1);break;case 12:image[C::Header]='x';break;case 13:image[7]='x';break;case 14:memset(p+24,0,32);break;case 15:memset(p+56,0,32);break;case 16:put32(p+20,0);break;case 17:put32(p+16,65029);break;case 18:put32(p+20,UINT32_MAX);break;case 19:put32(p+88,0);break;case 20:put32(image.data()+8,1);break;case 21:p[6]='2';break;}
  repairImage(n,mode!=9);C broken(6,n.api(),{65512,4096,1024},C::heap());assert(broken.load()==C::Io&&!broken.ready());Source source;Hash hash;A factory(&source,broken,source.guard(),baselineHash(hash),source.api());bind(source,factory,broken);C::Initialization admission;assert(factory.admitFreshEmpty(&admission)==C::Invalid);++corruptions;
 }
 {Native old;C core(6,old.api(),{65512,4096,1024},C::heap());auto id=identity({},0,4);assert(core.initialize(nullptr,0,id)==C::Ok);C::Snapshot snapshot;assert(core.captureSnapshot(&snapshot)==C::Ok);auto next=id;++next.generation;uint8_t incarnation[16]={8};C::ExportMarker marker;assert(core.prepareExport(snapshot,next,incarnation,&marker)==C::Ok&&core.retireExport(marker,id)==C::Ok);auto&bytes=selectedImage(old);put32(bytes.data()+12,0);memset(bytes.data()+40,0,56);repairImage(old,false);C unknown(6,old.api(),{65512,4096,1024},C::heap());assert(unknown.load()==C::Ok&&unknown.formatVersion()==2&&unknown.baseline().origin()==C::Origin::Unclassified&&!unknown.pendingOnlyAdoptionAllowed());C::Ticket ticket;assert(unknown.capture(&ticket)==C::Ok&&unknown.acknowledge(ticket,id)==C::Conflict);}
 for(uint32_t capacity:{483u,484u}){resetProcess();Nf::empty();Nf::Fixture fixture;C core(6,fixture.store.backend(),{capacity,4096,1024},C::heap());Source source;Hash hash;A factory(&source,core,source.guard(),baselineHash(hash),source.api());bind(source,factory,core);C::Initialization a;int rc=nativeCall(fixture,[&]{return factory.admitFreshEmpty(&a);});if(capacity==483)assert(rc==C::NoSpace&&Nf::nvs.empty());else {assert(rc==C::Ok&&nativeCall(fixture,[&]{return core.initializeFreshEmpty(a);})==C::Ok);C::Snapshot snapshot;assert(core.captureSnapshot(&snapshot)==C::Ok&&snapshot.imageSize()==484);}}resetProcess();Nf::empty();
 for(unsigned extra=0;extra<2;++extra){Native n;C core(6,n.api(),{65512,4096,1024},C::heap());Source source;for(unsigned i=0;i<32;++i)source.legacy.rows["full."+std::to_string(i)]=Bytes(2000,uint8_t(i));source.legacy.rows["tail"]=Bytes(368+extra,1);assert(source.legacy.map().size()==65028+extra);Hash hash;A factory(&source,core,source.guard(),baselineHash(hash),source.api());bind(source,factory,core);C::Initialization a;auto selection=source.selection();int rc=factory.admitImported(selection,&a);if(extra)assert(rc==C::NoSpace&&!a.valid()&&n.root.empty());else assert(rc==C::Ok&&core.initializeImportedBaseline(a)==C::Ok&&selectedImage(n).size()==65512);}
 printf("baseline provenance: %u repaired-outer-checksum semantic corruptions refuse; old unknown v2 remains Unclassified; 484-byte minimum and exact 65,028-byte payload boundaries PASS\n",corruptions);
}
void factoryFailures(){resetProcess();Nf::empty();unsigned cases=0;
 for(unsigned mode=0;mode<9;++mode){Native n;C core(6,n.api(),{65512,4096,1024},C::heap());Source source;source.legacy.rows["valid"]={'v'};Hash hash;A factory(&source,core,source.guard(),baselineHash(hash),source.api());bind(source,factory,core);auto selection=source.selection();C::Initialization a;
  if(mode==0)source.duplicate=true;
  if(mode==1){source.legacy.rows.clear();source.legacy.rows["UPPER"]={'v'};}
  if(mode==2)source.legacy.rows["valid"].clear();
  if(mode==3)source.legacy.rows["valid"]=Bytes(2049,1);
  if(mode==4)selection.fingerprint[0]^=1;
  if(mode==5)source.changeEpoch=true;
  if(mode==6)hash.broken=true;
  if(mode==7)source.fail=C::Retained;
  if(mode==8)source.reentry=true;
  const int rc=factory.admitImported(selection,&a);assert(rc==(mode==0||mode==4?C::Conflict:mode<=3?C::Invalid:mode==5?C::Stale:mode==6?C::Io:mode==7?C::Retained:C::Ok));if(mode==8){assert(source.reentries&&a.valid());const_cast<uint8_t*>(a.map())[a.mapSize()-1]^=1;assert(core.initializeImportedBaseline(a)==C::Conflict&&!a.valid());}else assert(!a.valid());assert(n.root.empty());if(mode==7){const auto callbacks=source.callbacks;assert(!factory.exitSafe()&&factory.admitImported(selection,&a)==C::Retained&&source.callbacks==callbacks);}++cases;
 }
 {Native n;C core(6,n.api(),{65512,4096,1024},C::heap());Source source;source.legacy.rows["valid"]={'v'};Hash hash;C::Allocator noMemory{nullptr,[](void*,size_t)->void*{return nullptr;},[](void*,void*){assert(false);}};A factory(&source,core,source.guard(),baselineHash(hash),source.api(),noMemory);bind(source,factory,core);C::Initialization a;auto selection=source.selection();assert(factory.admitImported(selection,&a)==C::NoMemory&&!a.valid()&&source.callbacks==0&&n.root.empty());}
 {Nf::Fixture fixture;C::Allocator noMemory{nullptr,[](void*,size_t)->void*{return nullptr;},[](void*,void*){assert(false);}};C core(6,fixture.store.backend(),{65512,4096,1024},noMemory);Source source;auto hooks=source.api();hooks.materialize=nullptr;A factory(&source,core,source.guard(),{},hooks);bind(source,factory,core);C::Initialization a;assert(nativeCall(fixture,[&]{return factory.admitFreshEmpty(&a);})==C::Ok&&nativeCall(fixture,[&]{return core.initializeFreshEmpty(a);})==C::NoMemory&&!a.valid()&&Nf::nvs.empty());assert(core.initializeFreshEmpty(a)==C::Invalid);}
 {Native n;C core(6,n.api(),{65512,4096,1024},C::heap());Source source;source.legacy.rows["secret.fixture"]={'v'};Hash hash;struct Allocation {size_t bytes=0;unsigned frees=0;} tracked;C::Allocator wiping{&tracked,[](void*p,size_t n)->void*{static_cast<Allocation*>(p)->bytes=n;return std::malloc(n);},[](void*p,void*bytes){auto&t=*static_cast<Allocation*>(p);for(size_t i=0;i<t.bytes;++i)assert(static_cast<uint8_t*>(bytes)[i]==0);++t.frees;std::free(bytes);}};A factory(&source,core,source.guard(),baselineHash(hash),source.api(),wiping);bind(source,factory,core);C::Initialization a;auto selection=source.selection();assert(factory.admitImported(selection,&a)==C::Ok&&a.mapSize());a.reset();assert(tracked.frees==1&&!a.valid()&&n.root.empty());}
 printf("baseline provenance: %u materialization/hash/source/epoch/reentry/copy-integrity failures, factory/core allocator refusals and owned-byte wiping preserve native absence and original sources PASS\n",cases);
}
void freshCustodyVisibility(){resetProcess();Nf::empty();
 struct Probe {A*factory=nullptr;unsigned allocations=0;} probe;
 C::Allocator allocator{&probe,[](void*p,size_t n)->void*{auto&v=*static_cast<Probe*>(p);assert(v.factory&&!v.factory->exitSafe());++v.allocations;return C::heap().allocate(nullptr,n);},[](void*,void*p){C::heap().release(nullptr,p);}};
 Nf::Fixture fixture;C core(6,fixture.store.backend(),S::limits(),allocator);Source source;A factory(&source,core,source.guard(),{},source.api());bind(source,factory,core);probe.factory=&factory;C::Initialization admission;
 assert(factory.exitSafe()&&nativeCall(fixture,[&]{return factory.admitFreshEmpty(&admission);})==C::Ok&&factory.exitSafe());
 assert(nativeCall(fixture,[&]{return core.initializeFreshEmpty(admission);})==C::Ok&&probe.allocations==1&&factory.exitSafe());
 puts("baseline provenance: source callbacks and Core allocation observe in-flight factory custody; completed admission becomes exit-safe PASS");
}
void lateRetainedCustody(){
 for(bool consume:{false,true})for(bool epoch:{false,true}){resetProcess();Nf::empty();Nf::Fixture fixture;Source source;A factory(&source,fixture.core,source.guard(),{},source.api());bind(source,factory,fixture.core);C::Initialization admission;
  if(consume)assert(nativeCall(fixture,[&]{return factory.admitFreshEmpty(&admission);})==C::Ok);
  if(epoch)source.retainOnEpoch=true;else source.retainOnLease=true;
  const int rc=nativeCall(fixture,[&]{return consume?fixture.core.initializeFreshEmpty(admission):factory.admitFreshEmpty(&admission);});
  assert(rc==C::Retained&&!admission.valid()&&!factory.exitSafe()&&Nf::nvs.empty());
 }
 puts("baseline provenance: late guard-observed retention outranks simultaneous lease loss/epoch change during admission and consumption PASS");
}
void initializerFaults(){unsigned cases=0,retained=0,uncertain=0,resumedFresh=0;
 for(bool legacy:{false,true})for(const char*kind:{"nvs-open","nvs-probe","nvs-read","nvs-set","nvs-commit","write","partial","read","sync","close","rename-before","rename-after"})for(bool after:{false,true})for(unsigned cut=1;;++cut){assert(cut<1000);resetProcess();Nf::empty();Source source;if(legacy)for(unsigned i=0;i<3;++i)source.legacy.rows["source."+std::to_string(i)]=Bytes(2048,uint8_t(i+1));const auto rows=source.legacy.rows;const auto expected=source.legacy.map();bool reached=false;
  {Nf::Fixture f;Hash hash;A factory(&source,f.core,source.guard(),baselineHash(hash),source.api());bind(source,factory,f.core);C::Initialization a;auto selected=source.selection();assert(nativeCall(f,[&]{return legacy?factory.admitImported(selected,&a):factory.admitFreshEmpty(&a);})==C::Ok);Nf::fault=kind;Nf::cut=cut;Nf::after=after;Nf::nativeCalls=Nf::fsCalls=0;Nf::armed=true;int rc=nativeCall(f,[&]{return legacy?f.core.initializeImportedBaseline(a):f.core.initializeFreshEmpty(a);});Nf::armed=false;reached=Nf::nativeCalls>=cut||Nf::fsCalls>=cut;assert(rc==C::Ok||rc==C::Io||rc==C::NoSpace||rc==C::Uncertain||rc==C::Retained);assert(!a.valid()&&source.legacy.rows==rows);if(rc==C::Retained){++retained;assert(!factory.exitSafe()&&(!f.store.exitSafe()||!source.facts.inspector.exitSafe()));}if(rc==C::Uncertain){++uncertain;assert(!f.core.ready());}}
  resetProcess();{Nf::Fixture f;Source restarted;restarted.legacy.rows=rows;int rc=nativeCall(f,[&]{return f.core.load();});assert(rc==C::Ok||rc==C::Missing||(!legacy&&rc==C::Initializing));if(rc==C::Ok){assert(mapOf(f.core)==expected&&f.core.baseline().origin()==(legacy?C::Origin::ImportedLegacy:C::Origin::FreshEmpty)&&!f.core.pendingCount());}else if(legacy){assert(imported(f,restarted)==C::Ok&&mapOf(f.core)==expected);}else {if(rc==C::Initializing)++resumedFresh;assert(restarted.rootState()!=A::RootState::InterruptedInit);assert(fresh(f,restarted)==C::Ok&&mapOf(f.core)==expected);}assert(source.legacy.rows==rows&&restarted.legacy.rows==rows);}
  if(!reached)break;
  ++cases;
 }
 printf("baseline provenance: %u checked fresh/import initializer faults (%u retained, %u uncertain), complete map+origin roots; %u interrupted FreshEmpty receipts resumed through fresh source/lease admission; zero source cleanup PASS\n",cases,retained,uncertain,resumedFresh);
}
int main(int argc,char**argv){assert(argc==2);setbuf(stdout,nullptr);Nf::rootPath=argv[1];std::filesystem::create_directories(Nf::rootPath);nativeOnlyFresh();freshAdoption();v3Recovery();importedGate();oldUnknownAndPermits();framingAndCapacity();factoryFailures();freshCustodyVisibility();lateRetainedCustody();initializerFaults();resetProcess();assert(Nf::handles.empty());puts("DISCONNECTED PROVENANCE CORE: synthetic source inventory only; first SD creation, whole imported-baseline disposition and product activation remain deferred; host FreshEmpty receipt recovery is explicitly checked");}
