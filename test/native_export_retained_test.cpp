// Focused late-provider-retention proof using the actual checked native adapter.
#include "native_rebase_roundtrip_fixture.h"
#include "experimental/native_replica/CheckedNativeExportRecovery.h"
#include "experimental/native_replica/CheckedLegacySettingsReader.h"
#include <functional>
using Recovery=RiscExperimental::CheckedNativeExportRecovery;
Recovery::Quiescence quietGuard(bool&quiet){return {&quiet,[](void*p){return *static_cast<bool*>(p);}};}
int startDurable(Nf::Fixture&f,Sd&sd,T&writer){
 const uint8_t incarnation[16]={'T','E','S','T','O','N','L','Y',1};
 return nativeCall(f,[&]{return writer.startDurable(f.core,sd.volume(),incarnation);});
}
void retainedWriter(){
 unsigned cases=0;
 for(bool durable:{false,true})for(bool lostLease:{false,true})for(bool cancel:{false,true}){
  resetProcess();Setup setup;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);Sd sd(setup.sd);Hash hash;T writer(hash.api(),sd.guard());
   assert((durable?startDurable(f,sd,writer):nativeCall(f,[&]{return writer.start(f.core,sd.volume());}))==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Verified);
   const auto root=nativeRoot(),pending=intents(f.core),map=mapOf(f.core);const unsigned nativeIo=Nf::nvsTotal+Nf::fsTotal,sdIo=sd.io;
   sd.retained=true;if(lostLease)sd.lease=false;
   if(cancel)writer.cancel();else assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Retained);
   assert(writer.state()==T::State::Retained&&writer.error()==C::Retained&&!writer.exitSafe());writer.cancel();writer.step();assert(writer.acknowledgeNative()==C::Stale&&writer.state()==T::State::Retained&&!writer.exitSafe());
   assert(nativeRoot()==root&&intents(f.core)==pending&&mapOf(f.core)==map&&f.core.exportPrepared()==durable&&nativeIo==Nf::nvsTotal+Nf::fsTotal&&sdIo==sd.io);
  }
  resetProcess();++cases;
 }
 printf("late retention: %u durable/volatile writer acknowledgement/cancellation cases preserve native map, all intents and custody even with lost lease PASS\n",cases);
}
void retainedRecovery(){
 unsigned cases=0;
 for(bool published:{false,true})for(unsigned lost=0;lost<4;++lost)for(bool cancel:{false,true}){
  resetProcess();Setup setup;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);Sd sd(setup.sd);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);
   if(published){drain(sd,writer);assert(writer.state()==T::State::Verified);}writer.cancel();drain(sd,writer);assert(writer.exitSafe()&&f.core.exportPrepared());
  }
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(setup.sd);Hash hash;bool quiet=true;Recovery recovery(f.core,hash.api(),sd.guard(),quietGuard(quiet));assert(nativeCall(f,[&]{return recovery.start(sd.volume());})==C::Ok);drain(sd,recovery);
   assert(recovery.state()==Recovery::State::Verified&&recovery.decision()==(published?Recovery::Decision::AcknowledgePrefix:Recovery::Decision::RetireUnpublished));
   const auto root=nativeRoot(),pending=intents(f.core),map=mapOf(f.core);const unsigned nativeIo=Nf::nvsTotal+Nf::fsTotal,sdIo=sd.io;
   sd.retained=true;if(lost&1)sd.lease=false;if(lost&2)quiet=false;
   if(cancel)recovery.cancel();else assert(nativeCall(f,[&]{return recovery.completeNative();})==C::Retained);
   assert(recovery.state()==Recovery::State::Retained&&recovery.error()==C::Retained&&!recovery.exitSafe());recovery.cancel();recovery.step();assert(recovery.completeNative()==C::Stale&&recovery.state()==Recovery::State::Retained&&!recovery.exitSafe());
   assert(nativeRoot()==root&&intents(f.core)==pending&&mapOf(f.core)==map&&f.core.exportPrepared()&&nativeIo==Nf::nvsTotal+Nf::fsTotal&&sdIo==sd.io);
  }
  resetProcess();++cases;
 }
 printf("late retention: %u target-prefix/old-base recovery completion/cancellation cases preserve selected marker and custody despite lost lease/quiescence PASS\n",cases);
}
void retainedTransfer(){
 for(bool published:{false,true}){
  resetProcess();Setup setup;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);Sd sd(setup.sd);Hash hash;T writer(hash.api(),sd.guard());assert(startDurable(f,sd,writer)==C::Ok);if(published)drain(sd,writer);writer.cancel();drain(sd,writer);assert(writer.exitSafe());}
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto root=nativeRoot(),pending=intents(f.core);const unsigned nativeIo=Nf::nvsTotal+Nf::fsTotal;Sd sd(setup.sd);Hash hash;bool quiet=true;
   // Checked end succeeds, then custody changes before the internal reader take.
   sd.download.base.base.end=[](void*p,uint64_t session){auto&s=Sd::self(p);int rc=risc_storage_async_end(&s.adapter,session);if(rc==RISC_STORAGE_ASYNC_OK){++s.ends;s.retained=true;s.lease=false;}return rc;};
   Recovery recovery(f.core,hash.api(),sd.guard(),quietGuard(quiet));assert(nativeCall(f,[&]{return recovery.start(sd.volume());})==C::Ok);drain(sd,recovery);
   assert(sd.ends&&recovery.state()==Recovery::State::Retained&&!recovery.exitSafe()&&recovery.error()==C::Retained&&recovery.decision()==Recovery::Decision::None);const unsigned io=sd.io;recovery.cancel();recovery.step();assert(recovery.completeNative()==C::Stale&&!recovery.exitSafe()&&sd.io==io);
   assert(nativeRoot()==root&&intents(f.core)==pending&&f.core.exportPrepared()&&nativeIo==Nf::nvsTotal+Nf::fsTotal);
  }
  resetProcess();
 }
 puts("late retention: target/base recovery transfer immediately after successful checked close keeps Retained despite simultaneous lost lease PASS");
}

