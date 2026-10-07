#include "ProviderGraphV2.h"
#include "../../../lib/hal/RuntimeFaultRetention.h"
#include "ProviderOwnedSpecV2.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>

namespace RuntimeProviders {
namespace {
template <size_t N>
void copyError(char (&out)[N], const char* input) {
  size_t count = 0;
  if (input) while (count + 1 < N && input[count]) ++count;
  // Both fields may share an enclosing object; do not use restrict-qualified
  // formatting to copy a module diagnostic into its graph's diagnostic.
  if (count) std::memmove(out, input, count);
  out[count] = 0;
}
}

namespace {
bool validName(const char* s) {
  if (!s || !s[0]) return false;
  size_t i = 0;
  while (i < 95 && s[i]) ++i;
  return i && i < 95;
}
}

GraphV2::~GraphV2() {
  risc_runtime_retention_guard();
  // Bound dependency tables may be retained by mapped ELFs and IRQ callbacks.
  // Never destroy those arrays while a provider refuses verified quiescence.
  if (!shutdown()) std::abort();
  for (size_t i = 0; i < count_; ++i) {
    delete nodes_[i].owned;
    nodes_[i].owned = nullptr;
  }
}

int GraphV2::find(const char* capability, uint32_t api) const {
  if (!validName(capability) || !api) return -1;
  int match = -1;
  for (size_t i = 0; i < count_; ++i) {
    if (nodes_[i].spec.api != api ||
        std::strcmp(nodes_[i].spec.provides, capability) != 0) continue;
    if (match >= 0) return -2;
    match = static_cast<int>(i);
  }
  return match;
}

int GraphV2::findProvider(const char* id, const char* capability, uint32_t api, uint64_t instance) const {
  if (!validName(id) || !validName(capability) || !api) return -1;
  int result=-1;
  for (size_t i=0;i<count_;++i) {
    const auto& s=nodes_[i].spec;
    if (s.api==api && !std::strcmp(s.id,id) && !std::strcmp(s.provides,capability) &&
        (!instance || (s.hardware && s.hardware->instance_id==instance))) {
      if (result>=0) return -2;
      result=static_cast<int>(i);
    }
  }
  return result;
}

bool GraphV2::hasProvider(const char* providerId, const char* capability,
                          uint32_t api) const {
  return findProvider(providerId, capability, api) >= 0;
}

bool GraphV2::hasProviderId(const char* providerId) const {
  if (!validName(providerId)) return false;
  for (size_t i = 0; i < count_; ++i)
    if (std::strcmp(nodes_[i].spec.id, providerId) == 0) return true;
  return false;
}

bool GraphV2::addVerified(const SpecV2& spec) {
  return addChecked(spec, false);
}

bool GraphV2::addManagerValidatedPrivileged(const SpecV2& spec) {
  // Manager-validated private entry. Exact privileged import validation stays
  // mandatory on relocation; content checksums are installation consistency.
  (void)spec; return false; // Privileged package admission is outside this bootstrap slice.
}

bool GraphV2::addChecked(const SpecV2& spec, bool privilegedAdmission) {
  bool emptyDigest = true;
  for (uint8_t byte : spec.contentSha256)
    if (byte) { emptyDigest = false; break; }
  const bool regular = spec.requiredOsCpuAbi == 0 &&
                       spec.verifiedElfBytes == nullptr &&
                       spec.verifiedElfLength == 0 &&
                       spec.declaredImports == nullptr &&
                       spec.declaredImportCount == 0 && emptyDigest;
  const bool privileged = spec.requiredOsCpuAbi == 1 &&
                          spec.verifiedElfBytes != nullptr &&
                          spec.verifiedElfLength > 0 &&
                          spec.verifiedElfLength <= 8u * 1024u * 1024u &&
                          spec.declaredImports != nullptr &&
                          spec.declaredImportCount <= 128;
  if (count_ == kMaxModules || !validName(spec.id) ||
      !validName(spec.provides) || !spec.api ||
      (spec.verifiedElfPath && spec.verifiedElfPath[0] != '/') ||
      (regular && !spec.verifiedElfPath) ||
      (!regular && !privileged) ||
      (privilegedAdmission != privileged) ||
      spec.requirementCount > kMaxRequirements ||
      !spec.lease.valid() ||
      (spec.requirementCount && !spec.requirements)) return false;
  if (privileged) {
    for (size_t i = 0; i < spec.declaredImportCount; ++i) {
      if (!spec.declaredImports[i]) return false;
      char bounded[OwnedNodeV2::kImportName]{};
      if (!OwnedNodeV2::copyString(bounded, sizeof(bounded),
                                   spec.declaredImports[i])) return false;
      if (i && std::strcmp(spec.declaredImports[i - 1], spec.declaredImports[i]) >= 0)
        return false;
    }
  }
  // nodes_ is fixed storage: appending a new absent provider cannot move or
  // invalidate healthy active modules, retained dependency tables, or live
  // grants. Never mutate the graph while a provider is mid-activation or
  // quarantined after failed start/quiesce; those states deliberately retain
  // mapped code and dependency pins until explicit recovery succeeds.
  for (size_t i = 0; i < count_; ++i) {
    if ((std::strcmp(nodes_[i].spec.id, spec.id) == 0 &&
         (!spec.hardware || !nodes_[i].spec.hardware || spec.hardware->instance_id==nodes_[i].spec.hardware->instance_id)) ||
        nodes_[i].visit == Visit::Visiting ||
        nodes_[i].module.state() == ModuleV2::State::Failed) return false;
  }
  for (size_t i = 0; i < spec.requirementCount; ++i) {
    if (!validName(spec.requirements[i].capability) || !spec.requirements[i].api)
      return false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(spec.requirements[i].capability,
                      spec.requirements[j].capability) == 0) return false;
  }
  if (spec.resourceIdentity.id[0]) {
    const auto& identity = spec.resourceIdentity;
    RuntimePackages::Identity canonical{};
    if (!RuntimePackages::makeIdentity(identity.kind, identity.id, identity.version,
          identity.artifact, false, &canonical) || identity.legacyVersion ||
        identity.kind == RuntimePackages::Kind::Application ||
        std::strcmp(identity.id, spec.id)) return false;
  }
  auto* owned = new (std::nothrow) OwnedNodeV2();
  if (!owned) return false;
  if (!owned->snapshot(spec)) {
    delete owned;
    return false;
  }
  Node& node = nodes_[count_++];
  node.owned = owned;
  node.spec = owned->spec;
  node.module.bindGraphOwnedImage(node.spec.verifiedElfBytes, node.spec.verifiedElfLength);
  return true;
}

