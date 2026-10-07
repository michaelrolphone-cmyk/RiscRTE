#include "ProviderModuleV2.h"
#include "../../../lib/hal/RuntimeFaultRetention.h"
#include <cstring>
#include <cstdio>
#include <limits>
#ifdef ESP_PLATFORM
#include <cstdlib>
#include <Logging.h>
extern "C" {
#include <esp_elf.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <private/esp_privileged_elf.h>
}
#endif
extern "C" {
#include <esp_dlfcn.h>
}

namespace RuntimeProviders {
namespace {
bool validDependencies(const risc_provider_dependency_v1* deps, size_t count) {
  if (count > 16 || (count && !deps)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!deps[i].capability_id || !deps[i].capability_id[0] ||
        !deps[i].api_version || !deps[i].api) return false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(deps[i].capability_id, deps[j].capability_id) == 0)
        return false;
  }
  return true;
}
bool hasQuiesce(const risc_driver_v2* driver) {
  return driver && driver->struct_size >= sizeof(risc_driver_v2) && driver->quiesce;
}
bool validRequest(const char* expectedId, const char* expectedCapability,
                  uint32_t expectedApi,
                  const risc_provider_dependency_v1* deps, size_t count) {
  return expectedId && expectedId[0] && expectedCapability &&
         expectedCapability[0] && expectedApi && validDependencies(deps, count);
}
void trace(const char* id, const char* stage) {
  (void)id; (void)stage;
#ifdef ESP_PLATFORM
  LOG_INF("PROV", "PROVREF id=%s stage=%s", id ? id : "?", stage);
#endif
}
} // namespace

void ModuleV2::report(const char* id, const char* stage, int code) {
  // Keep the original cause even if teardown subsequently fails.
  if (!error_[0]) std::snprintf(error_, sizeof(error_), "%s: %s rc=%d (0x%x)",
                              id ? id : "?", stage, code, static_cast<unsigned>(code));
  // Some ESP_PLATFORM host harnesses stub LOG_ERR to a no-op. Keep parameters
  // explicitly used in both logging-enabled and logging-disabled builds.
  (void)id; (void)stage; (void)code;
#ifdef ESP_PLATFORM
  LOG_ERR("PROV", "PROVREF id=%s failure=%s code=%d", id ? id : "?", stage, code);
#endif
}

bool ModuleV2::closeMapped() {
  revokeLease();
  risc_runtime_retention_guard();
  if (!handle_) return true;
#ifdef ESP_PLATFORM
  if (privileged_image_) {
    // Only generic memory is released here. Caller established quiescence.
    esp_elf_deinit(static_cast<esp_elf_t*>(handle_));
    std::free(handle_);
    handle_ = nullptr;
    privileged_image_ = false;
    return true;
  }
#endif
  if (dlclose(handle_) != 0) return false;
  handle_ = nullptr;
  privileged_image_ = false;
  return true;
}

bool ModuleV2::activateMapped(risc_driver_get_v2_fn get, const char* expectedId,
                              const char* expectedCapability, uint32_t expectedApi,
                              const risc_provider_dependency_v1* deps, size_t count) {
  if (risc_runtime_retention_required()) return false;
  const risc_driver_v2* candidate = get ? get(RISC_PROVIDER_DRIVER_ABI_V2) : nullptr;
  bool hardwareMapped=false;
  for (size_t i=0;i<count;++i) if (!std::strcmp(deps[i].capability_id,"hardware.device")) hardwareMapped=true;
  const bool valid = candidate && candidate->abi_version == RISC_PROVIDER_DRIVER_ABI_V2 &&
      candidate->struct_size >= RISC_DRIVER_V2_BASE_SIZE &&
      candidate->driver_id && candidate->capability_id &&
      std::strcmp(candidate->driver_id, expectedId) == 0 &&
      std::strcmp(candidate->capability_id, expectedCapability) == 0 &&
      candidate->capability_api == expectedApi && candidate->capability &&
      candidate->start && candidate->stop &&
      (!(privileged_image_ || hardwareMapped) || hasQuiesce(candidate));
  if (!valid) {
    report(expectedId, "elf-interface-or-identity");
    return false;
  }
  bool bound = true;
  if (candidate->struct_size >= sizeof(risc_driver_streams_v2)) {
    const auto* extended = reinterpret_cast<const risc_driver_streams_v2*>(candidate);
    if (extended->bind_streams) {
      if (!hasQuiesce(candidate) || !streamHost_ || !streamHost_->open ||
          !streamHost_->revoke || !streamHost_->close ||
          !(resourceIdentity_.id[0]
              ? streamHost_->openResources && streamHost_->openResources(&streamApi_, resourceIdentity_)
              : streamHost_->open(&streamApi_.streams))) {
        report(expectedId, "stream-context-unavailable");
        return false;
      }
      streamsRevoked_ = false;
      bound = extended->bind_streams(&streamApi_.streams);
    }
  }
  trace(expectedId, "hardware-start-begin");
  bool admitted = bound;
  if (admitted && lease_.begin) {
    leaseAttempted_ = true;
    admitted = lease_.begin(lease_.context);
  }
  if (admitted && candidate->start(deps, count)) {
    driver_ = candidate;
    api_ = candidate->capability;
    state_ = State::Active;
    trace(expectedId, "hardware-started");
    return true;
  }
  // Diagnostics can reenter provider code too. Storage authority must already
  // be dead before any failed-activation callback, including failed begin.
  revokeLease();
  if (candidate->struct_size >= sizeof(risc_driver_diagnostics_v2)) {
    const auto* diagnostics = reinterpret_cast<const risc_driver_diagnostics_v2*>(candidate);
    char detail[112]{};
    if (diagnostics->last_error && diagnostics->last_error(detail, sizeof(detail))) {
      detail[sizeof(detail) - 1] = 0;
      if (detail[0]) std::snprintf(error_, sizeof(error_), "%s: %s", expectedId, detail);
    }
  }
  if (!error_[0]) report(expectedId, "start rejected; update driver for diagnostics");
  revokeStreams();
  // A rejected start may still own DMA, tasks, IRQs or a lower provider.
  if (hasQuiesce(candidate) && !candidate->quiesce()) {
    driver_ = candidate;
    report(expectedId, "hardware-quiesce-rejected");
    return false;
  }
  candidate->stop();
  closeStreams();
  return false;
}

