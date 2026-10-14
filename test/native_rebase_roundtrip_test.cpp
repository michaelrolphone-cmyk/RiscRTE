#include "native_rebase_roundtrip_fixture.h"
void basicRoundtrip(){Setup s;Bytes finalMap;C::Identity finalId{};uint32_t keyCount=0;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto original=intents(f.core);rebase(f,s.sd);assert(intents(f.core)==original&&f.core.identity().generation==9);expectProfiles(f.core);finalMap=mapOf(f.core);exportAck(f,s.sd);expectProfiles(f.core);finalId=f.core.identity();assert(finalId.generation==10);C::Snapshot snap;assert(f.core.captureSnapshot(&snap)==C::Ok);keyCount=snap.count();assert(keyCount>16);}
 {Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);assert(!reboot.core.pendingCount()&&mapOf(reboot.core)==finalMap);expectProfiles(reboot.core);}
 // Model a complete native erase only AFTER the full dataset reached checked
 // SD publication and durable native acknowledgement. No native-only survival.
 Nf::empty();assert(Nf::nvs.empty()&&std::filesystem::is_empty(Nf::rootPath));
 {Nf::Fixture erased;assert(nativeCall(erased,[&]{return erased.core.load();})==C::Missing);Sd sd(s.sd);Reader::Candidate candidate;acquire(erased.core,sd,candidate);assert(candidate.count()==keyCount&&candidate.mapSize()==finalMap.size()&&!memcmp(candidate.map(),finalMap.data(),finalMap.size()));
  // Explicit test-owned restore authority. Initialize the FULL verified map in
  // one checked native publication; never install an empty/default placeholder.
  auto rows=descriptors(candidate.map(),candidate.mapSize(),candidate.count());auto id=candidate.identity();assert(id.generation==finalId.generation&&!memcmp(id.digest,finalId.digest,32));assert(nativeCall(erased,[&]{return erased.core.initialize(rows.data(),rows.size(),id);})==C::Ok);candidate.reset();expectProfiles(erased.core);assert(mapOf(erased.core)==finalMap&&!erased.core.pendingCount());}
 {Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);expectProfiles(reboot.core);assert(mapOf(reboot.core)==finalMap&&!reboot.core.pendingCount());}
 printf("checked full chain: %u keys, 33 surviving networks, 8 offline Save/update/Forget/unchanged intents, checked SD ack, full native erase and exact verified restore PASS\n",keyCount);
}
void stagedRestarts(){Setup s;NativeImage before;unsigned cases=0;
 for(unsigned cut=0;cut<=8;++cut){before.restore();Files files=s.sd;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Reader::Candidate candidate;acquire(f.core,sd,candidate);R r(f.core,6,{},C::heap());assert(r.begin(remote(candidate))==C::Ok);resolve(r,f.core,cut);if(cut<8)assert(r.finish(r.viewRevision(),r.frontier())==C::Conflict);assert(r.abandon()==C::Ok&&mapOf(f.core)==s.oldMap&&intents(f.core)==s.pending);}
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);assert(intents(f.core)==s.pending&&mapOf(f.core)==s.oldMap);rebase(f,files);expectProfiles(f.core);exportAck(f,files);}++cases;}
 printf("checked full chain: %u staged-prefix abandon/reboot boundaries retain original intents and successfully re-resolve/export PASS\n",cases);
}
void admissionAndConflict(){Setup s;NativeImage before;unsigned cases=0;
 // Correctly hashed, fully verified foreign/rollback/divergent sources do not
 // acquire authority from validity or generation. A bad page never yields bytes.
 for(unsigned mode=0;mode<7;++mode){before.restore();Files files=s.sd;Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto root=nativeRoot();
  if(mode==0){files.at(pagePath(s.remoteId,0))[24]^=1;Sd sd(files);Hash h;Reader reader(f.core,h.api(),sd.guard());assert(reader.start(sd.volume())==C::Ok);drain(sd,reader);Reader::Candidate c;assert(reader.state()==Reader::State::Failed&&reader.take(&c)==C::Stale&&!c.valid());}
  else {if(mode<=3){AppStore remoteMap=seedProfiles(32);auto bytes=remoteMap.map();auto id=identity(bytes,remoteMap.rows.size(),mode==2?6:mode==3?7:99);if(mode==1)id.lineage[0]^=1;files.clear();save(files,bytes,remoteMap.rows.size(),id);}
   if(mode==4){AppStore m=seedProfiles(32);auto api=m.api();auto c=credential("synthetic-alpha","synthetic-conflict");assert(portable_wifi_profile_save(&api,32,&c)==0);auto b=m.map();files.clear();save(files,b,m.rows.size(),identity(b,m.rows.size(),9));}
   Sd sd(files);Reader::Candidate c;acquire(f.core,sd,c);R::Limits lim{};
   if(mode==5){C::Snapshot snap;assert(f.core.captureSnapshot(&snap)==C::Ok);lim.nativeImageBytes=C::Header+c.mapSize()+4;}
   if(mode==6){C::Snapshot snap;assert(f.core.captureSnapshot(&snap)==C::Ok);lim.nativeImageBytes=C::Header+c.mapSize()+snap.imageSize()-C::Header-snap.mapSize()-4+4;}
   R r(f.core,6,lim,C::heap());int rc=r.begin(remote(c));
   if(mode<=3)assert(rc==C::Conflict);else if(mode==5)assert(rc==C::NoSpace);else {assert(rc==C::Ok);Bridge bridge(r);W stage;auto ready=prepare(r,0,bridge,stage);if(mode==4)assert(ready==W::Conflict);else assert(ready==W::Ready&&stage.commit(bridge.target())==W::Capacity);assert(r.resolvedCount()==0);}
  }
  assert(nativeRoot()==root&&mapOf(f.core)==s.oldMap&&intents(f.core)==s.pending);++cases;
 }
 // Stale native frontier refuses already prepared semantic results.
 before.restore();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(s.sd);Reader::Candidate c;acquire(f.core,sd,c);R r(f.core,6,{},C::heap());assert(r.begin(remote(c))==C::Ok);Bridge bridge(r);W stage;assert(prepare(r,0,bridge,stage)==W::Ready);C::Mutation change{"concurrent",C::Put,"kept",4};M::Intent i{};auto absent=credential("synthetic-extra-absent","");assert(M::make(M::Forget,absent,nullptr,&i));assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&change,1,i.bytes,sizeof i.bytes);})==C::Ok);assert(stage.commit(bridge.target())==W::Stale&&f.core.pendingCount()==9);assert(get(f.core,"concurrent")=="kept");}
 printf("checked full chain: %u verified corruption/lineage/rollback/divergence/schema/capacity refusals plus concurrent frontier preserve native state PASS\n",cases);
}