using Legacy=RiscExperimental::CheckedLegacySettingsReader;
// Real copied adapter/worker. Only the terminal provider observation changes:
// an end OK fence publishes retained custody without rewriting its return code.
// This is a synthetic boundary fault, never an active-session READY wrapper.
struct TerminalSd:Sd {
 enum Mode {Success,ReadIo,QueuedCancel,CompletedCancel,RunningCancel,LeaseLoss,StrictUnavailable,Unavailable,ContentFailure,QuiescenceLoss};
 Mode mode;bool loseLease;unsigned endOk=0,postEndObserved=0,pendingEnd=0,ioResults=0;bool runningFired=false;
 std::function<void()> duringRead;
 size_t(*read)(void*,uint32_t,void*,size_t)=nullptr;
 static TerminalSd&own(void*p){return *static_cast<TerminalSd*>(p);}
 TerminalSd(Files&f,Mode m,bool lost):Sd(f),mode(m),loseLease(lost){
  auto&v=*const_cast<risc_storage_volume_api_v1*>(volume());read=v.file_read;
  v.file_read=[](void*p,uint32_t h,void*out,size_t n)->size_t{auto&s=own(p);if(s.mode==ReadIo){s.point("terminal-read-io");return 0;}if(s.mode==RunningCancel&&!s.runningFired){s.runningFired=true;s.duringRead();}return s.read(p,h,out,n);};
  download.base.base.begin=[](void*p,uint64_t*out){auto&s=own(p);const int rc=risc_storage_async_begin(&s.adapter,out);if(rc==RISC_STORAGE_ASYNC_OK&&s.mode==StrictUnavailable)s.retained=true;return rc;};
  download.base.base.collect=[](void*p,uint64_t session,uint64_t op,risc_storage_async_result_v1*out){auto&s=own(p);const int rc=risc_storage_async_collect(&s.adapter,session,op,out);if(rc==RISC_STORAGE_ASYNC_IO)++s.ioResults;return rc;};
  download.base.base.end=[](void*p,uint64_t session){auto&s=own(p);const int rc=risc_storage_async_end(&s.adapter,session);if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)++s.pendingEnd;if(rc==RISC_STORAGE_ASYNC_OK){assert(!s.adapter.session&&s.worker.idle());++s.endOk;s.retained=true;if(s.loseLease)s.lease=false;}return rc;};
  download.base.base.fs.state.observe=[](void*p){auto&s=own(p);if(s.endOk){assert(!s.adapter.session&&s.worker.idle());++s.postEndObserved;}if(s.mode==StrictUnavailable&&s.adapter.session)return int(RISC_STORAGE_STATE_UNAVAILABLE);return s.retained?int(RISC_STORAGE_STATE_RETAINED):s.available?int(RISC_STORAGE_STATE_READY):int(RISC_STORAGE_STATE_UNAVAILABLE);};
 }
};
static void noTerminalOutput(Reader&job){Reader::Candidate out;assert(job.take(&out)==C::Stale&&!out.valid());}
static void noTerminalOutput(Legacy&job){Legacy::Candidate out;assert(job.take(&out)==C::Stale&&!out.valid());}
static void noTerminalOutput(T&job){assert(job.acknowledgeNative()==C::Stale);}
static void noTerminalOutput(Recovery&job){assert(job.completeNative()==C::Stale&&job.decision()==Recovery::Decision::None);}
template<class Job> void terminalRun(TerminalSd&sd,Job&job,bool&quiet){
 sd.duringRead=[&]{job.cancel();for(unsigned i=0;i<3;++i){const unsigned io=sd.io;job.step();assert(job.state()==Job::State::Running&&!job.exitSafe()&&sd.io==io);}};
 if(sd.mode==TerminalSd::QueuedCancel||sd.mode==TerminalSd::CompletedCancel||sd.mode==TerminalSd::LeaseLoss||sd.mode==TerminalSd::Unavailable||sd.mode==TerminalSd::QuiescenceLoss){
  for(unsigned n=0;!sd.adapter.operation;++n){assert(n<100&&job.state()==Job::State::Running);poll(sd,job,false);}
  if(sd.mode==TerminalSd::CompletedCancel){poll(sd,job);assert(!sd.worker.idle());}
  if(sd.mode==TerminalSd::LeaseLoss)sd.lease=false;else if(sd.mode==TerminalSd::Unavailable)sd.available=false;else if(sd.mode==TerminalSd::QuiescenceLoss)quiet=false;else job.cancel();
 }
 drain(sd,job);
 assert(sd.endOk==1&&sd.postEndObserved&&job.state()==Job::State::Retained&&job.error()==C::Retained&&!job.exitSafe());
 if(sd.mode==TerminalSd::ReadIo)assert(sd.ioResults==1);
 if(sd.mode==TerminalSd::RunningCancel)assert(sd.runningFired&&sd.pendingEnd>=3);
 if(sd.mode==TerminalSd::QueuedCancel||sd.mode==TerminalSd::LeaseLoss||sd.mode==TerminalSd::StrictUnavailable||sd.mode==TerminalSd::Unavailable||sd.mode==TerminalSd::QuiescenceLoss)assert(sd.io==0);
 const unsigned io=sd.io,observed=sd.postEndObserved;noTerminalOutput(job);job.cancel();job.step();assert(job.state()==Job::State::Retained&&!job.exitSafe()&&sd.io==io&&sd.postEndObserved==observed);
}
static uint32_t terminalCrc(const Bytes&b,size_t n){uint32_t c=~0u;for(size_t at=0;at<n;++at){c^=b[at];for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
static void terminalLegacy(Files&f,bool single){
 Bytes b(single?128:132);memcpy(b.data(),single?"RISCKV1":"RISCKV2",8);put32(b.data()+8,6);put32(b.data()+16,7);strcpy(reinterpret_cast<char*>(b.data()+24),"shared-00000006");if(!single)put32(b.data()+124,b.size());put32(b.data()+b.size()-4,terminalCrc(b,b.size()-4));
 if(single){f["/RTECFG/00000006/settings.kv"]=b;return;}Bytes index(28);memcpy(index.data(),"RTEKVP2",8);put32(index.data()+8,b.size());put32(index.data()+12,7);put32(index.data()+20,terminalCrc(b,b.size()));put32(index.data()+24,terminalCrc(index,24));f["/RTECFG/00000006/settings.index"]=index;f["/RTECFG/00000006/kv-0000000000000007-00000000"]=b;
}
void retainedAfterEnd(){
 unsigned cases=0;
 // Replica + two legacy readers, volatile/durable writers, target/base recovery.
 for(unsigned kind=0;kind<7;++kind)for(unsigned m=0;m<10;++m)for(bool lost:{false,true}){
  if(m==TerminalSd::QuiescenceLoss&&kind<5)continue;
  resetProcess();Setup setup;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,setup.sd);
   if(kind>=5){Sd before(setup.sd);Hash hash;T writer(hash.api(),before.guard());assert(startDurable(f,before,writer)==C::Ok);if(kind==6)drain(before,writer);writer.cancel();drain(before,writer);assert(writer.exitSafe()&&f.core.exportPrepared());}
   if(kind==1||kind==2)terminalLegacy(setup.sd,kind==2);
   if(m==TerminalSd::ContentFailure)setup.sd[kind==1?"/RTECFG/00000006/settings.index":kind==2?"/RTECFG/00000006/settings.kv":Index][0]^=1;
   const auto map=mapOf(f.core),pending=intents(f.core);TerminalSd sd(setup.sd,TerminalSd::Mode(m),lost);Hash hash;bool quiet=true;
   auto run=[&](auto&job){const auto root=nativeRoot();const unsigned nativeIo=Nf::nvsTotal+Nf::fsTotal;terminalRun(sd,job,quiet);assert(nativeRoot()==root&&mapOf(f.core)==map&&intents(f.core)==pending&&nativeIo==Nf::nvsTotal+Nf::fsTotal&&f.core.exportPrepared()==(kind>=4));};
   if(kind==0){Reader job(f.core,hash.api(),sd.guard());assert(nativeCall(f,[&]{return job.start(sd.volume());})==C::Ok);run(job);}
   else if(kind<=2){Legacy job(f.core,hash.api(),sd.guard());assert(nativeCall(f,[&]{return job.start(sd.volume(),kind==1?Legacy::Source::PagedV2:Legacy::Source::SingleFile);})==C::Ok);run(job);}
   else if(kind<=4){T job(hash.api(),sd.guard());assert((kind==4?startDurable(f,sd,job):nativeCall(f,[&]{return job.start(f.core,sd.volume());}))==C::Ok);run(job);}
   else {Recovery job(f.core,hash.api(),sd.guard(),quietGuard(quiet));assert(nativeCall(f,[&]{return job.start(sd.volume());})==C::Ok);run(job);}
  }
  resetProcess();++cases;
 }
 printf("terminal retention: %u actual adapter end-OK success/IO/queued/completed/running-cancel/lease/strict-observer cases retain all four consumers before safe exit, output or native acknowledgement PASS\n",cases);
}

int main(int argc,char**argv){assert(argc==2);setbuf(stdout,nullptr);Nf::rootPath=argv[1];std::filesystem::create_directories(Nf::rootPath);retainedWriter();retainedRecovery();retainedTransfer();retainedAfterEnd();assert(Nf::handles.empty());puts("EXPERIMENTAL HOST PROOF ONLY: late Retained is terminal custody; no native acknowledgement, cancellation escape, production or hardware activation");}
