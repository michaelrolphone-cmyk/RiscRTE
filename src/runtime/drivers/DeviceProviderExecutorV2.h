#pragma once
#include "ProviderGraphV2.h"
#include "NativeProviderPolicyV1.h"

#if defined(RISC_NATIVE_RUNTIME_CALLER_TEST) && RISC_NATIVE_RUNTIME_CALLER_TEST == 1
namespace RiscBoot { class RuntimeNativeCallerTestPeer; }
#endif

namespace RuntimePackages {
// Trusted compiled-in caller only, absent from all ELF symbol tables. The
// separately supplied policy is native authority, never package metadata.
class DeviceProviderExecutorV2 final {
 public:
  enum class NativeState : uint8_t { Empty, Staged, Committed, Closing, Retained };
  enum class NativeFailure : uint8_t { None, Stage, Mapping, Commit, Close };
  // Grant storage belongs to the caller; this record never substitutes another
  // slot/generation or derives policy authority from package metadata.
  struct NativeOwner {
    NativeOwner()=default;
    NativeOwner(const NativeOwner&)=delete;
    NativeOwner& operator=(const NativeOwner&)=delete;
    NativeOwner(NativeOwner&&)=delete;
    NativeOwner& operator=(NativeOwner&&)=delete;
    NativeState state=NativeState::Empty;
    NativeFailure failure=NativeFailure::None;
    const RuntimeProviders::NativeProviderPolicyV1* policy=nullptr;
    uint64_t mapping=0;
    RuntimeProviders::GrantV2 heldGrant{};
    const RuntimeProviders::GraphV2* graphOwner=nullptr;
    bool committed=false,releaseAttempted=false,closeInProgress=false;
   private:
    friend class DeviceProviderExecutorV2;
    void reset() {
      state=NativeState::Empty;failure=NativeFailure::None;policy=nullptr;
      mapping=0;heldGrant={};graphOwner=nullptr;committed=false;releaseAttempted=false;closeInProgress=false;
    }
  };
  static bool selectedNativeOwner(const RuntimeProviders::GraphV2& graph,
      const char* id,const char* capability,uint32_t api,uint64_t instance) {
    const auto* policy=graph.nativePolicyFor(id,capability,api,instance);
    return policy && policy->osCpuAbi==5;
  }
  static bool startNativeOwner(RuntimeProviders::GraphV2& graph,
      const char* id,const char* capability,uint32_t api,uint64_t instance,
      RuntimeProviders::GrantV2& grant,NativeOwner& owner) {
    if(!stageNativeOwner(graph,id,capability,api,instance,grant,owner))return false;
    if(!graph.nativeTaskMapping(grant,owner.policy,&owner.mapping)) {
      if(!graph.activationSafe())owner.state=NativeState::Retained;
      return false;
    }
    owner.failure=NativeFailure::Commit;
    if(!graph.commitNativeTaskOwner(grant,owner.policy,owner.mapping)) {
      if(!graph.activationSafe())owner.state=NativeState::Retained;
      return false;
    }
    owner.state=NativeState::Committed;owner.committed=true;owner.failure=NativeFailure::None;
    return true; // Entry permission, not readiness or retirement.
  }
  static bool closeNativeOwner(RuntimeProviders::GraphV2& graph,
      RuntimeProviders::GrantV2& grant,NativeOwner& owner,uint32_t budgetMs) {
    if(!grant.slot || owner.state==NativeState::Empty ||
       owner.state==NativeState::Retained || owner.releaseAttempted || owner.closeInProgress ||
       owner.graphOwner!=&graph || grant.slot!=owner.heldGrant.slot || grant.generation!=owner.heldGrant.generation ||
       graph.lifecycleBusy())return false;
    size_t index=0;
    if(!graph.nativeTaskIdentity(grant,owner.policy,index))return false;
    const auto held=owner.heldGrant;const auto* policy=owner.policy;
    const auto mapping=owner.mapping;const bool committed=owner.committed;
    owner.closeInProgress=true;
    struct CloseGuard {NativeOwner& owner;~CloseGuard(){owner.closeInProgress=false;}} guard{owner};
    if(owner.state!=NativeState::Closing) {
      owner.state=NativeState::Closing;owner.failure=NativeFailure::Close;
      const auto prepared=owner.committed
        ? graph.prepareNativeTaskClose(grant,owner.policy,owner.mapping,budgetMs)
        : graph.prepareNativeStagedClose(grant,owner.policy);
      if(prepared!=RuntimeProviders::GraphV2::NativeClosePreparation::Accepted) {
        owner.state=NativeState::Retained;return false;
      }
    }
    if(owner.state!=NativeState::Closing || owner.graphOwner!=&graph || owner.heldGrant.slot!=held.slot ||
       owner.heldGrant.generation!=held.generation || grant.slot!=held.slot ||
       grant.generation!=held.generation || owner.policy!=policy ||
       owner.mapping!=mapping || owner.committed!=committed) {
      owner.state=NativeState::Retained;return false;
    }
    // Closing never repeats a terminal callback. Uncommitted owners request
    // one resident cancellation before any grant revoke/unpin; both
    // paths wait for actual worker-safe retirement before their first release.
    const auto ready=owner.committed
      ? graph.nativeTaskCloseReady(grant,owner.policy,owner.mapping)
      : graph.nativeStagedCloseReady(grant,owner.policy);
    if(ready==RuntimeProviders::GraphV2::NativeCloseReadiness::Pending)return false;
    if(ready==RuntimeProviders::GraphV2::NativeCloseReadiness::Retained) {
      owner.state=NativeState::Retained;return false;
    }
    owner.releaseAttempted=true; // Before release: false may be pre/post revoke.
    if(!graph.release(grant)) {
      // release(false) may already have revoked/unpinned. Never callback again,
      // clear the record, destroy dependencies or infer a safe retry from bool.
      owner.state=NativeState::Retained;return false;
    }
    grant={};owner.reset();return true;
  }
  static bool registerNativePolicy(RuntimeProviders::GraphV2& graph,
      const RuntimeProviders::SpecV2& candidate,
      const RuntimeProviders::NativeProviderPolicyV1& policy) {
    return graph.addManagerValidatedPrivileged(candidate,policy);
  }
 private:
#if defined(RISC_NATIVE_RUNTIME_CALLER_TEST) && RISC_NATIVE_RUNTIME_CALLER_TEST == 1
  friend class RiscBoot::RuntimeNativeCallerTestPeer;
#endif
  // The production start path owns this exact stage/record step. No public
  // stage-only authority path is exposed.
  static bool stageNativeOwner(RuntimeProviders::GraphV2& graph,
      const char* id,const char* capability,uint32_t api,uint64_t instance,
      RuntimeProviders::GrantV2& grant,NativeOwner& owner) {
    if(grant.slot || owner.state!=NativeState::Empty)return false;
    owner.graphOwner=&graph;
    owner.policy=graph.nativePolicyFor(id,capability,api,instance);
    if(!owner.policy || owner.policy->osCpuAbi!=5){owner.reset();return false;}
    owner.failure=NativeFailure::Stage;
    grant=graph.stageNativeTaskOwner(owner.policy); // Save custody before mapping.
    owner.heldGrant=grant;
    if(!grant.slot) {
      if(!graph.activationSafe())owner.state=NativeState::Retained;
      else owner.reset();
      return false;
    }
    owner.state=NativeState::Staged;owner.failure=NativeFailure::Mapping;
    return true;
  }

};
}
