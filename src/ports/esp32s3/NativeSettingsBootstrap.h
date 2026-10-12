#pragma once
#include "NativeSettingsMaintenance.h"
#if RISC_NATIVE_SETTINGS_LIFECYCLE
#pragma push_macro("word")
#undef word
#include "../../../experimental/native_replica/CheckedSdNativeRestore.h"
#include "../../../experimental/native_replica/CheckedFirstSdCheckpoint.h"
#pragma pop_macro("word")
namespace RiscCpu {
// Optional independently supplied typed bootstrap clients. No native baseline
// is created from Missing: restore requires a qualified catalog and this exact
// checked source inventory; first export requires an already-qualified core.
class NativeSettingsBootstrap final {
 public:
 using C=RiscExperimental::NativeReplicaCore;using Store=RiscExperimental::CheckedNativeReplicaStore;
 using Coordinator=RiscExperimental::NativeSettingsCoordinator;
 using Catalog=RiscExperimental::CheckedSdSettingsCatalog;using Restore=RiscExperimental::CheckedSdNativeRestore;
 using First=RiscExperimental::CheckedFirstSdCheckpoint;using Sources=RiscExperimental::CheckedNvsFreshSources;
 using Attempt=RiscExperimental::NativeSdPortAttempt;using Progress=NativeSettingsLifecycle::Progress;
 using Result=RiscExperimental::NativeSdSynchronization::State;
 struct Namespace {C*core;Store*store;Catalog::Digest digest;C::Allocator allocator;Sources*sources=nullptr;First::Authority first{};};
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
 enum class Phase {Idle,Refresh,RefreshResult,Close,CatalogPrepare,CatalogRead,RestoreCollect,FirstPrepare,FirstStep,FirstArm,FirstAck,Release};
 struct Lock {std::atomic<bool>&g;bool held;explicit Lock(std::atomic<bool>&v):g(v),held(!g.exchange(true,std::memory_order_acquire)){}~Lock(){if(held)g.store(false,std::memory_order_release);}explicit operator bool()const{return held;}};
 Namespace inputs_[2];Coordinator*services_[2]{};uint32_t slots_[2]{};uint64_t device_;
 Result results_[2]={Result::Idle,Result::Idle};int errors_[2]={C::Ok,C::Ok};
 Coordinator::Lease lease_{};RiscBoot::Runtime*runtime_=nullptr;Port*port_=nullptr;
 unsigned current_=0,remaining_=0,kind_=0;bool registered_=false,active_=false,firstMode_=false,cancelled_=false;
 std::atomic<bool>gate_{false},fault_{false};Phase phase_=Phase::Idle;
 alignas(Attempt) unsigned char attemptStorage_[sizeof(Attempt)]{};Attempt*attempt_=nullptr;
 static constexpr size_t Bytes=sizeof(Catalog)>sizeof(First)?sizeof(Catalog):sizeof(First);
 alignas(Catalog) alignas(First) alignas(Restore) unsigned char storage_[Bytes>sizeof(Restore)?Bytes:sizeof(Restore)]{};
 Catalog*catalog(){return reinterpret_cast<Catalog*>(storage_);}First*first(){return reinterpret_cast<First*>(storage_);}Restore*restore(){return reinterpret_cast<Restore*>(storage_);}
 bool owner()const;bool workerStep();void destroy();void destroyAttempt();
 Progress start(RiscBoot::Runtime&,Port&,unsigned,bool);Progress startAttempt();Progress retain();
 void finish(int,bool=false);Catalog::Guard guard();Restore::Guard restoreGuard();
};
}
#endif
