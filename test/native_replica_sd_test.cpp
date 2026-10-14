#include "native_replica_sd_fixture.h"
static void basics(){
 Native n;Files files;seed(n,files,1025);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd sd(files);Hash h;T t(h.api(),sd.guard());assert(t.start(c,sd.volume())==0);
 for(unsigned i=0;i<1000;++i){sd.tick(t,false);}assert(sd.io==0&&c.pendingCount()==1);
 edit(c,"later");sd.run(t);assert(t.state()==T::State::Verified&&sd.ends&&c.pendingCount()==2&&!t.exitSafe());
 sd.nativeWorker=false;assert(t.acknowledgeNative()==C::Busy);sd.nativeWorker=true;assert(t.acknowledgeNative()==0&&t.exitSafe());assert(c.pendingCount()==1&&get(c,"edited")=="later"&&get(c,"k01024")==std::string(32,'v'));
 C reboot(6,n.api(),limits(),C::heap());assert(reboot.load()==0&&reboot.pendingCount()==1&&get(reboot,"edited")=="later");
 puts("SD transport: 1025 opaque keys, immutable complete snapshot, exact captured frontier, newer edit survives, checked close before worker-only acknowledgement PASS");
}
static void identityGates(){
 for(unsigned mode=0;mode<10;++mode){Native n;Files files;seed(n,files);const Files original=files;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);
  auto id=c.identity();Bytes idx=files[Index];if(mode==0)files.clear();if(mode==1)files[Index][0]^=1;
  if(mode==2){id.lineage[0]^=1;T::makeIndex(files[Index].data(),6,5200,100,id);}
  if(mode==3){--id.generation;T::makeIndex(files[Index].data(),6,5200,100,id);}
  if(mode==4){id.digest[0]^=1;T::makeIndex(files[Index].data(),6,5200,100,id);}
  if(mode==5){++id.generation;T::makeIndex(files[Index].data(),6,5200,100,id);}
  if(mode==6)files.at(pagePath(id,0))[30]^=1;
  if(mode==7)files.erase(pagePath(id,1));
  Sd sd(files);if(mode==8)sd.available=false;if(mode==9)sd.lease=false;Hash h;T t(h.api(),sd.guard());
  const int rc=t.start(c,sd.volume());if(rc==0){sd.run(t);assert(t.state()==T::State::Failed);}
  else assert(mode==9&&rc==C::Conflict&&t.reason()==T::Reason::CustodyLost);
  if(mode==0)assert(t.reason()==T::Reason::CheckpointMissing);
  if(mode==8)assert(t.reason()==T::Reason::MediaUnavailable);
  if(mode>=2&&mode<=5)assert(t.reason()==T::Reason::IdentityConflict);
  assert(sd.mutations==0&&get(c,"edited")=="local"&&c.pendingCount()==1);unchanged(n);
 }
 Native n;Files f;C unknown(6,n.api(),limits(),C::heap());assert(unknown.initialize(nullptr,0,{})==0);edit(unknown);Sd sd(f);Hash h;T t(h.api(),sd.guard());assert(t.start(unknown,sd.volume())==C::Conflict&&sd.io==0&&unknown.pendingCount()==1);
 puts("SD transport: absent/not-ready/unreadable/foreign/older/newer/divergent/corrupt SD cannot change native reads or consume intents; unknown base refuses all SD work PASS");
}
static void interruptionMatrix(){
 Native initial;Files original;seed(initial,original);unsigned steps=0;
 {Native n=initial;Files f=original;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);steps=s.run(t);assert(t.state()==T::State::Verified);}
 unsigned cases=0;
 // Stop on every owner/worker boundary, both before and after copied execution.
 for(bool queued:{false,true})for(unsigned mode=0;mode<4;++mode)for(unsigned cut=0;cut<=steps;++cut){Native n=initial;Files f=original;
  {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);
   for(unsigned i=0;i<cut&&t.state()==T::State::Running;++i)s.tick(t);
   if(queued&&t.state()==T::State::Running)s.tick(t,false);
   if(mode==0){t.cancel();s.run(t);assert(t.state()==T::State::Cancelled&&t.exitSafe());}
   if(mode==1){s.available=false;t.cancel();s.run(t);assert(t.state()==T::State::Cancelled&&t.exitSafe());}
   if(mode==2){s.lease=false;if(t.state()==T::State::Verified)assert(t.acknowledgeNative()==C::Conflict);else s.run(t);assert(t.state()==T::State::Failed&&t.exitSafe());}
   // mode 3 discards all process/worker state, retaining only durable model files.
   assert(c.pendingCount()==1&&get(c,"edited")=="local");
  }
  unchanged(n);retry(n,f);++cases;
 }
 printf("SD transport: %u cancel/removal/USB-lease revocation/restart boundaries, exact replay and no pending-edit loss PASS\n",cases);
}
static void faults(){
 Native initial;Files original;seed(initial,original);unsigned io=0;
 {Native n=initial;Files f=original;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);s.run(t);assert(t.state()==T::State::Verified);io=s.io;}
 unsigned cases=0;
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=io;++cut){Native n=initial;Files f=original;
  {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);s.fault=cut;s.after=after;s.crash=crash;Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);
   try{s.run(t);}catch(const Sd::Cut&){}
   assert(s.hit&&c.pendingCount()==1&&get(c,"edited")=="local");
   if(t.state()==T::State::Retained){unsigned before=s.io;t.cancel();for(unsigned i=0;i<10;++i)s.tick(t,false);assert(!t.exitSafe()&&s.io==before&&t.acknowledgeNative()==C::Stale);}
  }
  unchanged(n);retry(n,f);++cases;
 }
 printf("SD transport: %u reached before/after returned-error/power-cut filesystem calls, torn pages, checked read/sync/close/replace and exact restart replay PASS\n",cases);
}
static void restartAndRaces(){
 for(unsigned mode=0;mode<7;++mode){Native n;Files f;seed(n,f);
  {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);s.run(t);assert(t.state()==T::State::Verified);
   if(mode==0){s.available=false;assert(t.acknowledgeNative()==C::Io);}
   if(mode==1){s.retained=true;assert(t.acknowledgeNative()==C::Retained&&!t.exitSafe());}
   if(mode==2){assert(c.load()==0);assert(t.acknowledgeNative()==C::Stale);}
   if(mode==3){edit(c,"after-restart");}
   if(mode==6){n.retainedSeal=true;assert(t.acknowledgeNative()==C::Retained);t.cancel();assert(t.state()==T::State::Retained&&!t.exitSafe());n.retainedSeal=false;}
   if(mode==4||mode==5){n.failAck=true;n.applyAck=mode==5;assert(t.acknowledgeNative()==C::Uncertain);n.failAck=false;}
  }
  C reboot(6,n.api(),limits(),C::heap());assert(reboot.load()==0);
  if(mode==3){Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(reboot,s.volume())==0);s.run(t);assert(t.state()==T::State::Failed&&t.error()==C::Conflict&&reboot.pendingCount()==2&&get(reboot,"edited")=="after-restart"&&s.mutations==0);}
  else if(mode==5)assert(!reboot.pendingCount());else {assert(reboot.pendingCount()==1);retry(n,f);}
 }
 // Tamper the index after pages are staged, before the final compare/replace.
 Native n;Files f;seed(n,f);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);
 while(!f.count("/RTECFG/00000006/.replica.next")){s.tick(t);assert(t.state()==T::State::Running);}
 f[Index][32]^=1;s.run(t);assert(t.state()==T::State::Failed&&t.error()==C::Conflict&&!s.indexReplacements&&c.pendingCount()==1);
 puts("SD transport: expired tickets, late removal/retention, ambiguous native acknowledgement, restart with newer edits and pre-replacement race fail closed PASS");
}
static void recoveryAndBounds(){
 for(bool foreign:{false,true}){Native n;Files f;seed(n,f);if(foreign){auto bytes=Bytes(5200,'x');auto id=identity(bytes,100);id.lineage[0]=91;T::makeIndex(f[Index].data(),6,5200,100,id);}f[std::string(Index)+".risc-replace"]=f.at(Index);f.erase(Index);
  C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Sd s(f);Hash h;T t(h.api(),s.guard());assert(t.start(c,s.volume())==0);s.run(t);
  if(foreign)assert(t.state()==T::State::Failed&&t.reason()==T::Reason::IdentityConflict&&!s.indexReplacements&&c.pendingCount()==1);
  else {assert(t.state()==T::State::Verified);assert(t.acknowledgeNative()==0);}
 }
 // Empty maps have no zero-byte page writes. DELETE is still explicit intent.
 Native n;Files f;auto id=identity({},0);save(f,{},0,id);C c(6,n.api(),limits(),C::heap());assert(c.initialize(nullptr,0,id)==0);C::Mutation deletion{"absent",C::Delete,nullptr,0};assert(c.apply(c.revision(),&deletion,1,"delete",6)==0);Sd sd(f);Hash h;T t(h.api(),sd.guard());assert(t.start(c,sd.volume())==0);sd.run(t);assert(t.state()==T::State::Verified&&t.acknowledgeNative()==0&&!c.pendingCount());
 // Allocation failure does not replace an already-live capture ticket.
 struct Alloc{bool fail=false;C::Allocator api(){return {this,[](void*p,size_t n)->void*{return static_cast<Alloc*>(p)->fail?nullptr:malloc(n);},[](void*,void*p){free(p);}};}} alloc;
 Native n2;Files f2;seed(n2,f2);C c2(6,n2.api(),limits(),alloc.api());assert(c2.load()==0);C::Ticket ticket;assert(c2.capture(&ticket)==0);alloc.fail=true;C::Snapshot snapshot;assert(c2.captureSnapshot(&snapshot)==C::NoMemory&&!snapshot.valid());alloc.fail=false;auto next=c2.identity();++next.generation;assert(c2.acknowledge(ticket,next)==0);
 // A completed BUSY result has consumed its operation token; never poll it.
 Native n3;Files f3;seed(n3,f3);C c3(6,n3.api(),limits(),C::heap());assert(c3.load()==0);Sd busy(f3);busy.terminalBusy=true;Hash h3;T t3(h3.api(),busy.guard());assert(t3.start(c3,busy.volume())==0);busy.run(t3);assert(t3.state()==T::State::Failed&&c3.pendingCount()==1&&t3.exitSafe());retry(n3,f3);
 // A selected page corrupted after index publication is never acknowledged or
 // silently repaired as if it were an unselected staging orphan.
 Native n4;Files f4;seed(n4,f4);C c4(6,n4.api(),limits(),C::heap());assert(c4.load()==0);Sd bad(f4);Hash h4;T t4(h4.api(),bad.guard());assert(t4.start(c4,bad.volume())==0);while(!bad.indexReplacements){bad.tick(t4);assert(t4.state()==T::State::Running);}C::Identity selected{};selected.known=true;selected.generation=8;memcpy(selected.digest,f4[Index].data()+48,32);f4.at(pagePath(selected,0))[0]^=1;bad.run(t4);assert(t4.state()==T::State::Failed&&c4.pendingCount()==1&&get(c4,"edited")=="local");
 puts("SD transport: interrupted replacement backup recovery, foreign recovered base refusal, empty-map tombstone and snapshot-allocation failure PASS");
}
static void durableSparseAndCollisions(){
 for(unsigned missing=0;missing<4;++missing){Native n;Files files;seed(n,files);C core(6,n.api(),limits(),C::heap());assert(!core.load());Sd sd(files);auto&ext=sd.download.base.base.fs.state.prepared.base.sleep.terminal.power.volume;auto&v=ext.base;if(missing==0)v.dir_open=nullptr;if(missing==1)v.dir_next=nullptr;if(missing==2)ext.dir_close_checked=nullptr;if(missing==3)ext.handle_error=nullptr;const auto original=files;const auto nativeCalls=n.calls;Hash hash;T writer(hash.api(),sd.guard());const uint8_t incarnation[16]={1};assert(writer.startDurable(core,sd.volume(),incarnation)==C::Invalid&&writer.exitSafe()&&!core.exportPrepared()&&core.pendingCount()==1&&files==original&&n.calls==nativeCalls&&!sd.io&&!sd.mutations);}
 for(unsigned collision=0;collision<10;++collision){Native n;Files files;seed(n,files);C core(6,n.api(),limits(),C::heap());assert(!core.load());Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());const uint8_t incarnation[16]={1};assert(!writer.startDurable(core,sd.volume(),incarnation));C::ExportMarker marker;assert(!core.preparedExport(&marker));C::Snapshot snapshot;assert(!core.captureSnapshot(&snapshot));const bool stage=collision>=5;const unsigned mode=collision%5;Bytes exact;
  char staged[RiscExperimental::SdReplicaStage::PathBytes];assert(RiscExperimental::SdReplicaStage::makePath(staged,sizeof staged,6,marker.target()));const std::string path=stage?staged:pagePath(marker.target(),0);if(stage){exact.resize(T::IndexBytes);T::makeIndex(exact.data(),6,marker.mapSize(),marker.count(),marker.target());}else exact.assign(snapshot.map(),snapshot.map()+4096);
  Bytes existing=exact;if(mode==0)existing.clear();if(mode==1)existing.resize(17);if(mode==2){existing.resize(17);existing.back()^=1;}if(mode==3)existing.resize(4097);files[path]=existing;sd.run(writer);
  if(mode<2||mode==4){assert(writer.state()==T::State::Verified);if(mode==4)assert(!std::count(sd.operations.begin(),sd.operations.end(),"remove"));assert(!writer.acknowledgeNative()&&!core.pendingCount()&&!core.exportPrepared());}
  else {assert(writer.state()==T::State::Failed&&writer.error()==C::Conflict&&writer.exitSafe()&&files.at(path)==existing&&core.pendingCount()==1&&core.exportPrepared()&&!sd.indexReplacements);}
 }
 puts("durable SD collision gate: sparse directory APIs refused before marker/I/O; exact full bytes reused without remove, empty/exact prefixes repaired, nonprefix/oversize target page and stage bytes preserved PASS");
}
int main(){setbuf(stdout,nullptr);basics();durableSparseAndCollisions();identityGates();interruptionMatrix();faults();restartAndRaces();recoveryAndBounds();puts("DISCONNECTED HOST TRANSPORT ONLY: no port, ABI, migration, .90 routing, Wi-Fi resolver or hardware qualification");}
