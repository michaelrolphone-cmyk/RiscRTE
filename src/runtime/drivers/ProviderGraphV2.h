#pragma once
#include "ProviderModuleV2.h"
#include "runtime/RuntimeLimits.h"
#include <RiscHardwareConfigV1.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

/* Generic graph owns all registration metadata and dependency interface
 * tables. Privileged admission belongs only to the trusted executor;
 * a caller-supplied digest/import set must never confer OS/CPU rights. */
namespace RuntimePackages { class DeviceProviderExecutorV2; }
namespace RuntimeProviders {
struct RequirementV2 {
  const char* capability;
  uint32_t api;
  const char* providerId = nullptr;
  uint64_t providerInstance = 0;
  const void* trustedApi = nullptr; // Firmware-owned and pinned for graph lifetime.
};
struct SpecV2 {
  const char* id;
  const char* verifiedElfPath;
  const char* provides;
  uint32_t api;
  const RequirementV2* requirements;
  size_t requirementCount;
  uint32_t requiredOsCpuAbi = 0;
  const uint8_t* verifiedElfBytes = nullptr;
  size_t verifiedElfLength = 0;
  const char* const* declaredImports = nullptr;
  size_t declaredImportCount = 0;
  uint8_t contentSha256[32] = {};
  RuntimePackages::Identity resourceIdentity{};
  uint8_t packageManifestSha256[32]{};
  StorageGenerationStamp packageSourceStamp{};
  const risc_hardware_device_v1* hardware = nullptr;
  ModuleLeaseV2 lease{};
};
struct GrantV2 {
  uint32_t slot = 0;
  uint32_t generation = 0;
};

struct OwnedNodeV2;
class GraphV2 final {
 public:
  static constexpr size_t kMaxModules = RiscLimits::Providers;
  static constexpr size_t kMaxRequirements = 16;
  static_assert(kMaxModules-1<=UINT8_MAX,"Provider index width exceeded");
  // PSRAM cohort targets reserve one boot pin per provider plus16 app slots.
  static constexpr size_t kMaxGrants = RiscLimits::Grants;
  explicit GraphV2(const StreamHostV1* streams = nullptr) : streamHost_(streams) {}
  GraphV2(const GraphV2&) = delete;
  GraphV2& operator=(const GraphV2&) = delete;
  ~GraphV2();
  // Ordinary providers only; forged privileged specs fail regardless of hash.
  bool addVerified(const SpecV2& spec);
  GrantV2 acquire(const char* capability, uint32_t api);
  GrantV2 acquireFrom(const char* providerId, const char* capability, uint32_t api, uint64_t instance = 0);
  bool release(GrantV2 grant);
  // Trusted capability broker only; consumer is an authenticated context ID.
  bool grantStream(GrantV2, uint32_t consumer, uint32_t endpoint, uint32_t rights);
  const void* interfaceFor(GrantV2 grant) const;
  const risc_stream_session_provider_v1* streamSessionsFor(GrantV2, uint64_t* context) const;
  bool revokeStreamGrants(GrantV2);
  // Serialized native session callbacks cannot reenter graph lifecycle/polling.
  bool lifecycleBusy() const { return polling_ || streamCallback_ || lifecycle_; }
  bool beginStreamCallback() { if (streamCallback_ || !activationSafe()) return false; streamCallback_=true; return true; }
  void endStreamCallback() { streamCallback_=false; }
  bool shutdown();
  // Serialized round-robin dispatcher: <=4 callbacks, <=8ms each, 10ms total.
  // Each budget is clamped to remaining time before dispatch. Providers must
  // return cooperatively; an overrun cannot be preempted. No graph lock.
  // Optional scheduler yield runs once after work; omit when caller yields.
  void poll(uint32_t (*nowMs)(), void (*yield)());
  bool hasProvider(const char* providerId, const char* capability, uint32_t api) const;
  // Stored lifecycle state only. Does not load, validate or acquire a provider.
  bool activeFrom(const char* providerId, const char* capability, uint32_t api, uint64_t instance=0) const;
  bool hasProviderId(const char* providerId) const;
  size_t moduleCount() const { return count_; }
  size_t liveGrants() const;
  // Read-only admission fence: never recover/regrant uncertain cleanup state.
  bool activationSafe() const;
  // Readonly dependency calls remain valid in bounded poll callbacks. Failed
  // modules, visiting activation and pending releases still reject all reads.
  bool dependencyReadSafe() const;
  const char* lastError() const { return error_; }

