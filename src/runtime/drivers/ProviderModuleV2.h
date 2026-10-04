#pragma once
#include "../../../lib/hal/StorageGeneration.h"
#include <RiscProviderV2.h>
#include <RiscPackageResourcesV1.h>
#include "runtime/packages/PackageIdentity.h"
#include <cstddef>
#include <cstdint>

/* Generic module loader; cannot include USB, UART, GPIO or board headers.
 * Its trusted caller validates ordinary metadata and exact private ELF bytes,
 * pins dependencies, resolves API versions and authorizes provider execution.
 * Self-declared content hashes do not grant imports or hardware rights. */
namespace RuntimeProviders {
// Internal host-only lifetime hook; no provider ABI field or ELF export.
// Its context and dependency tables must outlive every retained mapped image.
struct ModuleLeaseV2 {
  void* context = nullptr;
  bool (*begin)(void*) = nullptr;
  void (*revoke)(void*) = nullptr;
  bool valid() const {
    return (!context && !begin && !revoke) || (context && begin && revoke);
  }
};
struct StreamHostV1 {
  bool (*open)(risc_stream_provider_v1*);
  void (*revoke)(uint64_t);
  void (*close)(uint64_t);
  bool (*grant)(uint64_t, uint64_t, uint32_t, uint32_t, uint32_t);
  void (*revokeGrant)(uint64_t, uint64_t);
  bool (*openResources)(risc_stream_provider_resources_v1*, const RuntimePackages::Identity&) = nullptr;
};
class ModuleV2 final {
 public:
  /* Failed also denotes quarantine after a partial start/teardown: code and
   * lower providers remain pinned but no new consumer may use its capability.
   * unload() retries verified quiescence; it never force-unmaps hardware. */
  enum class State : uint8_t { Absent, Active, Failed };
  ModuleV2() = default;
  ModuleV2(const ModuleV2&) = delete;
  ModuleV2& operator=(const ModuleV2&) = delete;
  /* Legacy/unprivileged loader, retained for host graph tests and modules not
   * requiring OS/CPU privilege. Never grants privileged imports. */
  bool load(const char* validatedElf, const char* expectedId,
            const char* expectedCapability, uint32_t expectedApi,
            const risc_provider_dependency_v1* dependencies, size_t count, bool independent = false);
  /* PRIVATE firmware admission path. Installation checks all payload bytes.
   * Runtime loading verifies its owned ELF snapshot at cold/uncertain boundaries;
   * unchanged quiescent generation proof avoids repeated executable hashing.
   * ABI, exact allowed imports and relocation checks remain mandatory.
   * Host builds deny this path; ownership requires separate capability grants
   * and successful quiescence before unmapping. */
  bool loadVerifiedBytes(const uint8_t* candidateBytes, size_t length,
                         const uint8_t contentSha256[32],
                         const char* const* declaredImports, size_t declaredImportCount,
                         const char* expectedId, const char* expectedCapability,
                         uint32_t expectedApi,
                         const risc_provider_dependency_v1* dependencies,
                         size_t count);
  bool setStreamHost(const StreamHostV1* host) {
    if (state_ != State::Absent || handle_) return false;
    streamHost_ = host; return true;
  }
  bool setLease(const ModuleLeaseV2& lease) {
    if (state_ != State::Absent || handle_ || !lease.valid()) return false;
    lease_ = lease; return true;
  }
  bool setResourceIdentity(const RuntimePackages::Identity& identity) {
    if (state_ != State::Absent || handle_) return false;
    resourceIdentity_ = identity; return true;
  }
  bool setPackageAdmission(const uint8_t (&manifest)[32], const StorageGenerationStamp& stamp) {
    if (state_ != State::Absent || handle_) return false;
    for (size_t i=0;i<32;++i) packageManifestSha256_[i]=manifest[i];
    packageSourceStamp_ = stamp; return true;
  }
  uint64_t streamContext() const { return state_ == State::Active ? streamApi_.streams.context : 0; }
  bool poll(uint32_t budgetMs);
  bool pinConsumer();
  bool unpinConsumer();
  bool unload();
  const void* capability() const { return state_ == State::Active ? api_ : nullptr; }
  const char* lastError() const { return error_; }
  State state() const { return state_; }
  uint32_t consumers() const { return consumers_; }
 private:
  friend class GraphV2;
  // Graph owns this private immutable allocation until after module shutdown.
  // Public loadVerifiedBytes callers cannot opt themselves into this proof.
  void bindGraphOwnedImage(const uint8_t* bytes, size_t size) {
    if (ownedImage_ != bytes || ownedImageBytes_ != size) {
      ownedImage_ = bytes; ownedImageBytes_ = size; ownedImageVerified_ = false;
    }
  }
  const uint8_t* ownedImage_ = nullptr;
  size_t ownedImageBytes_ = 0;
  bool ownedImageVerified_ = false;
  uint8_t ownedImageDigest_[32]{};
  char error_[160]{};
  void report(const char* id, const char* stage, int code = 0);
  bool activateMapped(risc_driver_get_v2_fn get, const char* expectedId,
                      const char* expectedCapability, uint32_t expectedApi,
                      const risc_provider_dependency_v1* dependencies, size_t count);
  bool closeMapped();
  void revokeLease();
  void revokeStreams();
  void closeStreams();
  const StreamHostV1* streamHost_ = nullptr;
  risc_stream_provider_resources_v1 streamApi_{};
  RuntimePackages::Identity resourceIdentity_{};
  uint8_t packageManifestSha256_[32]{};
  StorageGenerationStamp packageSourceStamp_{};
  bool streamsRevoked_ = false;
  ModuleLeaseV2 lease_{};
  bool leaseAttempted_ = false;
  void* handle_ = nullptr;
  const risc_driver_v2* driver_ = nullptr;
  const void* api_ = nullptr;
  uint32_t consumers_ = 0;
  bool privileged_image_ = false;
  State state_ = State::Absent;
};
}  // namespace RuntimeProviders