bool ModuleV2::load(const char* path, const char* expectedId,
                    const char* expectedCapability, uint32_t expectedApi,
                    const risc_provider_dependency_v1* deps, size_t count, bool independent) {
  if (!handle_) error_[0] = 0;
  if (handle_ || !path || !path[0] ||
      !validRequest(expectedId, expectedCapability, expectedApi, deps, count)) {
    report(expectedId, "invalid-elf-request");
    return false;
  }
  state_ = State::Failed;
  (void)dlerror();
  handle_ = independent ? esp_dlopen_instance(path) : dlopen(path, RTLD_NOW);
  if (!handle_) { report(expectedId, "elf-open-failed"); return false; }
  privileged_image_ = false;
  (void)dlerror();
  auto get = reinterpret_cast<risc_driver_get_v2_fn>(dlsym(handle_, "t5_driver_get"));
  const char* error = dlerror();
  if (error || !get) report(expectedId, "elf-entry-symbol-missing");
  if (!error && activateMapped(get, expectedId, expectedCapability,
                               expectedApi, deps, count)) return true;
  if (driver_) return false;
  (void)closeMapped();
  return false;
}

bool ModuleV2::loadVerifiedBytes(const uint8_t* candidateBytes, size_t length,
                                 const uint8_t contentSha256[32],
                                 const char* const* declaredImports,
                                 size_t declaredImportCount,
                                 const char* expectedId,
                                 const char* expectedCapability,
                                 uint32_t expectedApi,
                                 const risc_provider_dependency_v1* deps,
                                 size_t count) {
  if (!handle_) error_[0] = 0;
  (void)candidateBytes; (void)length; (void)contentSha256;
  (void)declaredImports; (void)declaredImportCount;
  (void)expectedId; (void)expectedCapability; (void)expectedApi;
  (void)deps; (void)count;
  return false;
}

bool ModuleV2::poll(uint32_t budgetMs) {
  if (!budgetMs || state_ != State::Active || !driver_ || !consumers_ ||
      driver_->struct_size < sizeof(risc_driver_poll_v2)) return false;
  const auto* extended = reinterpret_cast<const risc_driver_poll_v2*>(driver_);
  if (!extended->poll) return false;
  extended->poll(budgetMs);
  return true;
}
bool ModuleV2::pinConsumer() {
  if (state_ != State::Active || consumers_ == std::numeric_limits<uint32_t>::max())
    return false;
  ++consumers_;
  return true;
}

bool ModuleV2::unpinConsumer() {
  if (!consumers_) return false;
  --consumers_;
  return true;
}

void ModuleV2::revokeStreams() {
  if (streamApi_.streams.context && !streamsRevoked_) {
    streamHost_->revoke(streamApi_.streams.context);
    streamsRevoked_ = true;
  }
}
void ModuleV2::revokeLease() {
  if (!leaseAttempted_) return;
  leaseAttempted_ = false;
  lease_.revoke(lease_.context);
}
void ModuleV2::closeStreams() {
  if (!streamApi_.streams.context) return;
  revokeStreams();
  streamHost_->close(streamApi_.streams.context);
  streamApi_ = {};
}
void ModuleV2::reportQuiescence() {
  if (!driver_) return;
  if (!error_[0] && driver_->struct_size >= sizeof(risc_driver_diagnostics_v2)) {
    const auto* diagnostics = reinterpret_cast<const risc_driver_diagnostics_v2*>(driver_);
    char detail[112]{};
    if (diagnostics->last_error && diagnostics->last_error(detail, sizeof(detail))) {
      detail[sizeof(detail)-1] = 0;
      if (detail[0]) std::snprintf(error_, sizeof(error_), "%s: %s", driver_->driver_id, detail);
    }
  }
  report(driver_->driver_id, "hardware-quiesce-rejected");
}
bool ModuleV2::unload() {
  if (consumers_) return false;
  revokeLease();
  risc_runtime_retention_guard();
  revokeStreams();
  if (state_ == State::Failed && handle_ && driver_) {
    if (!hasQuiesce(driver_) || !driver_->quiesce()) { reportQuiescence(); return false; }
    driver_->stop();
    driver_ = nullptr;
  } else if (state_ == State::Active && driver_) {
    if (hasQuiesce(driver_) && !driver_->quiesce()) {
      reportQuiescence();
      api_ = nullptr;
      state_ = State::Failed;
      return false;
    }
    driver_->stop();
    driver_ = nullptr;
  }
  closeStreams();
  api_ = nullptr;
  if (!closeMapped()) {
    state_ = State::Failed;
    return false;
  }
  state_ = State::Absent;
  return true;
}
} // namespace RuntimeProviders
