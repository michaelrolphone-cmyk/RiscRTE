#pragma once
#include "NativeSettingsMaintenance.h"
#if RISC_NATIVE_SETTINGS_LIFECYCLE
#pragma push_macro("word")
#undef word
#include "../../../experimental/native_replica/CheckedSdNativeRestore.h"
#include "../../../experimental/native_replica/CheckedFirstSdCheckpoint.h"
#include "../../../experimental/native_replica/CheckedNvsLegacySources.h"
#pragma pop_macro("word")
namespace RiscCpu {
// Optional independently supplied typed bootstrap clients. Missing and exact
// interrupted receipts dispatch only through complete checked native sources.
// An available SD dataset competes explicitly with independent legacy data.
// No failed SD operation establishes vacancy or authorizes fresh defaults.
class NativeSettingsBootstrap final {
 public:
 using C=RiscExperimental::NativeReplicaCore;using Store=RiscExperimental::CheckedNativeReplicaStore;
 using Coordinator=RiscExperimental::NativeSettingsCoordinator;
 using Catalog=RiscExperimental::CheckedSdSettingsCatalog;using Restore=RiscExperimental::CheckedSdNativeRestore;
 using First=RiscExperimental::CheckedFirstSdCheckpoint;using Sources=RiscExperimental::CheckedNvsFreshSources;
 using Baseline=RiscExperimental::NativeBaselineAdmission;using Legacy=RiscExperimental::CheckedNvsLegacySources;
 using Attempt=RiscExperimental::NativeSdPortAttempt;using Progress=NativeSettingsLifecycle::Progress;
 using Result=RiscExperimental::NativeSdSynchronization::State;
 struct Namespace {C*core;Store*store;Catalog::Digest digest;C::Allocator allocator;Sources*sources=nullptr;First::Authority first{};Legacy*legacy=nullptr;};
 NativeSettingsBootstrap(Namespace preferences,Namespace wifi,uint64_t device):inputs_{preferences,wifi},device_(device){}
 ~NativeSettingsBootstrap();
 NativeSettingsBootstrap(const NativeSettingsBootstrap&)=delete;NativeSettingsBootstrap&operator=(const NativeSettingsBootstrap&)=delete;
 bool binds(const C&,const Store&,const C&,const Store&,uint64_t)const;
 bool registerBeforeActivate(Coordinator&,Coordinator&);
 bool firstAvailable(unsigned)const;
 Progress beginFirst(RiscBoot::Runtime&,Port&,unsigned);
 Progress beginPreApp(RiscBoot::Runtime&,Port&,int,int);
 Progress poll();void cancel();bool exitSafe()const;bool retained()const;
 Result result(unsigned i)const{return i<2?results_[i]:Result::Failed;}
 int error(unsigned i)const{return i<2?errors_[i]:C::Invalid;}
 private:
 enum class Phase {Idle,NativeInspect,NativeInspectResult,NativeInitialize,NativeInitializeResult,Refresh,RefreshResult,Close,CatalogPrepare,CatalogRead,RestoreCollect,FirstPrepare,FirstStep,FirstArm,FirstAck,Release};
 enum class NativeMode {None,Fresh,Import,FreshResume,ImportResume,SdResume};
 struct NativeInitialization {Baseline factory;C::Initialization admission;
  NativeInitialization(void*owner,C&core,Baseline::Guard guard,Baseline::Digest digest,Baseline::Sources sources,C::Allocator allocator):factory(owner,core,guard,digest,sources,allocator){}
 };
 struct Lock {std::atomic<bool>&g;bool held;explicit Lock(std::atomic<bool>&v):g(v),held(!g.exchange(true,std::memory_order_acquire)){}~Lock(){if(held)g.store(false,std::memory_order_release);}explicit operator bool()const{return held;}};
 Namespace inputs_[2];Coordinator*services_[2]{};uint32_t slots_[2]{};uint64_t device_;
 Result results_[2]={Result::Idle,Result::Idle};int errors_[2]={C::Ok,C::Ok};
 Progress sweepResult_=Progress::Idle;
 RiscExperimental::OwnedSettingsScratch<risc_storage_async_result_v1> refresh_;
 Coordinator::Lease lease_{};RiscBoot::Runtime*runtime_=nullptr;Port*port_=nullptr;
 unsigned current_=0,remaining_=0,kind_=0;bool registered_=false,active_=false,firstMode_=false;
 std::atomic<bool>cancelled_{false};
 std::atomic<bool>gate_{false},fault_{false};Phase phase_=Phase::Idle;
 NativeMode nativeMode_=NativeMode::None;Baseline::Witness nativeWitness_{};int nativeResult_=C::Invalid;
 alignas(Attempt) unsigned char attemptStorage_[sizeof(Attempt)]{};Attempt*attempt_=nullptr;
 static constexpr size_t FirstBytes=sizeof(Catalog)>sizeof(First)?sizeof(Catalog):sizeof(First);
 static constexpr size_t Bytes=FirstBytes>sizeof(NativeInitialization)?FirstBytes:sizeof(NativeInitialization);
 alignas(Catalog) alignas(First) alignas(Restore) alignas(NativeInitialization) unsigned char storage_[Bytes>sizeof(Restore)?Bytes:sizeof(Restore)]{};
 static_assert(sizeof(storage_)>=sizeof(Catalog)&&sizeof(storage_)>=sizeof(First)&&sizeof(storage_)>=sizeof(Restore)&&sizeof(storage_)>=sizeof(NativeInitialization),"Bootstrap placement storage must cover every selected type");
 Catalog*catalog(){return reinterpret_cast<Catalog*>(storage_);}First*first(){return reinterpret_cast<First*>(storage_);}Restore*restore(){return reinterpret_cast<Restore*>(storage_);}
 NativeInitialization*native(){return reinterpret_cast<NativeInitialization*>(storage_);}
 bool owner()const;bool workerStep();void destroy();void destroyAttempt();
 int inspectNative();int initializeNative();void queueNative();Baseline::Guard nativeGuard();
 Progress start(RiscBoot::Runtime&,Port&,unsigned,bool);Progress startNamespace();Progress completeNamespace();Progress startAttempt();Progress retain();
 void finish(int,bool=false);Catalog::Guard guard();Restore::Guard restoreGuard();
};
}
#endif