void readerFaults(){Setup s;NativeImage initial;unsigned calls=0,cases=0;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(s.sd);Reader::Candidate c;acquire(f.core,sd,c);calls=sd.io;}
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=calls;++cut){initial.restore();Files files=s.sd;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);auto root=nativeRoot();Sd sd(files);Hash hash;Reader reader(f.core,hash.api(),sd.guard());assert(reader.start(sd.volume())==C::Ok);sd.fault=cut;sd.after=after;sd.crash=crash;
   try{drain(sd,reader);}catch(const Sd::Cut&){}assert(sd.hit&&intents(f.core)==s.pending&&nativeRoot()==root&&sd.mutations==0);Reader::Candidate c;if(reader.state()==Reader::State::Verified){assert(!crash);assert(reader.take(&c)==C::Ok&&c.valid()&&c.identity().generation==9);AppStore expected=seedProfiles(32);auto bytes=expected.map();assert(c.mapSize()==bytes.size()&&!memcmp(c.map(),bytes.data(),bytes.size()));}else assert(reader.take(&c)==C::Stale&&!c.valid());
   if(reader.state()==Reader::State::Retained){unsigned io=sd.io;reader.cancel();for(unsigned i=0;i<3;++i)poll(sd,reader,false);assert(!reader.exitSafe()&&sd.io==io);}
  }
  resetProcess();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,files);exportAck(f,files);expectProfiles(f.core);}++cases;
 }
 initial.restore();Files files=s.sd;files[std::string(Index)+".risc-replace"]=files.at(Index);files.erase(Index);
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,files);exportAck(f,files);expectProfiles(f.core);}
 printf("checked full chain: %u reader returned-error/power-cut cases expose only verified bytes and no native mutation; backup-only index and fresh acquisition recover PASS\n",cases);
}
void partitionCapacity(){Setup s;Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(s.sd);Reader::Candidate candidate;acquire(f.core,sd,candidate);R r(f.core,6,{},C::heap());assert(r.begin(remote(candidate))==C::Ok);resolve(r,f.core);auto root=nativeRoot();
 Nf::otherUsed=0x80000-32768;assert(nativeCall(f,[&]{return r.finish(r.viewRevision(),r.frontier());})==C::NoSpace);assert(r.held()&&r.resolvedCount()==8&&nativeRoot()==root&&intents(f.core)==s.pending);Nf::otherUsed=0;
 assert(nativeCall(f,[&]{return r.finish(r.viewRevision(),r.frontier());})==C::Ok);exportAck(f,s.sd);expectProfiles(f.core);
 puts("checked full chain: actual AppData partition/recovery-reserve capacity refusal preserves held resolved candidate and permits explicit finish retry PASS");
}
void writerFaults(){Setup s;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,s.sd);}
 NativeImage merged;unsigned calls=0,steps=0,cases=0;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Files files=s.sd;Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);steps=drain(sd,writer);assert(writer.state()==T::State::Verified);calls=sd.io;writer.cancel();}
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=calls;++cut){merged.restore();Files files=s.sd;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);auto root=nativeRoot();Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);sd.fault=cut;sd.after=after;sd.crash=crash;
   try{drain(sd,writer);}catch(const Sd::Cut&){}assert(sd.hit&&intents(f.core)==s.pending&&nativeRoot()==root);expectProfiles(f.core);
   if(writer.state()==T::State::Retained){unsigned io=sd.io;writer.cancel();for(unsigned i=0;i<3;++i)poll(sd,writer,false);assert(!writer.exitSafe()&&sd.io==io&&writer.acknowledgeNative()==C::Stale);}
  }
  // Destruction above models whole-process reset, never retained quiescence.
  resetProcess();{Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);assert(intents(reboot.core)==s.pending);expectProfiles(reboot.core);exportAck(reboot,files);expectProfiles(reboot.core);}++cases;
 }
 printf("checked full chain: %u reached SD returned-error/power-cut boundaries recover exact captured map through actual native acknowledgement PASS\n",cases);
 unsigned cancellations=0;
 for(unsigned mode=0;mode<3;++mode)for(unsigned cut=0;cut<=steps;++cut){merged.restore();Files files=s.sd;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);for(unsigned i=0;i<cut&&writer.state()==T::State::Running;++i)poll(sd,writer);
   if(mode==0){writer.cancel();drain(sd,writer);assert(writer.state()==T::State::Cancelled&&writer.exitSafe());}
   if(mode==1){sd.lease=false;if(writer.state()==T::State::Verified)assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Conflict);else drain(sd,writer);assert(writer.state()==T::State::Failed&&writer.exitSafe());}
   // mode 2 is a process reset at every owner/worker handoff, including Verified.
   assert(intents(f.core)==s.pending);expectProfiles(f.core);
  }
  resetProcess();{Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);exportAck(reboot,files);expectProfiles(reboot.core);}++cancellations;
 }
 printf("checked full chain: %u SD cancel/lease-loss/restart handoffs preserve every native intent until verified acknowledgement PASS\n",cancellations);
}
void nativePublicationFaults(){Setup s;NativeImage initial;Bytes complete;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,s.sd);complete=mapOf(f.core);}
 NativeImage merged;unsigned cases=0,retentions=0,uncertain=0;
 // This exercises actual file and NVS errors both at native rebase finish and
 // at final native acknowledgement, rather than substituting a root model.
 for(bool acknowledge:{false,true})for(const char*kind:{"nvs-open","nvs-probe","nvs-read","nvs-set","nvs-commit","write","partial","read","sync","close","rename-before","rename-after"})for(bool after:{false,true})for(unsigned cut=1;cut<200;++cut){
  if(acknowledge)merged.restore();else initial.restore();Files files=s.sd;bool reached=false;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());Reader::Candidate candidate;R receiver(f.core,6,{},C::heap());
   if(acknowledge){assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Verified);}
   else {acquire(f.core,sd,candidate);assert(receiver.begin(remote(candidate))==C::Ok);resolve(receiver,f.core);}
   Nf::fault=kind;Nf::cut=cut;Nf::after=after;Nf::nativeCalls=Nf::fsCalls=0;Nf::armed=true;
   int rc=nativeCall(f,[&]{return acknowledge?writer.acknowledgeNative():receiver.finish(receiver.viewRevision(),receiver.frontier());});Nf::armed=false;reached=Nf::nativeCalls>=cut||Nf::fsCalls>=cut;
   assert(rc==C::Ok||rc==C::Io||rc==C::NoSpace||rc==C::Uncertain||rc==C::Retained);
   if(rc==C::Uncertain){assert(!f.core.ready());++uncertain;}
   if(rc==C::Retained){++retentions;const auto metadata=Nf::nvs;const unsigned calls=Nf::nvsTotal+Nf::fsTotal;assert(!f.store.exitSafe()&&!f.files.exitSafe());
    if(acknowledge){writer.cancel();assert(writer.state()==T::State::Retained&&!writer.exitSafe()&&writer.acknowledgeNative()==C::Stale);}
    else {assert(receiver.abandon()==C::Retained&&receiver.begin(remote(candidate))==C::Retained&&receiver.finish(1,1)==C::Retained);}
    auto backend=f.store.backend();uint64_t object=1;assert(backend.reserve(backend.context,C::Header+4,&object)==C::Retained&&object==0);f.store.finishOperation();assert(Nf::nvs==metadata&&Nf::nvsTotal+Nf::fsTotal==calls);
   }
  }
  resetProcess();{Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);auto map=mapOf(reboot.core);assert(map==s.oldMap||map==complete);
   if(acknowledge){assert(map==complete);assert(reboot.core.pendingCount()==0||intents(reboot.core)==s.pending);if(reboot.core.pendingCount())exportAck(reboot,files);}
   else {assert(intents(reboot.core)==s.pending);if(map==s.oldMap)rebase(reboot,files);expectProfiles(reboot.core);exportAck(reboot,files);}
   expectProfiles(reboot.core);assert(!reboot.core.pendingCount());
  }
  if(!reached)break;
  ++cases;
 }
 printf("checked full chain: %u actual native finish/ack faults, %u retained and %u uncertain outcomes, complete old/new reload and final recovery PASS\n",cases,retentions,uncertain);
}
void acknowledgedFrontier(){Setup s;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,s.sd);}
 NativeImage merged;
 // A later native operation survives a still-live captured acknowledgement.
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(s.sd);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Verified);
  M::Intent intent{};auto absent=credential("synthetic-later-absent","");assert(M::make(M::Forget,absent,nullptr,&intent));C::Mutation change{"late.local",C::Put,"kept",4};assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&change,1,intent.bytes,sizeof intent.bytes);})==C::Ok);assert(f.core.pendingCount()==9);
  assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Ok&&f.core.pendingCount()==1&&get(f.core,"late.local")=="kept");expectProfiles(f.core);}
 // Restart plus a newer native edit makes the deterministic replay differ.
 // The generic writer must refuse, and semantic replay may conflict on an
 // intermediate Save that was superseded in the already-published checkpoint.
 // This is an explicit remaining durable-prefix/coordinator gate.
 merged.restore();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);M::Intent i{};auto absent=credential("synthetic-later-absent","");assert(M::make(M::Forget,absent,nullptr,&i));C::Mutation change{"late.local",C::Put,"kept",4};assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&change,1,i.bytes,sizeof i.bytes);})==C::Ok);}
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto pending=intents(f.core),root=nativeRoot();Sd sd(s.sd);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Failed&&writer.reason()==T::Reason::IdentityConflict&&sd.mutations==0);
  Reader::Candidate c;acquire(f.core,sd,c);R receiver(f.core,6,{},C::heap());assert(receiver.begin(remote(c))==C::Ok);Bridge bridge(receiver);W stage;assert(prepare(receiver,0,bridge,stage)==W::Conflict);assert(intents(f.core)==pending&&nativeRoot()==root&&f.core.pendingCount()==9&&get(f.core,"late.local")=="kept");expectProfiles(f.core);}
 puts("checked full chain: live captured ack retains later edit; post-restart divergent replay and superseded intermediate intent fail closed (durable-prefix recovery remains outside proof) PASS");
}
int main(int argc,char**argv){assert(argc==2);setbuf(stdout,nullptr);Nf::rootPath=argv[1];std::filesystem::create_directories(Nf::rootPath);basicRoundtrip();stagedRestarts();admissionAndConflict();readerFaults();partitionCapacity();writerFaults();nativePublicationFaults();acknowledgedFrontier();assert(Nf::handles.empty());puts("DISCONNECTED HOST PROOF ONLY: explicit test restore authority; no creation/migration, product coordinator, port, SDK, UI, .90 activation or hardware qualification");}
