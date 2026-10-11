#include "native_rebase_test_support.h"
#include <experimental/native_replica/NativeReplicaJob.h>
void basics(){
 Disk disk;Alloc coreAlloc,receiverAlloc;receiverAlloc.verifyWipe=true;
 C core(6,disk.backend(),coreLimits(),coreAlloc.api());assert(core.initialize(nullptr,0,{})==C::Ok);
 assert(put(core,"offline","one","save-one")==C::Ok);assert(put(core,"offline","two","save-two")==C::Ok);
 assert(put(core,"offline","two","same-value-save")==C::Ok);
 const auto originalIntents=nativeIntents(core),root=disk.root;const auto revision=core.revision();
 Bytes map;append(map,"restored","newer",5);append(map,"remove","old",3);
 R candidate(core,6,receiverLimits(),receiverAlloc.api());assert(candidate.begin(remote(map,2))==C::Ok);map.assign(map.size(),0);
 assert(candidate.pendingCount()==3&&candidate.resolvedCount()==0&&get(candidate,"restored")=="newer");
 assert(candidate.finish(candidate.viewRevision(),candidate.frontier())==C::Conflict&&disk.root==root);
 uint64_t id=0;char intent[64]{};uint32_t n=0;assert(candidate.pending(candidate.viewRevision(),0,&id,intent,sizeof intent,&n)==C::Ok&&id==1&&std::string(intent,n)=="save-one");
 C::Mutation first[]={{"logical",C::Put,"one",3},{"remove",C::Delete,nullptr,0}};
 auto firstBatch=batch(candidate,1,"save-one",first,2);const auto view=firstBatch.viewRevision;
 disk.calls=0;assert(candidate.stage(firstBatch)==C::Ok&&disk.calls==0&&candidate.resolvedCount()==1);
 assert(candidate.get(view,"logical",intent,sizeof intent,&n)==C::Stale&&n==0);
 assert(get(candidate,"logical")=="one"&&candidate.get(candidate.viewRevision(),"remove",intent,sizeof intent,&n)==C::Missing);
 assert(disk.root==root&&core.revision()==revision&&get(core,"offline")=="two"&&nativeIntents(core)==originalIntents);
 firstBatch.viewRevision=candidate.viewRevision();assert(candidate.stage(firstBatch)==C::Conflict);
 C::Mutation second{"logical",C::Put,"two",3};assert(candidate.stage(batch(candidate,2,"save-two",&second,1))==C::Ok);
 assert(candidate.stage(batch(candidate,3,"same-value-save"))==C::Ok&&candidate.resolvedCount()==3);
 assert(candidate.finish(candidate.viewRevision(),candidate.frontier())==C::Ok&&!candidate.held()&&candidate.outcome()==C::Ok);
 assert(get(core,"logical")=="two"&&get(core,"restored")=="newer"&&core.pendingCount()==3&&nativeIntents(core)==originalIntents&&core.identity().generation==900);
 assert(candidate.abandon()==C::Ok&&candidate.stage(firstBatch)==C::Stale);
 C reboot(6,disk.backend(),coreLimits(),coreAlloc.api());assert(reboot.load()==C::Ok&&nativeIntents(reboot)==originalIntents&&get(reboot,"logical")=="two");
 puts("native receiver: copied remote map, ordered exact intents, atomic generic PUT/DELETE plus zero-mutation markers, unchanged native reads until finish and retained original intents after reboot PASS");
}
void refusals(){
 Disk disk;Alloc a,ra;ra.verifyWipe=true;C core(6,disk.backend(),coreLimits(),a.api());assert(core.initialize(nullptr,0,{})==C::Ok);assert(put(core,"local","kept","one")==C::Ok);assert(put(core,"local","kept","two")==C::Ok);
 Bytes map;append(map,"remote","r",1);const auto root=disk.root;R candidate(core,6,receiverLimits(),ra.api());
 assert(candidate.begin(remote(map,1,identity(900),1))==C::Invalid);
 {R wrong(core,1,receiverLimits(),ra.api());assert(wrong.begin(remote(map,1,identity(900),1))==C::Conflict);}
 auto unknown=remote(map,1);unknown.identity={};assert(candidate.begin(unknown)==C::Invalid);
 auto malformed=map;malformed[16]=250;assert(candidate.begin(remote(malformed,1))==C::Invalid);
 auto duplicate=map;duplicate.insert(duplicate.end(),map.begin(),map.end());assert(candidate.begin(remote(duplicate,2))==C::Invalid);
 assert(candidate.begin(remote(map,1))==C::Ok);assert(candidate.begin(remote(map,1))==C::Busy);
 const auto start=candidate.viewRevision();auto good=batch(candidate,1,"one");
 auto wrong=good;wrong.namespaceId=1;assert(candidate.stage(wrong)==C::Conflict);
 wrong=good;++wrong.viewRevision;assert(candidate.stage(wrong)==C::Stale);
 wrong=good;++wrong.pendingFrontier;assert(candidate.stage(wrong)==C::Stale);
 wrong=good;wrong.originalIntentId=2;wrong.originalIntent="two";assert(candidate.stage(wrong)==C::Conflict);
 wrong=good;wrong.originalIntentId=0;assert(candidate.stage(wrong)==C::Conflict);
 wrong=good;wrong.originalIntent="bad";assert(candidate.stage(wrong)==C::Conflict);
 wrong=good;wrong.originalIntentId=999;assert(candidate.stage(wrong)==C::Conflict);
 C::Mutation invalid[]={{"same",C::Put,"a",1},{"same",C::Delete,nullptr,0}};
 wrong=good;wrong.mutations=invalid;wrong.count=2;assert(candidate.stage(wrong)==C::Invalid);
 wrong=good;wrong.count=UINT32_MAX;assert(candidate.stage(wrong)==C::NoSpace);
 assert(candidate.resolvedCount()==0&&candidate.viewRevision()==start&&disk.root==root);
 assert(candidate.stage(good)==C::Ok);assert(candidate.abandon()==C::Ok&&!candidate.held()&&disk.root==root);
 assert(candidate.begin(remote(map,1))==C::Ok&&candidate.viewRevision()!=start&&candidate.resolvedCount()==0);
 assert(candidate.stage(good)==C::Stale);assert(put(core,"local","newer","concurrent")==C::Ok);
 auto fresh=batch(candidate,1,"one");assert(candidate.stage(fresh)==C::Stale);assert(candidate.finish(candidate.viewRevision(),candidate.frontier())==C::Stale);
 assert(candidate.abandon()==C::Ok&&get(core,"local")=="newer"&&core.pendingCount()==3);
 // A same-generation divergent digest, foreign lineage and rollback are refused.
 Disk knownDisk;C known(6,knownDisk.backend(),coreLimits(),a.api());assert(known.initialize(nullptr,0,identity(50))==C::Ok);R held(known,6,receiverLimits(),ra.api());
 assert(held.begin(remote(map,1,identity(49)))==C::Conflict);assert(held.begin(remote(map,1,identity(100,2)))==C::Conflict);
 auto divergent=identity(50);divergent.digest[1]=1;assert(held.begin(remote(map,1,divergent))==C::Conflict);
 assert(held.begin(remote(map,1,identity(50)))==C::Ok&&held.finish(held.viewRevision(),held.frontier())==C::Ok);
 puts("native receiver: bound namespace, malformed map, unknown/out-of-order/duplicate ID, exact bytes, view/frontier CAS, abandonment, concurrent edit and identity refusals PASS");
}
void limitsAndAllocations(){
 Disk disk;Alloc a,ra;ra.verifyWipe=true;C core(6,disk.backend(),coreLimits(),a.api());assert(core.initialize(nullptr,0,{})==C::Ok);assert(put(core,"local","kept","one")==C::Ok);const auto root=disk.root;
 Bytes map;append(map,"remote","x",1);
 for(unsigned failure=1;failure<=2;++failure){R c(core,6,receiverLimits(),ra.api());ra.fail=ra.calls+failure;assert(c.begin(remote(map,1))==C::NoMemory&&!c.held()&&disk.root==root);ra.fail=0;}
 {R c(core,6,receiverLimits(),ra.api());a.fail=a.calls+1;assert(c.begin(remote(map,1))==C::NoMemory&&!c.held());a.fail=0;}
 {auto limits=receiverLimits();limits.pendingBytes=14;R c(core,6,limits,ra.api());assert(c.begin(remote(map,1))==C::NoSpace);}
 {auto limits=receiverLimits();limits.intentBytes=2;R c(core,6,limits,ra.api());assert(c.begin(remote(map,1))==C::Invalid);}
 // Native image cap includes original group mutation bytes, not just intents.
 {auto limits=receiverLimits(300);R c(core,6,limits,ra.api());std::string value(80,'x');Bytes big;append(big,"remote",value.data(),value.size());assert(c.begin(remote(big,1))==C::NoSpace);}
 {auto limits=receiverLimits(300);R c(core,6,limits,ra.api());assert(c.begin(remote(map,1))==C::Ok);std::string value(80,'x');C::Mutation m{"remote",C::Put,value.data(),uint32_t(value.size())};assert(c.stage(batch(c,1,"one",&m,1))==C::NoSpace&&c.resolvedCount()==0&&get(c,"remote")=="x");}
 {auto limits=receiverLimits();limits.batchBytes=34;R c(core,6,limits,ra.api());assert(c.begin(remote(map,1))==C::Ok);assert(c.stage(batch(c,1,"one"))==C::NoSpace&&c.resolvedCount()==0);}
 {R c(core,6,receiverLimits(),ra.api());assert(c.begin(remote(map,1))==C::Ok);C::Mutation m{"remote",C::Put,"new",3};auto b=batch(c,1,"one",&m,1);ra.fail=ra.calls+1;assert(c.stage(b)==C::NoMemory&&c.resolvedCount()==0&&get(c,"remote")=="x");ra.fail=0;
  assert(c.stage(b)==C::Ok);
  for(unsigned failure=1;failure<=2;++failure){ra.fail=ra.calls+failure;assert(c.finish(c.viewRevision(),c.frontier())==C::NoMemory&&disk.root==root&&c.held());ra.fail=0;}
  a.fail=a.calls+1;assert(c.finish(c.viewRevision(),c.frontier())==C::NoMemory&&disk.root==root&&c.held());a.fail=0;
  disk.capacity=0;assert(c.finish(c.viewRevision(),c.frontier())==C::NoSpace&&disk.root==root&&c.held());disk.capacity=16*1024*1024;
  assert(c.finish(c.viewRevision(),c.frontier())==C::Ok&&get(core,"remote")=="new");
 }
 puts("native receiver: snapshot/pending/map/descriptor/core allocation failures, byte budgets, retained full-group capacity and wiping custom allocator PASS");
}
void scaleAndReboot(){
 Disk disk;Alloc a,ra;C core(6,disk.backend(),coreLimits(),a.api());assert(core.initialize(nullptr,0,{})==C::Ok);assert(put(core,"local","kept","one")==C::Ok);
 Bytes map;for(unsigned i=0;i<1025;++i){char key[16];snprintf(key,sizeof key,"opaque%04u",i);append(map,key,"v",1);}const auto root=disk.root;
 {R c(core,6,receiverLimits(),ra.api());assert(c.begin(remote(map,1025))==C::Ok&&c.count()==1025);assert(c.stage(batch(c,1,"one"))==C::Ok);assert(disk.root==root);}
 {C reboot(6,disk.backend(),coreLimits(),a.api());assert(reboot.load()==C::Ok&&reboot.pendingCount()==1&&get(reboot,"local")=="kept");R c(reboot,6,receiverLimits(),ra.api());assert(c.begin(remote(map,1025))==C::Ok&&c.resolvedCount()==0);assert(c.stage(batch(c,1,"one"))==C::Ok);assert(c.finish(c.viewRevision(),c.frontier())==C::Ok&&get(reboot,"opaque1024")=="v");}
 puts("native receiver: 1025 opaque keys, no key-count cap, discard/re-resolve RAM markers after reboot PASS");
}
void reentryAndFinalStale(){
 Disk disk;Alloc a,ra;C core(6,disk.backend(),coreLimits(),a.api());assert(core.initialize(nullptr,0,{})==C::Ok);assert(put(core,"local","one","first")==C::Ok);
 Bytes map;append(map,"remote","old",3);R c(core,6,receiverLimits(),ra.api());
 struct Probe {R*candidate;R::Remote source;unsigned calls=0;} probe{&c,remote(map,1)};
 ra.probeContext=&probe;ra.probe=[](void*p){auto&probe=*static_cast<Probe*>(p);auto&candidate=*probe.candidate;++probe.calls;
  assert(candidate.abandon()==C::Busy);assert(candidate.begin(probe.source)==C::Busy);assert(candidate.stage({})==C::Busy);assert(candidate.finish(0,0)==C::Busy);
  char out[1];uint32_t size=0;uint64_t id=0;assert(candidate.get(0,"remote",out,sizeof out,&size)==C::Busy);assert(candidate.pending(0,0,&id,out,sizeof out,&size)==C::Busy);
 };
 assert(c.begin(remote(map,1))==C::Ok);const auto initialCalls=probe.calls;assert(c.stage(batch(c,1,"first"))==C::Ok&&probe.calls>initialCalls);
 // Even after the full ordered prefix is resolved, a new native intent wins.
 assert(put(core,"local","newer","second")==C::Ok);const auto root=disk.root;assert(c.finish(c.viewRevision(),c.frontier())==C::Stale&&disk.root==root);
 assert(c.abandon()==C::Ok&&get(core,"local")=="newer"&&core.pendingCount()==2);
 assert(c.begin(remote(map,1))==C::Ok);assert(c.stage(batch(c,1,"first"))==C::Ok);assert(c.stage(batch(c,2,"second"))==C::Ok);
 assert(c.finish(c.viewRevision(),c.frontier())==C::Ok&&probe.calls>10);ra.probe=nullptr;
 puts("native receiver: allocator/release callback reentry refused, complete-resolution concurrent edit prevents final root publication PASS");
}
void ticketAndRetry(){
 Disk disk;Alloc a,ra;C core(6,disk.backend(),coreLimits(),a.api());assert(core.initialize(nullptr,0,{})==C::Ok);assert(put(core,"local","kept","one")==C::Ok);
 Bytes map;append(map,"remote","new",3);const auto root=disk.root,original=nativeIntents(core);C::Ticket exportTicket;assert(core.capture(&exportTicket)==C::Ok);
 // begin captures a new core ticket even when later admission fails. Rebase and
 // asynchronous transport ticket ownership therefore MUST be mutually exclusive.
 {auto small=receiverLimits();small.pendingBytes=1;R refused(core,6,small,ra.api());assert(refused.begin(remote(map,1))==C::NoSpace);assert(core.acknowledge(exportTicket,identity(1))==C::Stale&&disk.root==root&&nativeIntents(core)==original);}
 R c(core,6,receiverLimits(),ra.api());assert(c.begin(remote(map,1))==C::Ok);assert(c.stage(batch(c,1,"one"))==C::Ok);
 disk.fault(1,Disk::Before);assert(c.finish(c.viewRevision(),c.frontier())==C::Io&&c.held()&&core.ready()&&disk.root==root);
 disk.fault(0,Disk::Before);assert(c.finish(c.viewRevision(),c.frontier())==C::Ok&&get(core,"remote")=="new"&&nativeIntents(core)==original);
 puts("native receiver: exclusive rebase/export ticket contract and safe explicit retry after pre-root failure PASS");
}
void uncertainRecoveryAndMembership(){
 Bytes map;append(map,"remote_a","new",3);append(map,"remote_b","new",3);
 // Small map root replace/readback are backend calls 5/6 after reserve,
 // write, seal and object read. Every old/new ambiguous outcome is reloaded.
 for(unsigned mode=0;mode<3;++mode)for(unsigned cut:{5u,6u}){
  Disk disk;Alloc a,ra;C core(6,disk.backend(),coreLimits(),a.api());assert(core.initialize(nullptr,0,{})==C::Ok);assert(put(core,"local","old","one")==C::Ok);
  const auto original=nativeIntents(core);R c(core,6,receiverLimits(),ra.api());assert(c.begin(remote(map,2))==C::Ok);assert(c.stage(batch(c,1,"one"))==C::Ok);const auto old=batch(c,1,"one");
  disk.fault(cut,Disk::Mode(mode));assert(c.finish(c.viewRevision(),c.frontier())==C::Uncertain&&!c.held()&&c.abandon()==C::Uncertain);
  disk.fault(0,Disk::Before);assert(core.load()==C::Ok&&nativeIntents(core)==original);
  uint8_t bytes[8];uint32_t n=0;if(core.get("remote_a",bytes,sizeof bytes,&n)==C::Ok){assert(get(core,"remote_b")=="new");assert(core.get("local",bytes,sizeof bytes,&n)==C::Missing);}else{assert(get(core,"local")=="old");assert(core.get("remote_b",bytes,sizeof bytes,&n)==C::Missing);}
  assert(c.stage(old)==C::Stale);assert(c.begin(remote(map,2))==C::Ok&&c.viewRevision()>old.viewRevision);assert(c.stage(old)==C::Stale);
  assert(c.stage(batch(c,1,"one"))==C::Ok&&c.finish(c.viewRevision(),c.frontier())==C::Ok&&nativeIntents(core)==original&&get(core,"remote_b")=="new");
 }
 {Disk disk;Alloc a,ra;C core(6,disk.backend(),coreLimits(),a.api());assert(core.initialize(nullptr,0,{})==C::Ok);assert(put(core,"local","first","same-intent")==C::Ok);C::Ticket t;assert(core.capture(&t)==C::Ok);assert(put(core,"local","second","same-intent")==C::Ok);assert(core.acknowledge(t,identity(800))==C::Ok&&core.pendingCount()==1);
  R c(core,6,receiverLimits(),ra.api());assert(c.begin(remote(map,2))==C::Ok&&c.frontier()==2);
  assert(c.stage(batch(c,1,"same-intent"))==C::Conflict&&c.resolvedCount()==0);assert(c.stage(batch(c,2,"same-intent"))==C::Ok&&c.finish(c.viewRevision(),c.frontier())==C::Ok);
 }
 puts("native receiver: six same-receiver uncertain reload/re-resolve cases reject old views; acknowledged ID below frontier is not pending membership PASS");
}
void retainedRootCustody(){
 using J=RiscExperimental::NativeReplicaJob;
 for(unsigned mode=1;mode<=3;++mode){Disk disk;Alloc a,ra;C core(6,disk.backend(),coreLimits(),a.api());assert(core.initialize(nullptr,0,{})==C::Ok);assert(put(core,"local","old","one")==C::Ok);
  Bytes map;append(map,"remote_a","new",3);append(map,"remote_b","new",3);R c(core,6,receiverLimits(),ra.api());assert(c.begin(remote(map,2))==C::Ok);assert(c.stage(batch(c,1,"one"))==C::Ok);
  J job([](void*p,uint32_t,const void*,uint32_t)->J::Result{auto&c=*static_cast<R*>(p);return {c.finish(c.viewRevision(),c.frontier()),0,0};},&c);uint64_t token=0;assert(job.begin(1,nullptr,0,&token)==RISC_STORAGE_WORK_OK);disk.retainedRootMode=mode;assert(job.workerStep());
  J::Result result{};assert(job.collect(token,&result)==RISC_STORAGE_WORK_OK&&result.status==C::Retained&&!job.exitSafe());assert(!core.ready()&&!c.held()&&c.outcome()==C::Retained);
  const auto calls=disk.calls;assert(c.abandon()==C::Retained&&c.begin(remote(map,2))==C::Retained&&c.stage({})==C::Retained&&c.finish(0,0)==C::Retained);assert(job.begin(1,nullptr,0,&token)==RISC_STORAGE_WORK_RETAINED&&disk.calls==calls);
  char out[16];uint32_t n=0;assert(core.get("local",out,sizeof out,&n)==C::Uncertain);
  // Simulate a new backend/process after external custody recovery. The old
  // receiver/job remain latched; neither is used as permission to release it.
  disk.retainedRootMode=0;C reboot(6,disk.backend(),coreLimits(),a.api());assert(reboot.load()==C::Ok&&reboot.pendingCount()==1);
  if(mode==1){assert(get(reboot,"local")=="old"&&reboot.get("remote_a",out,sizeof out,&n)==C::Missing);}else{assert(get(reboot,"remote_a")=="new"&&get(reboot,"remote_b")=="new");assert(reboot.get("local",out,sizeof out,&n)==C::Missing);}
  uint64_t id=0;assert(reboot.pending(0,&id,out,sizeof out,&n)==C::Ok&&id==1&&std::string(out,n)=="one");assert(!job.exitSafe()&&c.abandon()==C::Retained);
 }
 puts("native receiver: retained root replacement before/after and retained root readback preserve uncertain native state plus receiver/job custody PASS");
}
void finishFaults(){
 Disk source;{Alloc a;C c(6,source.backend(),coreLimits(),a.api());assert(c.initialize(nullptr,0,{})==C::Ok);assert(put(c,"old_a","old","original")==C::Ok);assert(put(c,"old_b","old","second")==C::Ok);}
 const std::string payload(2048,'x');Bytes map;append(map,"new_a",payload.data(),payload.size());append(map,"new_b",payload.data(),payload.size());append(map,"new_c",payload.data(),payload.size());
 auto prepare=[&](R&c){assert(c.begin(remote(map,3))==C::Ok);assert(c.stage(batch(c,1,"original"))==C::Ok);assert(c.stage(batch(c,2,"second"))==C::Ok);};
 unsigned calls=0;{Disk d=source;Alloc a,ra;C core(6,d.backend(),coreLimits(),a.api());assert(core.load()==C::Ok);R c(core,6,receiverLimits(),ra.api());prepare(c);d.fault(0,Disk::Before);assert(c.finish(c.viewRevision(),c.frontier())==C::Ok);calls=d.calls;}
 unsigned cases=0;
 for(unsigned power=0;power<2;++power)for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=calls;++cut){Disk d=source;Alloc a,ra;
  try{C core(6,d.backend(),coreLimits(),a.api());assert(core.load()==C::Ok);const auto original=nativeIntents(core);R c(core,6,receiverLimits(),ra.api());prepare(c);d.fault(cut,Disk::Mode(mode),power!=0);int rc=c.finish(c.viewRevision(),c.frontier());assert(rc==C::Ok||rc==C::Io||rc==C::Uncertain);if(rc==C::Uncertain){assert(!c.held()&&c.outcome()==C::Uncertain&&c.abandon()==C::Uncertain&&!core.ready());}else assert(nativeIntents(core)==original);
  }catch(const Disk::PowerCut&){a.discardVolatileAfterPowerCut();ra.discardVolatileAfterPowerCut();}
  d.fault(0,Disk::Before);{C reboot(6,d.backend(),coreLimits(),a.api());assert(reboot.load()==C::Ok&&reboot.pendingCount()==2);uint32_t n=0;char out[16];const int found=reboot.get("new_a",out,sizeof out,&n);const bool installed=found==C::Ok||found==C::BufferSmall;
   if(installed){assert(get(reboot,"new_a")==payload&&get(reboot,"new_b")==payload&&get(reboot,"new_c")==payload&&reboot.identity().generation==900);assert(reboot.get("old_a",out,sizeof out,&n)==C::Missing&&reboot.get("old_b",out,sizeof out,&n)==C::Missing);}else{assert(get(reboot,"old_a")=="old"&&get(reboot,"old_b")=="old"&&!reboot.identity().known);assert(reboot.get("new_b",out,sizeof out,&n)==C::Missing&&reboot.get("new_c",out,sizeof out,&n)==C::Missing);}
   uint64_t id=0;assert(reboot.pending(0,&id,out,sizeof out,&n)==C::Ok&&id==1&&std::string(out,n)=="original");assert(reboot.pending(1,&id,out,sizeof out,&n)==C::Ok&&id==2&&std::string(out,n)=="second");
  }++cases;
 }
 printf("native receiver: %u finish before/after/torn returned-failure and immediate-power-cut points preserve complete old/new roots and every original intent PASS\n",cases);
}
int main(){basics();refusals();limitsAndAllocations();scaleAndReboot();reentryAndFinalStale();ticketAndRetry();uncertainRecoveryAndMembership();retainedRootCustody();finishFaults();puts("DISCONNECTED RECEIVER: RAM markers are not durable or SD acknowledgements; no production/SDK/UI/.90 activation or hardware qualification");}