void GraphV2::releaseDependencies(size_t index) {
  Node& node = nodes_[index];
  while (node.acquired) {
    size_t dependency = node.dependencies[--node.acquired];
    (void)nodes_[dependency].module.unpinConsumer();
    (void)deactivateIfUnused(dependency);
  }
  // Never invalidate this table before the provider has safely unloaded.
  for (size_t i = 0; i < node.spec.requirementCount; ++i)
    node.boundDependencies[i] = {};
}

bool GraphV2::deactivateIfUnused(size_t index) {
  Node& node = nodes_[index];
  if (node.visit != Visit::Active || node.module.consumers()) return true;
  if (!node.module.unload()) {
    if (node.module.lastError()[0]) copyError(error_, node.module.lastError());
    else fail("Provider quiescence rejected", node.spec.id);
    return false;
  }
  node.visit = Visit::Idle;
  releaseDependencies(index);
  return true;
}

bool GraphV2::fail(const char* stage, const char* identity) {
  std::snprintf(error_, sizeof(error_), "%s: %s", stage, identity ? identity : "unknown");
  return false;
}

bool GraphV2::activate(size_t index) {
  Node& node = nodes_[index];
  if (node.visit == Visit::Visiting) return fail("Dependency cycle", node.spec.id);
  if (node.module.state() == ModuleV2::State::Failed) {
    if (node.module.lastError()[0]) {
      copyError(error_, node.module.lastError());
      return false;
    }
    return fail("Provider failed", node.spec.id);
  }
  if (node.visit == Visit::Active)
    return node.module.state() == ModuleV2::State::Active;
  node.visit = Visit::Visiting;
  for (size_t i = 0; i < node.spec.requirementCount; ++i) {
    const RequirementV2& requirement = node.spec.requirements[i];
    if (!std::strcmp(requirement.capability,"hardware.device")) {
      if (!node.spec.hardware || requirement.api!=1) {
        releaseDependencies(index); node.visit=Visit::Idle;
        return fail("Missing selected hardware",node.spec.id);
      }
      node.boundDependencies[i]={requirement.capability,1,node.spec.hardware};
      continue;
    }
    if (requirement.trustedApi) {
      node.boundDependencies[i]={requirement.capability,requirement.api,requirement.trustedApi};
      continue;
    }
    const int dependency = requirement.providerId
        ? findProvider(requirement.providerId,requirement.capability,requirement.api,requirement.providerInstance)
        : find(requirement.capability, requirement.api);
    if (dependency < 0 ||
        !activate(static_cast<size_t>(dependency)) ||
        !nodes_[dependency].module.pinConsumer()) {
      if (dependency < 0) fail("Dependency missing/ambiguous", requirement.capability);
      else if (!error_[0]) fail("Dependency pin failed", requirement.capability);
      releaseDependencies(index);
      node.visit = Visit::Idle;
      return false;
    }
    node.dependencies[node.acquired++] = static_cast<uint8_t>(dependency);
    node.boundDependencies[i] = {requirement.capability, requirement.api,
                                 nodes_[dependency].module.capability()};
  }
  (void)node.module.setStreamHost(streamHost_);
  (void)node.module.setLease(node.spec.lease);
  (void)node.module.setResourceIdentity(node.spec.resourceIdentity);
  (void)node.module.setPackageAdmission(node.spec.packageManifestSha256, node.spec.packageSourceStamp);
  // The graph owns package identity, singleton admission and consumer counts.
  // Every node needs its own mapping: software and hardware packages may share
  // driver.elf, while ordinary dlopen intentionally rejects duplicate basenames.
  const bool loaded = node.spec.requiredOsCpuAbi
      ? node.module.loadVerifiedBytes(node.spec.verifiedElfBytes,
                                      node.spec.verifiedElfLength,
                                      node.spec.contentSha256,
                                      node.spec.declaredImports,
                                      node.spec.declaredImportCount,
                                      node.spec.id, node.spec.provides,
                                      node.spec.api,
                                      node.spec.requirementCount ? node.boundDependencies : nullptr,
                                      node.spec.requirementCount)
      : node.module.load(node.spec.verifiedElfPath, node.spec.id,
                         node.spec.provides, node.spec.api,
                         node.spec.requirementCount ? node.boundDependencies : nullptr,
                         node.spec.requirementCount, true);
  if (!loaded) {
    if (node.module.lastError()[0])
      copyError(error_, node.module.lastError());
    else fail("Provider load/start failed (no diagnostic)", node.spec.id);
    if (node.module.unload()) {
      releaseDependencies(index);
      node.visit = Visit::Idle;
    } else {
      // A failed start may own live DMA/interrupt state. Never drop the
      // dependencies, clear the interface pointers or regrant the node.
      node.visit = Visit::Active;
    }
    return false;
  }
  node.visit = Visit::Active;
  return true;
}