  // Recover only the exact provider whose activation failed before a grant
  // could be issued. Never revoke a live or pending-release grant, unload a
  // dependency held by another provider, or reset uncertain physical state.
  // A failed quiesce keeps the mapped ELF and dependency pointers intact for
  // a later checked retry; this is NOT a global graph shutdown.
  bool recoverFailedFrom(const char* providerId, const char* capability, uint32_t api) {
    if (lifecycleBusy()) return false;
    LifecycleScope scope(lifecycle_);
    const int target = findProvider(providerId, capability, api);
    if (target < 0) return false;
    const size_t index = static_cast<size_t>(target);
    Node& node = nodes_[index];
    if (node.visit == Visit::Visiting || node.module.consumers()) return false;
    for (const GrantSlot& grant : grants_)
      if (grant.occupied && grant.node == index) return false;
    if (node.visit == Visit::Idle && node.module.state() == ModuleV2::State::Absent)
      return true; // Failed before mapping, or already recovered.
    if (node.visit == Visit::Releasing) return deactivateIfUnused(index);
    if (node.module.state() != ModuleV2::State::Failed ||
        !node.module.unload()) return false;
    node.visit = Visit::Releasing;
    return deactivateIfUnused(index); // Only after verified physical quiescence.
  }

  // Enumerate only independently admitted package identities. Enumeration
  // grants no capability, invokes no ELF and permits multiple providers of
  // the same semantic capability. The caller must acquire the exact ID and
  // must not retain the returned string across graph destruction.
  const char* matchingProviderId(size_t index, const char* capability,
                                 uint32_t api) const {
    if (index >= count_ || !capability || !*capability || !api) return nullptr;
    const SpecV2& spec = nodes_[index].spec;
    return spec.provides && spec.id && spec.api == api &&
                   std::strcmp(spec.provides, capability) == 0 ? spec.id : nullptr;
  }

 private:
  // Compiled-in firmware executor only; not an ordinary ELF export. The
  // executor must verify package identity, policy, rollback floor, exact
  // digest and import declarations BEFORE entering this API.
  // Friendship is an API boundary, not a memory-isolation guarantee.
  friend class ::RuntimePackages::DeviceProviderExecutorV2;
  bool addManagerValidatedPrivileged(const SpecV2& spec);
  bool addChecked(const SpecV2& spec, bool privilegedAdmission);

  enum class Visit : uint8_t { Idle, Visiting, Active, Releasing };
  struct Node {
    SpecV2 spec{};
    OwnedNodeV2* owned = nullptr;
    ModuleV2 module;
    Visit visit = Visit::Idle;
    uint8_t dependencies[kMaxRequirements]{};
    // start() may retain this table until quiesce/stop; unlike a temporary
    // activate() stack array, this remains valid while the ELF is mapped.
    risc_provider_dependency_v1 boundDependencies[kMaxRequirements]{};
    size_t acquired = 0;
    // The last dependency can be unpinned but still awaiting checked cleanup.
    // Keep it in acquired until cleanup succeeds, so retry never unpins twice.
    bool dependencyReleasePending = false;
  };
  struct GrantSlot {
    uint32_t generation = 0;
    uint8_t node = 0;
    bool occupied = false;
    // First release revokes use and unpins exactly once. If quiesce fails,
    // preserve this slot for an explicit retry; never call the ELF through it.
    bool pendingRelease = false;
  };
  bool fail(const char* stage, const char* identity);
  char error_[160]{};
  Node nodes_[kMaxModules]{};
  GrantSlot grants_[kMaxGrants]{};
  const StreamHostV1* streamHost_ = nullptr;
  size_t count_ = 0;
  uint32_t nextGeneration_ = 0;
  size_t nextPoll_ = 0;
  bool polling_ = false;
  bool streamCallback_ = false;
  bool lifecycle_ = false;
  struct LifecycleScope {
    bool& state;
    explicit LifecycleScope(bool& flag):state(flag){state=true;}
    ~LifecycleScope(){state=false;}
  };

  int find(const char* capability, uint32_t api) const;
  int findProvider(const char* id, const char* capability, uint32_t api, uint64_t instance = 0) const;
  GrantV2 acquireIndex(size_t index);
  bool activate(size_t index);
  bool releaseDependencies(size_t index);
  bool deactivateIfUnused(size_t index);
};
}  // namespace RuntimeProviders
