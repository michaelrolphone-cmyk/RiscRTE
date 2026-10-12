#include "native_rebase_roundtrip_fixture.h"
#include "experimental/native_replica/CheckedNativeExportRecovery.h"
#include <array>
using Recovery=RiscExperimental::CheckedNativeExportRecovery;
// Deterministic test-only freshness source. Production must supply actual fresh
// native incarnation material; this monotonically unique fixture is not entropy.
std::array<uint8_t,16> freshIncarnation(){static uint64_t next=1;std::array<uint8_t,16> id{};memcpy(id.data(),"TESTONLY",8);for(unsigned i=0;i<8;++i)id[8+i]=uint8_t(next>>(8*i));++next;return id;}
Recovery::Quiescence quiescence(bool&quiet){return {&quiet,[](void*p){return *static_cast<bool*>(p);}};}
int startDurable(Nf::Fixture&f,Sd&sd,T&writer){auto incarnation=freshIncarnation();return nativeCall(f,[&]{return writer.startDurable(f.core,sd.volume(),f.core.formatVersion()==1?incarnation.data():nullptr);});}
AppStore storeOf(C&core){AppStore s;C::Snapshot snapshot;assert(core.captureSnapshot(&snapshot)==C::Ok);for(auto&r:descriptors(snapshot.map(),snapshot.mapSize(),snapshot.count()))s.rows[r.key]=Bytes(static_cast<const uint8_t*>(r.data),static_cast<const uint8_t*>(r.data)+r.size);return s;}
void laterEdit(Nf::Fixture&f,const char*value="later-value"){
 M::Intent intent{};auto absent=credential("synthetic-later-absent","");assert(M::make(M::Forget,absent,nullptr,&intent));C::Mutation change{"later.opaque",C::Put,value,uint32_t(strlen(value))};
 assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&change,1,intent.bytes,M::IntentBytes);})==C::Ok);M::wipe(&intent,sizeof intent);
}
void laterWifi(Nf::Fixture&f){AppStore local=storeOf(f.core);auto api=local.api();uint32_t count=0;assert(portable_wifi_profile_count(&api,&count)==0);unsigned slot=UINT32_MAX;
 for(unsigned i=0;i<count;++i){portable_wifi_credentials c{};if(portable_wifi_profile_load(&api,i,&c)==0&&!strcmp(c.ssid,"synthetic-alpha"))slot=i;portable_wifi_credentials_clear(&c);}assert(slot!=UINT32_MAX);
 auto before=credential("synthetic-alpha","synthetic-two"),after=credential("synthetic-alpha","synthetic-three");appEdit(f,local,M::Save,slot,after,&before);laterEdit(f);
}
Bytes pendingAfter(C&core,uint64_t after){Bytes out;for(;;){uint8_t bytes[1024]{};uint64_t id=0;uint32_t size=0;int rc=core.pending(after,&id,bytes,sizeof bytes,&size);if(rc==C::Missing)break;assert(rc==C::Ok);for(unsigned i=0;i<8;++i)out.push_back(uint8_t(id>>(8*i)));out.insert(out.end(),bytes,bytes+size);C::wipe(bytes,sizeof bytes);after=id;}return out;}
Recovery::Decision recover(Nf::Fixture&f,Files&files){Sd sd(files);Hash hash;bool quiet=true;Recovery r(f.core,hash.api(),sd.guard(),quiescence(quiet));assert(nativeCall(f,[&]{return r.start(sd.volume());})==C::Ok);drain(sd,r);assert(r.state()==Recovery::State::Verified&&!r.exitSafe());auto decision=r.decision();assert(nativeCall(f,[&]{return r.completeNative();})==C::Ok&&r.exitSafe());return decision;}
void exportDurable(Nf::Fixture&f,Files&files){const auto original=intents(f.core);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok&&f.core.exportPrepared()&&intents(f.core)==original&&sd.io==0);drain(sd,writer);assert(writer.state()==T::State::Verified&&intents(f.core)==original);assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Ok&&writer.exitSafe()&&!f.core.exportPrepared()&&!f.core.pendingCount());}
void completeRecovery(Nf::Fixture&f,Files&files){if(f.core.exportPrepared())recover(f,files);if(f.core.pendingCount())exportDurable(f,files);assert(!f.core.exportPrepared()&&!f.core.pendingCount());}
void publishedPrefix(){
 for(unsigned mode=0;mode<3;++mode){Setup setup;Bytes latest,newer;C::ExportMarker captured;uint64_t revision=0;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);revision=f.core.revision();Sd sd(setup.sd);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok&&sd.io==0);assert(f.core.preparedExport(&captured)==C::Ok&&captured.through()==8&&captured.prefixCount()==8&&f.core.headerSize()==C::HeaderV2&&f.core.revision()==revision+1);
   if(mode==0){laterWifi(f);}
   drain(sd,writer);assert(writer.state()==T::State::Verified);if(mode==1)laterWifi(f);
   if(mode<2){latest=mapOf(f.core);newer=pendingAfter(f.core,8);assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Ok&&f.core.pendingCount()==2&&mapOf(f.core)==latest&&intents(f.core)==newer);}
   else writer.cancel(); // Durable marker survives checked, completed cancellation.
  }
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);if(mode==2){assert(f.core.exportPrepared());laterWifi(f);latest=mapOf(f.core);newer=pendingAfter(f.core,8);assert(recover(f,setup.sd)==Recovery::Decision::AcknowledgePrefix);}
   assert(f.core.pendingCount()==2&&mapOf(f.core)==latest&&intents(f.core)==newer&&f.core.identity().generation==10&&!f.core.exportPrepared());assert(f.core.acknowledgeExport(captured,captured.target())==C::Stale);exportDurable(f,setup.sd);assert(mapOf(f.core)==latest&&f.core.identity().generation==11);
  }
  {Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok&&reboot.core.formatVersion()==2&&mapOf(reboot.core)==latest&&!reboot.core.pendingCount());}
 }
 puts("durable export chain: live and reboot recovery acknowledge exactly 8 captured groups while newer Wi-Fi overwrite/opaque intents and complete current map survive PASS");
}
void oldBaseRetirement(){Setup setup;Bytes latest,pending;C::ExportMarker marker;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);Sd sd(setup.sd);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok&&sd.io==0);assert(f.core.preparedExport(&marker)==C::Ok);laterWifi(f);latest=mapOf(f.core);pending=intents(f.core);writer.cancel();drain(sd,writer);assert(writer.state()==T::State::Cancelled&&writer.exitSafe()&&f.core.exportPrepared());}
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);assert(recover(f,setup.sd)==Recovery::Decision::RetireUnpublished);assert(mapOf(f.core)==latest&&intents(f.core)==pending&&f.core.identity().generation==9&&!f.core.exportPrepared());assert(f.core.retireExport(marker,marker.base())==C::Stale);exportDurable(f,setup.sd);assert(mapOf(f.core)==latest&&f.core.identity().generation==10);}
 puts("durable export chain: verified exact old base retires marker only, preserves every newer edit and recaptures without retaining the old map PASS");
}
void recoveryGuards(){Setup setup;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);Sd sd(setup.sd);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);writer.cancel();drain(sd,writer);}NativeImage armed;
 for(unsigned mode=0;mode<8;++mode){armed.restore();Files files=setup.sd;Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto root=nativeRoot(),original=intents(f.core);C::ExportMarker selected;assert(f.core.preparedExport(&selected)==C::Ok);
  if(mode<3){auto map=seedProfiles(32);auto bytes=map.map();auto id=identity(bytes,map.rows.size(),mode==0?99:10);if(mode==1)id.lineage[0]^=1;if(mode==2){bytes.back()^=1;id=identity(bytes,map.rows.size(),10);}files.clear();save(files,bytes,map.rows.size(),id);}
  if(mode==3)files.at(pagePath(setup.remoteId,0))[0]^=1;
  Sd sd(files);Hash hash;bool quiet=mode!=4;Recovery r(f.core,hash.api(),sd.guard(),quiescence(quiet));int rc=nativeCall(f,[&]{return r.start(sd.volume());});
  if(mode==4){assert(rc==C::Conflict&&sd.io==0);}else{assert(rc==C::Ok);drain(sd,r);
   if(mode<=3)assert(r.state()==Recovery::State::Failed);
   else {assert(r.state()==Recovery::State::Verified);if(mode==5)quiet=false;if(mode==6)sd.lease=false;if(mode==7)r.cancel();
    assert((mode==7?r.completeNative():nativeCall(f,[&]{return r.completeNative();}))==(mode==7?C::Stale:C::Conflict));}
  }
  assert(nativeRoot()==root&&intents(f.core)==original&&f.core.exportPrepared());
 }
 // Read/recovery may span newer native mutations, but stale selected handles may not.
 armed.restore();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(setup.sd);Hash hash;bool quiet=true;Recovery r(f.core,hash.api(),sd.guard(),quiescence(quiet));assert(nativeCall(f,[&]{return r.start(sd.volume());})==C::Ok);drain(sd,r);assert(r.state()==Recovery::State::Verified);laterWifi(f);const auto pending=intents(f.core),map=mapOf(f.core);assert(nativeCall(f,[&]{return r.completeNative();})==C::Ok&&intents(f.core)==pending&&mapOf(f.core)==map);}
 puts("durable export chain: exact identity/content, writer quiescence, held same-medium lease, cancellation and newer-during-verification guards PASS");
}
void envelopeBoundary(){
 Nf::empty();Files files;AppStore original;for(unsigned i=0;i<32;++i)original.rows["full."+std::to_string(i)]=Bytes(2000,uint8_t(i));original.rows["tail"]=Bytes(700,42);
 auto bytes=original.map();assert(bytes.size()==65360&&bytes.size()>65512-C::HeaderV2-4);auto rows=descriptors(bytes.data(),bytes.size(),original.rows.size());auto base=identity(bytes,rows.size(),5);save(files,bytes,rows.size(),base);Bytes current,pending;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.initialize(rows.data(),rows.size(),base);})==C::Ok);Sd sd(files);Hash readerHash,writerHash;Reader reader(f.core,readerHash.api(),sd.guard());
  C::Mutation deletion{"full.0",C::Delete,nullptr,0};assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&deletion,1,"x",1);})==C::Ok);T writer(writerHash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);writer.cancel();drain(sd,writer);current=mapOf(f.core);pending=intents(f.core);
  assert(reader.start(sd.volume())==C::Ok);drain(sd,reader);assert(reader.state()==Reader::State::Failed&&reader.error()==C::NoSpace);assert(f.core.exportPrepared());
 }
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);assert(recover(f,files)==Recovery::Decision::RetireUnpublished&&mapOf(f.core)==current&&intents(f.core)==pending);exportDurable(f,files);assert(mapOf(f.core)==current&&f.core.identity().generation==6);}
 puts("durable export chain: 65,360-byte prior v1 base remains verifiable for retirement after v2 upgrade while ordinary reader enforces current 65,124-byte import budget PASS");
}
void v2ReceiverBudget(){
 Nf::empty();Files files;AppStore initial;initial.rows["k"]={'a'};const auto map=initial.map();auto base=identity(map,1,1);save(files,map,1,base);Nf::Fixture f;C::Value value{"k","a",1};assert(nativeCall(f,[&]{return f.core.initialize(&value,1,base);})==C::Ok);exportDurable(f,files);
 C::Mutation change{"k",C::Put,"b",1};assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&change,1,"x",1);})==C::Ok);C::Snapshot snapshot;assert(f.core.captureSnapshot(&snapshot)==C::Ok&&snapshot.headerSize()==C::HeaderV2);const auto root=nativeRoot(),pending=intents(f.core);Sd sd(files);Reader::Candidate candidate;acquire(f.core,sd,candidate);
 R::Limits limits;limits.nativeImageBytes=snapshot.imageSize()-1;R shortReceiver(f.core,6,limits,C::heap());assert(shortReceiver.begin(remote(candidate))==C::NoSpace);
 limits.nativeImageBytes=snapshot.imageSize();R receiver(f.core,6,limits,C::heap());assert(receiver.begin(remote(candidate))==C::Ok);uint64_t id=0;uint32_t n=0;char original[16]{};assert(receiver.pending(receiver.viewRevision(),0,&id,original,sizeof original,&n)==C::Ok&&n==1);
 C::Mutation bigger{"k",C::Put,"bb",2};R::Batch batch{6,receiver.viewRevision(),receiver.frontier(),id,original,n,&bigger,1};assert(receiver.stage(batch)==C::NoSpace&&receiver.resolvedCount()==0&&nativeRoot()==root);
 batch.mutations=&change;assert(receiver.stage(batch)==C::Ok&&nativeRoot()==root);assert(nativeCall(f,[&]{return receiver.finish(receiver.viewRevision(),receiver.frontier());})==C::Ok&&f.core.headerSize()==C::HeaderV2&&intents(f.core)==pending);exportDurable(f,files);
 puts("durable export chain: v2 held receiver counts real captured header/groups, admits exact byte limit, rejects one-byte excess before staging and retains original pending intent PASS");
}
void durableSdFaults(){Setup setup;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);}NativeImage merged;unsigned calls=0,steps=0,cases=0,handoffs=0;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Files files=setup.sd;Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);steps=drain(sd,writer);assert(writer.state()==T::State::Verified);calls=sd.io;writer.cancel();}
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=calls;++cut){merged.restore();Files files=setup.sd;Bytes latest;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);laterWifi(f);latest=mapOf(f.core);sd.fault=cut;sd.after=after;sd.crash=crash;try{drain(sd,writer);}catch(const Sd::Cut&){}assert(sd.hit&&f.core.exportPrepared()&&f.core.pendingCount()==10&&mapOf(f.core)==latest);}
  resetProcess();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok&&mapOf(f.core)==latest&&f.core.pendingCount()==10);completeRecovery(f,files);assert(mapOf(f.core)==latest);}++cases;
 }
 for(unsigned mode=0;mode<3;++mode)for(unsigned cut=0;cut<=steps;++cut){merged.restore();Files files=setup.sd;Bytes latest;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);laterWifi(f);latest=mapOf(f.core);for(unsigned i=0;i<cut&&writer.state()==T::State::Running;++i)poll(sd,writer);
   if(mode==0){writer.cancel();drain(sd,writer);assert(writer.state()==T::State::Cancelled);}else if(mode==1){sd.lease=false;if(writer.state()==T::State::Verified)assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Conflict);else drain(sd,writer);}
   assert(f.core.exportPrepared()&&f.core.pendingCount()==10&&mapOf(f.core)==latest);
  }
  resetProcess();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);completeRecovery(f,files);assert(mapOf(f.core)==latest);}++handoffs;
 }
 printf("durable export chain: %u SD publication returned-error/power cuts and %u cancel/lease/restart handoffs recover with newer native writes and exact prefix authority PASS\n",cases,handoffs);
}
void recoveryReadFaults(){Setup setup;Files source=setup.sd;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,source);Sd sd(source);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Verified);writer.cancel();laterWifi(f);}NativeImage armed;
 unsigned io=0,steps=0,cases=0,handoffs=0,retained=0;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(source);Hash hash;bool quiet=true;Recovery r(f.core,hash.api(),sd.guard(),quiescence(quiet));assert(nativeCall(f,[&]{return r.start(sd.volume());})==C::Ok);steps=drain(sd,r);io=sd.io;assert(r.state()==Recovery::State::Verified);r.cancel();}
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=io;++cut){armed.restore();Files files=source;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto root=nativeRoot(),pending=intents(f.core);Sd sd(files);Hash hash;bool quiet=true;Recovery r(f.core,hash.api(),sd.guard(),quiescence(quiet));assert(nativeCall(f,[&]{return r.start(sd.volume());})==C::Ok);sd.fault=cut;sd.after=after;sd.crash=crash;try{drain(sd,r);}catch(const Sd::Cut&){}assert(sd.hit&&nativeRoot()==root&&intents(f.core)==pending&&f.core.exportPrepared());
   if(r.state()==Recovery::State::Retained){++retained;auto calls=sd.io;r.cancel();for(unsigned i=0;i<5;++i)poll(sd,r,false);assert(!r.exitSafe()&&r.state()==Recovery::State::Retained&&sd.io==calls&&r.completeNative()==C::Stale);}
  }
  resetProcess();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);assert(recover(f,files)==Recovery::Decision::AcknowledgePrefix&&f.core.pendingCount()==2);}++cases;
 }
 for(unsigned mode=0;mode<4;++mode)for(unsigned cut=0;cut<=steps;++cut){armed.restore();Files files=source;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto root=nativeRoot(),pending=intents(f.core);Sd sd(files);Hash hash;bool quiet=true;Recovery r(f.core,hash.api(),sd.guard(),quiescence(quiet));assert(nativeCall(f,[&]{return r.start(sd.volume());})==C::Ok);for(unsigned i=0;i<cut&&r.state()==Recovery::State::Running;++i)poll(sd,r);
   if(mode==0){r.cancel();drain(sd,r);assert(r.state()==Recovery::State::Cancelled);}else if(mode<3){if(mode==1)sd.lease=false;else quiet=false;if(r.state()==Recovery::State::Verified)assert(nativeCall(f,[&]{return r.completeNative();})==C::Conflict);else drain(sd,r);assert(r.state()==Recovery::State::Failed);}
   assert(nativeRoot()==root&&intents(f.core)==pending&&f.core.exportPrepared());
  }
  resetProcess();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);assert(recover(f,files)==Recovery::Decision::AcknowledgePrefix&&f.core.pendingCount()==2);}++handoffs;
 }
 printf("durable export chain: %u recovery-reader failures (%u retained) and %u cancel/lease/quiescence/reset handoffs preserve selected marker until fresh checked evidence PASS\n",cases,retained,handoffs);
}
void markerNativeFaults(){Setup setup;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);}NativeImage merged;
 unsigned cases=0,retained=0,uncertain=0;
 for(unsigned action=0;action<3;++action){merged.restore();Files sdInitial=setup.sd;Bytes latest,all,prefixSuffix;
  if(action){Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(sdInitial);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);laterWifi(f);if(action==2){drain(sd,writer);assert(writer.state()==T::State::Verified);}writer.cancel();drain(sd,writer);}
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);latest=mapOf(f.core);all=intents(f.core);prefixSuffix=pendingAfter(f.core,8);}NativeImage start;
  for(const char*kind:{"nvs-open","nvs-probe","nvs-read","nvs-set","nvs-commit","write","partial","read","sync","close","rename-before","rename-after"})for(bool after:{false,true})for(unsigned cut=1;cut<200;++cut){start.restore();Files files=sdInitial;bool reached=false;
   {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());bool quiet=true;Recovery recovery(f.core,hash.api(),sd.guard(),quiescence(quiet));
    if(action){assert(nativeCall(f,[&]{return recovery.start(sd.volume());})==C::Ok);drain(sd,recovery);assert(recovery.state()==Recovery::State::Verified&&recovery.decision()==(action==2?Recovery::Decision::AcknowledgePrefix:Recovery::Decision::RetireUnpublished));}
    Nf::fault=kind;Nf::cut=cut;Nf::after=after;Nf::nativeCalls=Nf::fsCalls=0;Nf::armed=true;int rc=action?nativeCall(f,[&]{return recovery.completeNative();}):startDurable(f,sd,writer);Nf::armed=false;reached=Nf::nativeCalls>=cut||Nf::fsCalls>=cut;
    assert(rc==C::Ok||rc==C::Io||rc==C::NoSpace||rc==C::Uncertain||rc==C::Retained);if(!action)assert(sd.io==0);
    if(rc==C::Uncertain){assert(!f.core.ready());++uncertain;}if(rc==C::Retained){++retained;assert(!f.store.exitSafe());const auto metadata=Nf::nvs;const unsigned io=Nf::nvsTotal+Nf::fsTotal;uint64_t object=99;auto b=f.store.backend();assert(b.reserve(b.context,512,&object)==C::Retained&&object==0);if(action){recovery.cancel();assert(!recovery.exitSafe());}else{writer.cancel();assert(!writer.exitSafe());}assert(Nf::nvs==metadata&&io==Nf::nvsTotal+Nf::fsTotal);}
   }
   resetProcess();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok&&mapOf(f.core)==latest);assert(intents(f.core)==all||(action==2&&intents(f.core)==prefixSuffix));completeRecovery(f,files);assert(mapOf(f.core)==latest);}
   if(!reached)break;
   ++cases;
  }
 }
 printf("durable export chain: %u actual native marker-arm/retire/prefix-ack failures (%u retained, %u uncertain) recover old/new roots with original/newer map and prefix membership intact PASS\n",cases,retained,uncertain);
}
int main(int argc,char**argv){assert(argc==2);setbuf(stdout,nullptr);Nf::rootPath=argv[1];std::filesystem::create_directories(Nf::rootPath);publishedPrefix();oldBaseRetirement();recoveryGuards();envelopeBoundary();v2ReceiverBudget();durableSdFaults();recoveryReadFaults();markerNativeFaults();assert(Nf::handles.empty());puts("DISCONNECTED DURABLE-EXPORT PROOF: fixed synthetic incarnation source only; no initial dataset creation, migration, SDK/UI/.90/port or hardware qualification");}