GrantV2 GraphV2::acquireIndex(size_t index) {
  size_t slot = kMaxGrants;
  for (size_t i = 0; i < kMaxGrants; ++i)
    if (!grants_[i].occupied) { slot = i; break; }
  if (slot == kMaxGrants) { fail("Grant table full", nodes_[index].spec.id); return {}; }
  if (!activate(index)) return {};
  if (!nodes_[index].module.pinConsumer()) {
    fail("Provider pin failed", nodes_[index].spec.id);
    (void)deactivateIfUnused(index);
    return {};
  }
  ++nextGeneration_;
  if (!nextGeneration_) ++nextGeneration_;
  grants_[slot] = {nextGeneration_, static_cast<uint8_t>(index), true};
  return {static_cast<uint32_t>(slot + 1), nextGeneration_};
}

void GraphV2::poll(uint32_t (*nowMs)(), void (*yield)()) {
  if (!nowMs || !count_ || polling_) return;
  polling_ = true;
  const uint32_t began = nowMs();
  unsigned calls = 0;
  for (size_t visited = 0; visited < count_; ++visited) {
    const uint32_t elapsed = static_cast<uint32_t>(nowMs() - began);
    if (elapsed >= 10) break;
    const uint32_t remaining = 10 - elapsed;
    const size_t index = nextPoll_;
    nextPoll_ = (nextPoll_ + 1) % count_;
    if (nodes_[index].module.poll(remaining < 8 ? remaining : 8)) ++calls;
    if (calls >= 4 || static_cast<uint32_t>(nowMs() - began) >= 10) break;
  }
  if (calls && yield) yield();
  polling_ = false;
}
GrantV2 GraphV2::acquire(const char* capability, uint32_t api) {
  error_[0] = 0;
  const int target = find(capability, api);
  if (target < 0) fail("Capability missing/ambiguous", capability);
  return target < 0 ? GrantV2{} : acquireIndex(static_cast<size_t>(target));
}

