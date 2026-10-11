// Focused late-provider-retention proof using the actual checked native adapter.
#include "native_rebase_roundtrip_fixture.h"
#include "experimental/native_replica/CheckedNativeExportRecovery.h"
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
int main(int argc,char**argv){assert(argc==2);setbuf(stdout,nullptr);Nf::rootPath=argv[1];std::filesystem::create_directories(Nf::rootPath);retainedWriter();retainedRecovery();retainedTransfer();assert(Nf::handles.empty());puts("EXPERIMENTAL HOST PROOF ONLY: late Retained is terminal custody; no native acknowledgement, cancellation escape, production or hardware activation");}
