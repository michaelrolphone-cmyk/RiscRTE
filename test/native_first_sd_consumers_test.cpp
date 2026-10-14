#define NATIVE_SD_CONSUMERS_NO_MAIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "native_sd_consumers_test.cpp"
#pragma GCC diagnostic pop
#include "experimental/native_replica/CheckedFirstSdCheckpoint.h"
#include "experimental/native_replica/NativeBaselineAdmission.h"
#include <memory>
#ifdef NATIVE_FIRST_CHECKED_STORE
// The lower custody fixture uses N for NativeSdCompletion.
#define N CheckedMetadataAlias
#include "native_replica_checked_fixture.h"
#undef N
namespace Nf=NativeFixture;
#endif
using F=RiscExperimental::CheckedFirstSdCheckpoint;
extern "C" {void first_capacity_budget(void);unsigned first_capacity_elapsed(void);void first_capacity_limit(int);void first_capacity_epoch_change(void);void first_capacity_receipt_change(void);void first_capacity_disable(void);unsigned first_capacity_scans(void);unsigned first_capacity_reads(void);unsigned first_capacity_writes(void);void first_seed_begin(unsigned);void first_trace_enable(unsigned);unsigned first_collected(void);unsigned first_fault_count(void);unsigned first_fault_mutations(void);}
static void role(Task next){task=next;
#ifdef NATIVE_FIRST_CHECKED_STORE
 Nf::onWorker=next==Task::NativeSettings;
#endif
}
struct NativeCounters {
#ifdef NATIVE_FIRST_CHECKED_STORE
 unsigned calls=Nf::nvsTotal+Nf::fsTotal;
 void unchanged()const{assert(calls==Nf::nvsTotal+Nf::fsTotal);}
#else
 unsigned calls=nativeOps;
 void unchanged()const{assert(calls==nativeOps);}
#endif
};
static bool firstOwnerHeld=true;
static F::Guard firstGuard(){auto result=guard();result.held=[](void*){return lease&&firstOwnerHeld;};return result;}
struct FirstAuthority {
 unsigned randoms=0;bool stopped=true,space=true,entropy=true,retain=false;
 F::Authority api(){return {this,[](void*p){assert(task==Task::Owner);auto&s=*static_cast<FirstAuthority*>(p);if(s.retain){fixture_retain();lease=false;return false;}return s.stopped;},[](void*p,uint8_t out[16]){assert(task==Task::NativeSettings);auto&s=*static_cast<FirstAuthority*>(p);memset(out,0,16);out[0]=uint8_t(++s.randoms+64);return s.entropy;},[](void*p,uint32_t,uint32_t){assert(task==Task::Owner);return static_cast<FirstAuthority*>(p)->space;}};}
};
static void importedFirst(C&core,Hash&hash,bool empty){
 using B=RiscExperimental::NativeBaselineAdmission;
 struct Source{bool empty;};Source source{empty};auto h=hash.api();
 B::Sources sources{&source,[](void*){return int(C::Ok);},nullptr,
 [](void*p,uint32_t ns,const B::Selection&s,const B::Rows&rows,B::Witness*w){*w={ns,B::RootState::NeverInitialized,s.kind,s.revision,{}};memcpy(w->fingerprint,s.fingerprint,32);return static_cast<Source*>(p)->empty?int(C::Ok):rows.append(rows.context,"legacy","valuable",8);},
 [](void*,uint32_t,const B::Witness&){return int(C::Ok);}};
 B b(&source,core,{nullptr,[](void*){return true;},[](void*){return uint64_t(1);}},{h.context,h.begin,h.update,h.finish},sources);
 B::Selection selected;selected.kind=1;selected.revision=7;selected.fingerprint[0]=42;C::Initialization admission;
 assert(b.admitImported(selected,&admission)==C::Ok&&core.initializeImportedBaseline(admission)==C::Ok);
}
static void firstEdit(C&core,const char*text){role(Task::NativeSettings);C::Mutation m{"edited",C::Put,text,uint32_t(strlen(text))};assert(core.apply(core.revision(),&m,1,"synthetic-intent",16)==C::Ok);role(Task::Owner);}
static int firstPrepare(F&f,A&a,bool before=false,bool after=false){
 assert(f.prepareNative(a)==C::Ok&&a.release()==A::Context);if(before)f.cancel();Counters sd;NativeCounters native;f.step();assert(f.collectPreparation()==C::Busy&&!f.preparationWorkerStep());native.unchanged();
 role(Task::ProviderIo);assert(!f.preparationWorkerStep());role(Task::NativeSettings);assert(f.preparationWorkerStep());sd.unchanged();assert(f.collectPreparation()==C::Busy);role(Task::Owner);if(after)f.cancel();NativeCounters collected;const int rc=f.collectPreparation();collected.unchanged();return rc;
}
template<class Job>static void firstDrive(Job&f,A&a,C&core,const risc_storage_volume_api_v1*v){
 unsigned rounds=0;bool probe=true;
 while(f.state()==Job::State::Running){Counters sd;role(Task::NativeSettings);f.step();sd.unchanged();role(Task::Other);f.step();sd.unchanged();role(Task::Owner);NativeCounters native;f.step();native.unchanged();if(probe&&a.hasSession()){assert(fixture_observe()==RISC_STORAGE_STATE_UNAVAILABLE);strictRaw(core,v);role(Task::Owner);probe=false;}if(!worker.idle())step();assert(++rounds<4000);}
 assert(consumers_refreshes()==1);
}
static int armFirst(F&f,A&a,NativeStore*store,const char*mode){
 assert(f.state()==F::State::Admitted&&f.prepareNative()==C::Invalid&&a.release()==A::Context);
 const bool polling=strstr(mode,"poll"),cancel=strstr(mode,"arm-cancel"),running=strstr(mode,"arm-running-cancel");
 NativeCounters before;
 if(polling){const unsigned reads=fixture_reads(),writes=fixture_writes(),submits=consumers_submissions();assert(store);if(strstr(mode,"busy"))store->busyReserves=2;std::atomic<bool>stop{false};std::atomic<unsigned>polls{0},executions{0};std::thread thread([&]{task=Task::NativeSettings;while(!stop.load()){if(f.nativeArmWorkerStep())++executions;++polls;std::this_thread::yield();}});while(polls<100)std::this_thread::yield();assert(f.issueNativeArm()==C::Ok);int rc=C::Busy;while(rc==C::Busy){rc=f.collectNativeArm();std::this_thread::yield();}const unsigned n=polls;while(polls<n+100)std::this_thread::yield();stop=true;thread.join();assert(executions==(strstr(mode,"busy")?3u:1u));assert(reads==fixture_reads()&&writes==fixture_writes()&&submits==consumers_submissions());return rc;}
 assert(f.issueNativeArm()==C::Ok&&a.receiptPinned());Counters sd;assert(!f.nativeArmWorkerStep()&&f.collectNativeArm()==C::Busy);before.unchanged();
 if(running){assert(store);store->blockCommit=true;std::thread t([&]{task=Task::NativeSettings;assert(f.nativeArmWorkerStep());});{std::unique_lock<std::mutex>lock(gate);cv.wait(lock,[]{return enteredNative;});}f.cancel();assert(!f.exitSafe());{std::lock_guard<std::mutex>lock(gate);allowNative=true;}cv.notify_all();t.join();store->blockCommit=false;}
 else{if(cancel)f.cancel();if(store&&strstr(mode,"arm-native-retained"))store->retainSeal=true;role(Task::NativeSettings);assert(f.nativeArmWorkerStep());role(Task::Owner);}
 sd.unchanged();if(cancel)before.unchanged();if(strstr(mode,"arm-late-retained")){fixture_retain();lease=false;}if(strstr(mode,"arm-unavailable")){snapshotFaultAt=snapshots+1;snapshotUnavailable=true;}NativeCounters collecting;const int rc=f.collectNativeArm();collecting.unchanged();return rc;
}
static int finishFirst(F&f,A&a,NativeStore*store,const char*mode){
 assert(f.state()==F::State::Verified&&f.acknowledgeNative()==C::Invalid);
 if(store)return complete(f,a,*store,mode);
 NativeCounters native;assert(f.issueNativeCompletion()==C::Ok&&a.receiptPinned()&&a.release()==A::Context);Counters sd;assert(!f.nativeCompletionWorkerStep()&&f.collectNativeCompletion()==C::Busy);native.unchanged();role(Task::NativeSettings);assert(f.nativeCompletionWorkerStep());role(Task::Owner);sd.unchanged();NativeCounters collecting;const int rc=f.collectNativeCompletion();collecting.unchanged();return rc;
}