GrantV2 GraphV2::acquireFrom(const char* providerId, const char* capability,
                           uint32_t api, uint64_t instance) {
  error_[0] = 0;
  const int target = findProvider(providerId, capability, api, instance);
  if (target < 0) fail("Provider not admitted", providerId);
  return target < 0 ? GrantV2{} : acquireIndex(static_cast<size_t>(target));
}

const void* GraphV2::interfaceFor(GrantV2 grant) const {
  if (!grant.slot || grant.slot > kMaxGrants || !grant.generation) return nullptr;
  const GrantSlot& slot = grants_[grant.slot - 1];
  if (!slot.occupied || slot.pendingRelease ||
      slot.generation != grant.generation) return nullptr;
  return nodes_[slot.node].module.capability();
}

bool GraphV2::grantStream(GrantV2 grant, uint32_t consumer, uint32_t endpoint, uint32_t rights) {
  if (!interfaceFor(grant) || !streamHost_ || !streamHost_->grant || !streamHost_->revokeGrant)
    return false;
  const uint64_t context = nodes_[grants_[grant.slot - 1].node].module.streamContext();
  const uint64_t lease = (uint64_t(grant.generation) << 32) | grant.slot;
  return context && streamHost_->grant(context, lease, consumer, endpoint, rights);
}
bool GraphV2::release(GrantV2 grant) {
  if (!grant.slot || grant.slot > kMaxGrants || !grant.generation) return false;
  GrantSlot& slot = grants_[grant.slot - 1];
  if (!slot.occupied || slot.generation != grant.generation) return false;
  if (!slot.pendingRelease && streamHost_ && streamHost_->revokeGrant) {
    const uint64_t context = nodes_[slot.node].module.streamContext();
    if (context) streamHost_->revokeGrant(context, (uint64_t(grant.generation) << 32) | grant.slot);
  }
  const size_t node = slot.node;
  if (!slot.pendingRelease) {
    // Revoke all future interface access and decrement the consumer exactly
    // once, even if hardware quiescence fails repeatedly.
    if (!nodes_[node].module.unpinConsumer()) return false;
    slot.pendingRelease = true;
  }
  if (!deactivateIfUnused(node)) return false;
  // Only a completed teardown (or another still-live consumer) can retire
  // the slot. Until then retain its original generation for release retries.
  slot.occupied = false;
  slot.pendingRelease = false;
  return true;
}

size_t GraphV2::liveGrants() const {
  size_t total = 0;
  for (const GrantSlot& grant : grants_) if (grant.occupied) ++total;
  return total;
}

bool GraphV2::shutdown() {
  if (liveGrants()) return false;
  for (size_t pass = 0; pass <= count_; ++pass) {
    bool progress = false;
    for (size_t i = 0; i < count_; ++i) {
      Node& node = nodes_[i];
      if (node.module.consumers()) continue;
      if (node.visit == Visit::Active ||
          (node.visit == Visit::Idle &&
           node.module.state() == ModuleV2::State::Failed)) {
        if (!node.module.unload()) return false;
        node.visit = Visit::Idle;
        releaseDependencies(i);
        progress = true;
      }
    }
    if (!progress) break;
  }
  for (size_t i = 0; i < count_; ++i)
    if (nodes_[i].visit != Visit::Idle || nodes_[i].acquired ||
        nodes_[i].module.state() != ModuleV2::State::Absent ||
        nodes_[i].module.consumers()) return false;
  return true;
}
}  // namespace RuntimeProviders

bool RuntimeProviders::GraphV2::activationSafe() const {
  if(polling_)return false;
  for(size_t i=0;i<count_;++i)
    if(nodes_[i].visit==Visit::Visiting || nodes_[i].module.state()==ModuleV2::State::Failed)return false;
  for(const auto& grant:grants_)if(grant.occupied && grant.pendingRelease)return false;
  return true;
}
