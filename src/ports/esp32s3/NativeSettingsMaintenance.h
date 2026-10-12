#pragma once
#include "NativeSettingsLifecycle.h"
#if RISC_NATIVE_SETTINGS_LIFECYCLE
#include "../../../experimental/native_replica/NativeSdSynchronization.h"
namespace RiscCpu {
class NativeSettingsBootstrap;
// Concrete, default-off Admission implementation. It selects no private backend
// or product. Construct once in permanent INTERNAL memory, before prepare().
// The exact cores/stores, digest control, payload allocators, device and native
// incarnation inputs must already be independently qualified by its caller.
class NativeSettingsMaintenance final {
 public:
 using C=RiscExperimental::NativeReplicaCore;
 using Store=RiscExperimental::CheckedNativeReplicaStore;
 using Coordinator=RiscExperimental::NativeSettingsCoordinator;
 using Sync=RiscExperimental::NativeSdSynchronization;
 using Attempt=RiscExperimental::NativeSdPortAttempt;
 using Progress=NativeSettingsLifecycle::Progress;
 struct Namespace {
  C*core;Store*store;Sync::Checkpoint::Digest digest;C::Allocator allocator;
  // Native entropy for the first durable export incarnation, never card/app/
  // clock derived. Copied at construction; nullptr cannot invent an incarnation.
  const uint8_t*initialIncarnation=nullptr;
 };
 NativeSettingsMaintenance(Namespace preferences,Namespace wifi,uint64_t device,
                           const NativeSettingsLifecycle::Writer*writers,size_t writerCount,
                           NativeSettingsBootstrap*bootstrap=nullptr);
 NativeSettingsMaintenance(const NativeSettingsMaintenance&)=delete;
 NativeSettingsMaintenance&operator=(const NativeSettingsMaintenance&)=delete;
 ~NativeSettingsMaintenance();
 const NativeSettingsLifecycle::Admission&admission()const{return admission_;}
 bool exitSafe()const;
 bool retained()const;
 // RAM-only diagnostics. No settings, keys or fingerprints are exposed.
 Sync::State result(uint32_t nameSpace)const;
 int error(uint32_t nameSpace)const;
 private:
 struct Binding {
  Namespace input{};Coordinator*service=nullptr;Sync*sync=nullptr;
  uint8_t incarnation[16]{};bool hasIncarnation=false;
  Sync::State result=Sync::State::Idle;int error=C::Ok;
  alignas(Sync) unsigned char storage[sizeof(Sync)]{};
 } bindings_[2];
 NativeSettingsLifecycle::Admission admission_{};uint64_t device_;
 NativeSettingsBootstrap*bootstrap_=nullptr;bool auxiliary_=false,preAppAuxiliary_=false;
 alignas(Attempt) unsigned char attemptStorage_[sizeof(Attempt)]{};
 Attempt*attempt_=nullptr;unsigned next_=0,current_=0;
 RiscBoot::Runtime*runtime_=nullptr;Port*port_=nullptr;unsigned remaining_=0;
 Progress sweepResult_=Progress::Idle;
 bool registered_=false,active_=false,refusing_=false,retained_=false;
 bool registerBeforeActivate(Coordinator&,Coordinator&);
 Progress begin(RiscBoot::Runtime&,Port&);
 Progress startOne(unsigned);Progress continueSweep(Progress);
 Progress poll();void cancel();void destroyAttempt();
 Progress retain();Progress finished();
};
}
#endif