#ifdef NATIVE_FIRST_CHECKED_STORE
template<class Job>static int checkedPreparation(Job&job){Counters sd;NativeCounters native;assert(!job.preparationWorkerStep()&&job.collectPreparation()==C::Busy);native.unchanged();role(Task::NativeSettings);assert(job.preparationWorkerStep());role(Task::Owner);sd.unchanged();NativeCounters owner;const int rc=job.collectPreparation();owner.unchanged();return rc;}
template<class Job>static void checkedCompletion(Job&job,A&a){NativeCounters native;assert(job.issueNativeCompletion()==C::Ok&&a.receiptPinned());Counters sd;assert(!job.nativeCompletionWorkerStep()&&job.collectNativeCompletion()==C::Busy);native.unchanged();role(Task::NativeSettings);assert(job.nativeCompletionWorkerStep());role(Task::Owner);sd.unchanged();NativeCounters owner;assert(job.collectNativeCompletion()==C::Ok&&job.exitSafe());owner.unchanged();}
static void storeComposition(Nf::Fixture&checked,Hash&hash,A&a,const risc_storage_volume_api_v1*v,const char*mode){
 C&core=checked.core;uint8_t incarnation[16]={42};T writer(hash.api(),guard());assert(writer.prepareNative(core,a,true,incarnation)==C::Ok&&checkedPreparation(writer)==C::Ok);
 if(strstr(mode,"retire")){writer.cancel();firstDrive(writer,a,core,v);assert(writer.exitSafe());}
 else{firstDrive(writer,a,core,v);assert(writer.state()==T::State::Verified);if(strstr(mode,"recovery"))writer.cancel();}
 assert(core.exportPrepared());firstEdit(core,"newer");
 if(strstr(mode,"recovery")||strstr(mode,"retire")){E recovery(core,hash.api(),guard(),quiet());assert(recovery.prepareNative(a)==C::Ok&&checkedPreparation(recovery)==C::Ok);firstDrive(recovery,a,core,v);assert(recovery.state()==E::State::Verified&&recovery.decision()==(strstr(mode,"retire")?E::Decision::RetireUnpublished:E::Decision::AcknowledgePrefix));checkedCompletion(recovery,a);}
 else checkedCompletion(writer,a);
 const bool retired=strstr(mode,"retire");assert(!core.exportPrepared()&&core.pendingCount()==(retired?2u:1u)&&core.identity().generation==(retired?7u:8u)&&value(core)=="newer");
 role(Task::NativeSettings);checked.store.finishOperation();{Nf::Fixture reboot(3);assert(reboot.core.load()==C::Ok&&!reboot.core.exportPrepared()&&reboot.core.pendingCount()==(retired?2u:1u)&&reboot.core.identity().generation==(retired?7u:8u)&&value(reboot.core)=="newer");reboot.store.finishOperation();}role(Task::Owner);cleanup(a);printf("actual checked native store + X4/FatFs %s PASS\n",mode);
}
#endif
static void pathRejections(A&a){
 risc_storage_package_request_v1 q{};q.operation=RISC_STORAGE_PACKAGE_MKDIR;strcpy(q.path,"/RTECFG");assert(a.openChild()==C::Ok);Counters raw;assert(a.package(q)==A::Context);raw.unchanged();close(a);
 RiscExperimental::NativeSdSession session;assert(session.bind(a,RiscExperimental::NativeSdSession::Role::FirstCheckpoint,3)==C::Ok);uint64_t token=0;assert(session.begin(&token)==C::Ok);
 Counters before;for(const char*path:{"/","/RTECFG/00000004","/RTECFG/00000003/nested"}){q={};q.operation=RISC_STORAGE_PACKAGE_MKDIR;strcpy(q.path,path);assert(session.package(q,&token)==A::Context&&!token);}
 for(const char*path:{"/RTECFG/00000003/nr-orphan","/RTECFG/00000003/.replica.first","/RTECFG/00000003/replica.index","/RTECFG/00000003/settings.kv"}){q={};q.operation=RISC_STORAGE_PACKAGE_WRITE_SMALL;strcpy(q.path,path);q.size=1;assert(session.package(q,&token)==A::Context&&!token);risc_storage_async_request_v1 remove{};remove.operation=RISC_STORAGE_ASYNC_REMOVE_FILE;strcpy(remove.path,path);assert(session.submit(remove,&token)==A::Context&&!token);}
 q={};q.operation=RISC_STORAGE_PACKAGE_REPLACE;strcpy(q.path,"/RTECFG/00000003/.replica.first");strcpy(q.destination,"/RTECFG/00000003/replica.index");assert(session.package(q,&token)==A::Context&&!token);before.unchanged();
 for(unsigned i=0;i<20;++i){const int rc=session.end();if(rc==C::Ok)break;assert(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY);if(!worker.idle())step();assert(i<19);}assert(session.release()==C::Ok);cleanup(a);
}
int main(int argc,char**argv){assert(argc==3);const char*mode=argv[1];role(Task::Owner);const auto*v=fixture_start();
 if(!strncmp(mode,"seed-",5)){first_seed_begin(strstr(mode,"bare")?0:strstr(mode,"parent")?1:2);if(strstr(mode,"legacy"))seedLegacy();if(strstr(mode,"index"))seedReplica();if(strstr(mode,"orphan")){const uint8_t b=1;consumers_seed_file("/RTECFG/00000003/nr-orphan",&b,1);}consumers_seed_save(argv[2]);fixture_stop();return 0;}
 consumers_seed_load(argv[2]);const unsigned fault=!strncmp(mode,"io-",3)?unsigned(atoi(mode+3)):0;first_trace_enable(0);A a({3,55,7,71,v},controlledHooks());attempt=&a;bootstrap(a,v);first_trace_enable(fault?fault+first_collected():0);if(!strcmp(mode,"paths")){pathRejections(a);return 0;}Hash hash;NativeStore*memory=nullptr;
#ifdef NATIVE_FIRST_CHECKED_STORE
 Nf::rootPath=std::string(argv[2])+".native-"+mode;role(Task::NativeSettings);Nf::empty();Nf::Fixture checked(3);C&core=checked.core;
#else
 NativeStore native;memory=&native;C core(3,native.api(),{65512,4096,1024},C::heap());role(Task::NativeSettings);
#endif
 const bool unknown=strstr(mode,"unknown")||strstr(mode,"empty");const bool empty=strstr(mode,"empty");
 if(unknown)importedFirst(core,hash,empty);else{C::Value row{"base","vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv",32};assert(core.initialize(&row,1,mapIdentity(baseMap(),1))==C::Ok);}role(Task::Owner);if(!empty)firstEdit(core,"captured");const unsigned captured=core.pendingCount();
#ifdef NATIVE_FIRST_CHECKED_STORE
 role(Task::NativeSettings);checked.store.finishOperation();role(Task::Owner);
#endif
 #ifdef NATIVE_FIRST_CHECKED_STORE
 if(!strncmp(mode,"store-",6)){storeComposition(checked,hash,a,v,mode);return 0;}
#endif
 FirstAuthority authority;if(strstr(mode,"entropy"))authority.entropy=false;authority.space=false; /* Synthetic capacity callback cannot authorize native arm. */
 if(!strcmp(mode,"capacity-budget"))first_capacity_budget();
 if(!strncmp(mode,"capacity",8))first_capacity_limit(!strcmp(mode,"capacity")?-1000:!strcmp(mode,"capacity-short")?-1:!strcmp(mode,"capacity-exact")?0:1000);
 F first(core,hash.api(),firstGuard(),authority.api());
 if(!strcmp(mode,"native-owner-queue")){firstOwnerHeld=false;const unsigned reads=fixture_reads(),writes=fixture_writes(),submitted=consumers_submissions();NativeCounters native;assert(first.prepareNative(a)==C::Conflict&&first.exitSafe());assert(reads==fixture_reads()&&writes==fixture_writes()&&submitted==consumers_submissions());native.unchanged();firstOwnerHeld=true;cleanup(a);return 0;}
 if(!strcmp(mode,"capacity-missing")){first_capacity_disable();Counters before;assert(first.prepareNative(a)==C::Invalid);before.unchanged();cleanup(a);return 0;}
 if(strstr(mode,"quiet-queue")){authority.retain=true;assert(first.prepareNative(a)==C::Retained&&!first.exitSafe()&&!a.exitSafe());return 0;}
 const int prep=firstPrepare(first,a,strstr(mode,"cancel-prep"),strstr(mode,"cancel-after-prep"));
 if(strstr(mode,"cancel-prep")||strstr(mode,"cancel-after-prep")||strstr(mode,"entropy")){assert(prep!=C::Ok&&first.exitSafe()&&!core.exportPrepared()&&!consumers_mutations());cleanup(a);return 0;}assert(prep==C::Ok);
 if(!strcmp(mode,"capacity-cancel")||!strcmp(mode,"capacity-running-cancel")||!strcmp(mode,"capacity-cancel-retained")){
  first.step();first.step();assert(!worker.idle());
  if(strstr(mode,"running")){
   blockRead=true;std::thread thread([]{role(Task::ProviderIo);assert(worker.workerStep());});
   {std::unique_lock<std::mutex> lock(gate);cv.wait(lock,[]{return enteredRead;});}
   first.cancel();first.step();assert(!first.exitSafe()&&first.state()==F::State::Running);
   {std::lock_guard<std::mutex> lock(gate);allowRead=true;}cv.notify_all();thread.join();blockRead=false;
  }else first.cancel();
  if(strstr(mode,"retained"))fixture_adapter_retain();
  firstDrive(first,a,core,v);
  assert(!core.exportPrepared()&&!consumers_mutations());
  if(strstr(mode,"retained")){assert(first.state()==F::State::Retained&&!first.exitSafe());return 0;}
  assert(first.exitSafe());cleanup(a);return 0;
 }
 if(strstr(mode,"quiet-scan")){authority.retain=true;}firstDrive(first,a,core,v);
 if(!strcmp(mode,"capacity-budget")){assert(first.state()==F::State::Failed&&first.error()==C::Io&&first.exitSafe()&&!core.exportPrepared()&&!consumers_mutations()&&!first_capacity_writes());cleanup(a);return 0;}
 if(!strcmp(mode,"capacity")||!strcmp(mode,"capacity-short")){assert(first_capacity_elapsed()>0&&first_capacity_elapsed()<15000);assert(first.state()==F::State::Failed&&first.error()==C::NoSpace&&first.exitSafe()&&!core.exportPrepared()&&!consumers_mutations()&&first_capacity_reads()>0&&!first_capacity_writes());cleanup(a);return 0;}
 if(!strncmp(mode,"capacity",8)){assert(first_capacity_scans()==1&&first_capacity_reads()>0&&!first_capacity_writes());}
 if(!strcmp(mode,"capacity-epoch")||!strcmp(mode,"capacity-receipt")||!strcmp(mode,"capacity-lease")){
  if(strstr(mode,"epoch"))first_capacity_epoch_change();else if(strstr(mode,"receipt"))first_capacity_receipt_change();else lease=false;
  assert(first.issueNativeArm()!=C::Ok&&!core.exportPrepared()&&!consumers_mutations()&&first.exitSafe());if(lease)cleanup(a);return 0;
 }
 if(strstr(mode,"quiet-scan")){assert(first.state()==F::State::Retained&&!first.exitSafe()&&!a.exitSafe());return 0;}
 if(strstr(mode,"conflict")||first_fault_count()){assert(first.state()==F::State::Failed&&first.exitSafe()&&!core.exportPrepared()&&!consumers_mutations());cleanup(a);return 0;}
 assert(first.state()==F::State::Admitted&&!core.exportPrepared()&&!consumers_mutations()&&core.identity().known==!unknown);assert(first.issueNativeCompletion()!=C::Ok);
 if(!strcmp(mode,"native-owner-arm")||!strcmp(mode,"native-owner-retained")){firstOwnerHeld=false;NativeCounters native;const bool retained=strstr(mode,"retained");if(retained)fixture_adapter_retain();assert(first.issueNativeArm()==(retained?C::Retained:C::Conflict)&&!core.exportPrepared()&&!consumers_mutations());native.unchanged();if(retained){assert(!first.exitSafe());return 0;}assert(first.exitSafe());firstOwnerHeld=true;cleanup(a);return 0;}
 if(strstr(mode,"quiet-arm")){authority.retain=true;assert(first.issueNativeArm()==C::Retained&&!first.exitSafe()&&!a.exitSafe());return 0;}
 if(strstr(mode,"stale"))firstEdit(core,"stale");
 const int armed=armFirst(first,a,memory,mode);
 if(strstr(mode,"arm-native-retained")||strstr(mode,"arm-late-retained")||strstr(mode,"arm-unavailable")){assert(armed==C::Retained&&!first.exitSafe()&&!a.exitSafe()&&!consumers_mutations());return 0;}
 if(strstr(mode,"arm-cancel")||strstr(mode,"arm-running-cancel")||strstr(mode,"stale")){assert(first.exitSafe()&&!consumers_mutations());assert(core.exportPrepared()==bool(strstr(mode,"arm-running-cancel")));assert(armed==(strstr(mode,"arm-running-cancel")?C::Ok:C::Stale));cleanup(a);return 0;}
 assert(armed==C::Ok&&core.exportPrepared()&&core.identity().known==!unknown&&core.formatVersion()==4&&!consumers_mutations());C::ExportMarker marker;assert(core.preparedExport(&marker)==C::Ok&&marker.firstCreation());Bytes markerBytes(marker.encoded(),marker.encoded()+C::MarkerBytes);
 firstEdit(core,"newer");
#ifdef NATIVE_FIRST_CHECKED_STORE
 role(Task::NativeSettings);checked.store.finishOperation();role(Task::Owner);
#endif
 F*job=&first;std::unique_ptr<F> resumed;
 if(strstr(mode,"resume")){
  if(strstr(mode,"prefix")){unsigned rounds=0;while(!consumers_mutations()){NativeCounters before;first.step();before.unchanged();if(!worker.idle())step();assert(++rounds<2000);}assert(first.state()==F::State::Running);}
  first.cancel();firstDrive(first,a,core,v);assert(first.exitSafe());for(unsigned i=0;i<7;++i)firstEdit(core,"newer");const unsigned randoms=authority.randoms;
  if(strstr(mode,"reload")){role(Task::NativeSettings);
#ifdef NATIVE_FIRST_CHECKED_STORE
   checked.store.finishOperation();core.~C();new(&core)C(3,checked.store.backend(),S::limits(),C::heap());
#else
   core.~C();new(&core)C(3,memory->api(),{65512,4096,1024},C::heap());
#endif
   assert(core.load()==C::Ok&&core.exportPrepared()&&value(core)=="newer");role(Task::Owner);
  }
  resumed.reset(new F(core,hash.api(),firstGuard(),authority.api()));assert(firstPrepare(*resumed,a)==C::Ok&&authority.randoms==randoms);job=resumed.get();}
 firstDrive(*job,a,core,v);
 if(first_fault_count()){assert(job->state()==F::State::Failed&&job->error()==C::Io&&job->exitSafe()&&core.exportPrepared()&&consumers_mutations()==first_fault_mutations());cleanup(a);return 0;}
 assert(job->state()==F::State::Verified&&!job->exitSafe()&&a.release()==A::Context&&core.pendingCount()==captured+(strstr(mode,"resume")?8u:1u));assert(core.preparedExport(&marker)==C::Ok&&!memcmp(markerBytes.data(),marker.encoded(),markerBytes.size()));
 if(!strcmp(mode,"native-owner-ack")){firstOwnerHeld=false;NativeCounters native;assert(job->issueNativeCompletion()==C::Conflict&&job->exitSafe()&&core.exportPrepared());native.unchanged();firstOwnerHeld=true;cleanup(a);return 0;}
 if(strstr(mode,"quiet-complete")){authority.retain=true;assert(job->issueNativeCompletion()==C::Retained&&!job->exitSafe()&&!a.exitSafe());return 0;}
 const int rc=finishFirst(*job,a,memory,mode);
 if(strstr(mode,"final-release-epoch")){assert(rc==C::Conflict&&job->exitSafe()&&!a.exitSafe());return 0;}
 if(strstr(mode,"retained")||strstr(mode,"completion-unavailable")){assert(rc==C::Retained&&!job->exitSafe()&&!a.exitSafe());return 0;}
 if(strstr(mode,"cancel")&&!strstr(mode,"running-cancel")){assert(rc==C::Stale&&core.exportPrepared());}
 else{assert(rc==C::Ok&&!core.exportPrepared()&&core.identity().known&&core.identity().generation==(unknown?1u:8u)&&core.pendingCount()==(strstr(mode,"resume")?8u:1u)&&value(core)=="newer");}
 assert(job->exitSafe());
#ifdef NATIVE_FIRST_CHECKED_STORE
 role(Task::NativeSettings);checked.store.finishOperation();{Nf::Fixture reboot(3);assert(reboot.core.load()==C::Ok&&reboot.core.identity().known&&reboot.core.identity().generation==(unknown?1u:8u)&&reboot.core.pendingCount()==(strstr(mode,"resume")?8u:1u)&&value(reboot.core)=="newer");reboot.store.finishOperation();}role(Task::Owner);
#endif
 assert(!fault);cleanup(a);printf("typed first checkpoint %s PASS (refreshes=%u, sessions=%u, successful collections=%u)\n",mode,consumers_refreshes(),consumers_begins(),first_collected());
}
