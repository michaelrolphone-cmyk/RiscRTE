#include "diagnostics/Performance.h"
#include "diagnostics/StageLog.h"
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
#if RISC_STAGE_LOGS
constexpr size_t DetailCapacity=512;
void logDetail(const char* id,const char* detail){
  // Keep the provider's report independent of the shorter retained error field.
  // Repeating the bounded ID keeps each plain statement attributable. Even a
  // 95-byte ID plus an 80-byte part fits the 255-byte timestamped line limit.
  const size_t length=std::strlen(detail);
  for(size_t offset=0;offset<length;offset+=80){
    RISC_STAGE_LOG("provider detail id=%s part=%u text=%.80s",id?id:"?",unsigned(offset/80+1),detail+offset);
  }
  if(length==DetailCapacity-1){
    RISC_STAGE_LOG("provider detail id=%s source-buffer-full=511 report-may-be-truncated",id?id:"?");
  }
}
#else
constexpr size_t DetailCapacity=112;
#endif
} // namespace

void ModuleV2::report(const char* id, const char* stage, int code) {
  // Keep the original cause even if teardown subsequently fails.
  if (!error_[0]) std::snprintf(error_, sizeof(error_), "%s: %s rc=%d (0x%x)",
                              id ? id : "?", stage, code, static_cast<unsigned>(code));
  RISC_STAGE_LOG("provider failed id=%s reason=%s code=%d",id?id:"?",stage,code);
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
  const risc_stream_session_provider_v1* sessions = nullptr;
  if (candidate->struct_size >= offsetof(risc_driver_stream_sessions_v2,stream_sessions)) {
    const auto* extended = reinterpret_cast<const risc_driver_stream_sessions_v2*>(candidate);
    // Unrelated larger descriptors do not authorize reading their suffix as a
    // pointer. Inspect the explicit tag/version before touching adapter memory.
    if (extended->extension_tag == RISC_DRIVER_STREAM_SESSIONS_TAG_V1) {
      if (extended->extension_version != RISC_DRIVER_STREAM_SESSIONS_VERSION_V1 ||
          candidate->struct_size < sizeof(*extended)) {
        report(expectedId, "stream-session-extension-invalid"); return false;
      }
      sessions = extended->stream_sessions;
      if (!sessions || sessions->api_version != RISC_STREAM_SESSION_PROVIDER_API_V1 ||
          sessions->struct_size < sizeof(*sessions) || !sessions->open || !sessions->call ||
          !sessions->close || !extended->poll.streams.bind_streams || !hasQuiesce(candidate)) {
        report(expectedId, "stream-session-interface-invalid"); return false;
      }
    }
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
  bool started=false;
  if(admitted) {
#if RISC_STAGE_LOGS
    const auto startUs=RiscDiagnostics::monotonicUs();
#endif
    RISC_STAGE_LOG("provider start begin id=%s",expectedId);
    RiscPerf::Scope startTrace(24,25,RiscPerf::identity(expectedId));
    started=candidate->start(deps,count);
    // Revoke failed-start authority before calling any diagnostic sink.
    if(!started)revokeLease();
    RISC_STAGE_LOG("provider start end id=%s result=%s elapsed_us=%llu",expectedId,started?"ok":"failed",
                   (unsigned long long)(RiscDiagnostics::monotonicUs()-startUs));
  } else {
    revokeLease();
    RISC_STAGE_LOG("provider start skipped id=%s reason=%s",expectedId,bound?"lease-rejected":"stream-bind-rejected");
  }
  if (started) {
    driver_ = candidate;
    streamSessions_ = sessions;
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
    char detail[DetailCapacity]{};
    if (diagnostics->last_error && diagnostics->last_error(detail, sizeof(detail))) {
      detail[sizeof(detail) - 1] = 0;
#if RISC_STAGE_LOGS
      logDetail(expectedId,detail);
#endif
      if (detail[0]) std::snprintf(error_, sizeof(error_), "%s: %.111s", expectedId, detail);
    }
  }
  if (!error_[0]) report(expectedId, "start rejected; update driver for diagnostics");
  if (!revokeStreams()) { driver_ = candidate; report(expectedId, "stream-revoke-retained"); return false; }
  // A rejected start may still own DMA, tasks, IRQs or a lower provider.
  if (hasQuiesce(candidate) && !candidate->quiesce()) {
    driver_ = candidate;
    report(expectedId, "hardware-quiesce-rejected");
    return false;
  }
  candidate->stop();
  if (!closeStreams()) { report(expectedId, "stream-close-retained"); return false; }
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
  if (driver_ || streamCleanupRetained_) return false;
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

bool ModuleV2::revokeStreams() {
  if (streamApi_.streams.context && !streamsRevoked_) {
    if (streamHost_->revokeChecked) {
      if (!streamHost_->revokeChecked(streamApi_.streams.context)) { streamCleanupRetained_=true; return false; }
    } else streamHost_->revoke(streamApi_.streams.context);
    streamsRevoked_ = true;
  }
  return true;
}
void ModuleV2::revokeLease() {
  if (!leaseAttempted_) return;
  leaseAttempted_ = false;
  lease_.revoke(lease_.context);
}
bool ModuleV2::closeStreams() {
  if (!streamApi_.streams.context) return true;
  if (!revokeStreams()) return false;
  if (streamHost_->closeChecked) {
    if (!streamHost_->closeChecked(streamApi_.streams.context)) { streamCleanupRetained_=true; return false; }
  } else streamHost_->close(streamApi_.streams.context);
  streamApi_ = {};
  return true;
}
void ModuleV2::reportQuiescence() {
  if (!driver_) return;
  if (!error_[0] && driver_->struct_size >= sizeof(risc_driver_diagnostics_v2)) {
    const auto* diagnostics = reinterpret_cast<const risc_driver_diagnostics_v2*>(driver_);
    char detail[DetailCapacity]{};
    if (diagnostics->last_error && diagnostics->last_error(detail, sizeof(detail))) {
      detail[sizeof(detail)-1] = 0;
#if RISC_STAGE_LOGS
      logDetail(driver_->driver_id,detail);
#endif
      if (detail[0]) std::snprintf(error_, sizeof(error_), "%s: %.111s", driver_->driver_id, detail);
    }
  }
  report(driver_->driver_id, "hardware-quiesce-rejected");
}
bool ModuleV2::unload() {
  if (consumers_ || streamCleanupRetained_) return false;
  revokeLease();
  risc_runtime_retention_guard();
  if (!revokeStreams()) { api_ = nullptr; state_ = State::Failed; return false; }
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
  if (!closeStreams()) { api_ = nullptr; state_ = State::Failed; return false; }
  streamSessions_ = nullptr;
  api_ = nullptr;
  if (!closeMapped()) {
    state_ = State::Failed;
    return false;
  }
  state_ = State::Absent;
  return true;
}
} // namespace RuntimeProviders
