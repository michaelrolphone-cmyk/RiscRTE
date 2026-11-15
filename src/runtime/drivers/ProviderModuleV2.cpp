#include "diagnostics/Performance.h"
#include "diagnostics/StageLog.h"
#include "ProviderModuleV2.h"
#include "NativeProviderPolicyValidationV1.h"
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
#include <private/esp_privileged_elf.h>
#include <private/elf_platform.h>
#include <private/esp_elf_internal_v1.h>
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
#include <private/esp_cpu_task_scope_binding_v2.h>
#endif
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
  RISC_STAGE_LOG("provider reference id=%s stage=%s",id?id:"?",stage);
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
  if(!nativeTaskPrepareClose() || !leaseSafe())return false;
  revokeLease();
  if(!nativeTaskSafe() || !leaseSafe() || !nativeTaskRelease())return false;
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
  if (risc_runtime_retention_required() || !leaseSafe()) return false;
  const risc_driver_v2* candidate = get ? get(RISC_PROVIDER_DRIVER_ABI_V2) : nullptr;
  if(!leaseSafe())return false;
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
  bool nativeRootValid=false;
  if(candidate->struct_size>=offsetof(risc_driver_native_root_v1,extension_tag)+sizeof(uint32_t)) {
    const auto* root=reinterpret_cast<const risc_driver_native_root_v1*>(candidate);
    if(root->extension_tag==RISC_DRIVER_NATIVE_ROOT_TAG_V1) {
      if(candidate->struct_size<sizeof(*root) ||
         root->extension_version!=RISC_DRIVER_NATIVE_ROOT_VERSION_V1 ||
         !ownedPolicy_ || !root->poll.streams.bind_streams || !hasQuiesce(candidate) ||
         !lease_.context || !lease_.begin || !lease_.revoke || !lease_.safe) {
        report(expectedId,"native-root-extension-invalid");return false;
      }
      const auto* api=root->native_root;
      if(!api || api->api_version!=RISC_NATIVE_ROOT_API_V1 || api->struct_size!=sizeof(*api) ||
         !api->call || !api->health) {
        report(expectedId,"native-root-interface-invalid");return false;
      }
      nativeRootValid=true;
    }
  }
  if(ownedPolicy_ && ownedPolicy_->osCpuAbi==5 && (!nativeTaskStaging_ || !nativeRootValid)) {
    report(expectedId,"native-task-root-required");return false;
  }
  const risc_stream_session_provider_v1* sessions = nullptr;
  if (candidate->struct_size >= offsetof(risc_driver_stream_sessions_v2,extension_tag) + sizeof(uint32_t)) {
    const auto* extended = reinterpret_cast<const risc_driver_stream_sessions_v2*>(candidate);
    // Unrelated larger descriptors do not authorize reading their suffix as a
    // pointer. Inspect the explicit tag/version before touching adapter memory.
    if (extended->extension_tag == RISC_DRIVER_STREAM_SESSIONS_TAG_V1) {
      if (candidate->struct_size < sizeof(*extended) ||
          extended->extension_version != RISC_DRIVER_STREAM_SESSIONS_VERSION_V1) {
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
  if (candidate->struct_size >= offsetof(risc_driver_service_v2,extension_tag)+sizeof(uint32_t)) {
    const auto* service = reinterpret_cast<const risc_driver_service_v2*>(candidate);
    if (service->extension_tag == RISC_DRIVER_SERVICE_TAG_V1 &&
        (candidate->struct_size < sizeof(*service) ||
         service->extension_version != RISC_DRIVER_SERVICE_VERSION_V1 ||
         !service->service || !hasQuiesce(candidate))) {
      report(expectedId, "service-extension-invalid"); return false;
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
      if(nativeRootValid && !streamApi_.streams.context) {
        report(expectedId,"native-root-mapping-invalid");return false;
      }
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
    if(nativeTaskBegin(nativeRootValid)) {
      started=candidate->start(deps,count);
      started=nativeTaskEnd(started);
    }
    // Revoke failed-start authority before calling any diagnostic sink.
    if(!started)revokeLease();
    RISC_STAGE_LOG("provider start end id=%s result=%s elapsed_us=%llu",expectedId,started?"ok":"failed",
                   (unsigned long long)(RiscDiagnostics::monotonicUs()-startUs));
  } else {
    revokeLease();
    RISC_STAGE_LOG("provider start skipped id=%s reason=%s",expectedId,bound?"lease-rejected":"stream-bind-rejected");
  }
  if((!started && !nativeTaskPrepareClose()) || !leaseSafe()) {
    // Native retained storage may be reported during start itself. Preserve
    // code/dependencies without entering diagnostics, quiesce, stop or dlclose.
    revokeLease();driver_=candidate;api_=nullptr;state_=State::Failed;
    report(expectedId,"host-lease-retained");return false;
  }
  if (started) {
    driver_ = candidate;
    nativeRootValid_=nativeRootValid;
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
  if(!nativeTaskSafe() || !leaseSafe()){driver_=candidate;return false;}
  if (!error_[0]) report(expectedId, "start rejected; update driver for diagnostics");
  if (!revokeStreams()) { driver_ = candidate; report(expectedId, "stream-revoke-retained"); return false; }
  if(!nativeTaskSafe() || !leaseSafe()){driver_=candidate;return false;}
  // A rejected start may still own DMA, tasks, IRQs or a lower provider.
  if (hasQuiesce(candidate) && !candidate->quiesce()) {
    driver_ = candidate;
    report(expectedId, "hardware-quiesce-rejected");
    return false;
  }
  if(!nativeTaskSafe() || !leaseSafe()){driver_=candidate;return false;}
  candidate->stop();
  if(!nativeTaskSafe() || !leaseSafe()){driver_=candidate;return false;}
  if (!closeStreams()) { report(expectedId, "stream-close-retained"); return false; }
  return false;
}

bool ModuleV2::load(const char* path, const char* expectedId,
                    const char* expectedCapability, uint32_t expectedApi,
                    const risc_provider_dependency_v1* deps, size_t count, bool independent,
                    void* (*openImage)(void*,const char*,const void*), void* imageContext, const void* imageSource) {
  if (!handle_) error_[0] = 0;
  if (handle_ || (ownedPolicy_ && ownedPolicy_->osCpuAbi==5) || !path || !path[0] ||
      !validRequest(expectedId, expectedCapability, expectedApi, deps, count)) {
    report(expectedId, "invalid-elf-request");
    return false;
  }
  state_ = State::Failed;
  (void)dlerror();
  handle_ = openImage ? openImage(imageContext,path,imageSource) :
      independent ? esp_dlopen_instance(path) : dlopen(path, RTLD_NOW);
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
#ifdef ESP_PLATFORM
  if (handle_ || !ownedPolicy_ || (ownedPolicy_->osCpuAbi==5 && !nativeTaskStaging_) || candidateBytes != ownedImage_ ||
      !candidateBytes || length != ownedImageBytes_ ||
      !validRequest(expectedId, expectedCapability, expectedApi, deps, count) ||
      !nativePolicyMatchesModule(*ownedPolicy_, candidateBytes, length,
          contentSha256, declaredImports, declaredImportCount, expectedId,
          expectedCapability, expectedApi)) {
    report(expectedId, "native-policy-or-image-not-bound");
    return false;
  }
  state_ = State::Failed;
  if (risc_runtime_retention_required() || !leaseSafe()) {
    report(expectedId, "host-lease-retained");
    return false;
  }
  auto* snapshot = static_cast<uint8_t*>(
      heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!snapshot) snapshot = static_cast<uint8_t*>(
      heap_caps_malloc(length, MALLOC_CAP_8BIT));
  if (!snapshot) {
    report(expectedId, "elf-snapshot-oom", static_cast<int>(length));
    return false;
  }
  std::memcpy(snapshot, candidateBytes, length);
  // Verify the copy consumed by relocation, even after a prior clean unload.
  // Neither a cached digest nor changing the public load arguments is proof.
  if (!nativeProviderImageValid(*ownedPolicy_, snapshot, length)) {
    heap_caps_free(snapshot);
    report(expectedId, "native-policy-snapshot-integrity");
    return false;
  }
  auto* image = static_cast<esp_elf_t*>(std::malloc(sizeof(esp_elf_t)));
  if (!image) {
    heap_caps_free(snapshot);
    report(expectedId, "elf-handle-oom");
    return false;
  }
  trace(expectedId, "elf-relocate-begin");
  const int result =
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF
      (ownedPolicy_->osCpuAbi==5 ? esp_elf_relocate_privileged_selected_diagnostics_v5 :
       esp_elf_relocate_privileged_selected_diagnostics_v2)(
#else
      esp_elf_relocate_privileged_selected_diagnostics_v1(
#endif
      image, snapshot, length, ownedPolicy_->imports, ownedPolicy_->importCount,
      ownedPolicy_->diagnosticAbi);
  heap_caps_free(snapshot);
  if (result != 0) {
    std::free(image);
    report(expectedId, "elf-relocation-failed", result);
    return false;
  }
  handle_ = image;
  privileged_image_ = true;
  risc_driver_get_v2_fn get = nullptr;
  for (uint16_t i = 0; i < image->num; ++i) {
    if (image->symtab[i].name &&
        std::strcmp(image->symtab[i].name, "t5_driver_get") == 0) {
      get = reinterpret_cast<risc_driver_get_v2_fn>(image->symtab[i].addr);
      break;
    }
  }
  if (!get) report(expectedId, "elf-entry-symbol-missing");
  else trace(expectedId, "elf-relocated");
  if (activateMapped(get, expectedId, expectedCapability, expectedApi, deps, count))
    return true;
  if (driver_ || streamCleanupRetained_) return false;
  (void)closeMapped();
  return false;
#else
  (void)candidateBytes; (void)length; (void)contentSha256;
  (void)declaredImports; (void)declaredImportCount;
  (void)expectedId; (void)expectedCapability; (void)expectedApi;
  (void)deps; (void)count;
  return false;
#endif
}

bool ModuleV2::nativeRootReady(const NativeProviderPolicyV1* policy,uint64_t mapping) const {
  return !nativeRootRetained_ && nativeRootValid_ && policy && policy==ownedPolicy_ &&
      mapping && mapping==streamApi_.streams.context && !streamsRevoked_ &&
      !streamCleanupRetained_ && state_==State::Active && driver_ && consumers_ &&
      leaseAttempted_ && lease_.context && lease_.begin && lease_.revoke && lease_.safe &&
      leaseSafe() && streamSafe();
}
const void* ModuleV2::capability() const {
  if(ownedPolicy_ && ownedPolicy_->osCpuAbi==5 && !nativeTaskCommitted_)return nullptr;
  return state_==State::Active ? api_ : nullptr;
}
void ModuleV2::retainNativeRoot() {
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
  if(nativeTaskBinding_.token)esp_cpu_task_binding_retain_v1(nativeTaskBinding_);
#endif
  nativeRootRetained_=true;api_=nullptr;state_=State::Failed;
}
#include "ProviderModuleTaskScopeV5.inc"

bool ModuleV2::poll(uint32_t budgetMs) {
  if(ownedPolicy_ && ownedPolicy_->osCpuAbi==5 && !nativeTaskCommitted_)return false;
  if (!budgetMs || state_ != State::Active || !leaseSafe() || !streamSafe() || !driver_ || !consumers_ ||
      driver_->struct_size < sizeof(risc_driver_poll_v2)) return false;
  const auto* extended = reinterpret_cast<const risc_driver_poll_v2*>(driver_);
  if (!extended->poll) return false;
  extended->poll(budgetMs);
  return true;
}
bool ModuleV2::service(uint32_t budgetMs) {
  if(ownedPolicy_ && ownedPolicy_->osCpuAbi==5 && !nativeTaskCommitted_)return false;
  if (!budgetMs || budgetMs>RISC_DRIVER_SERVICE_MAX_MS || state_ != State::Active ||
      !leaseSafe() || !streamSafe() || !driver_ || !consumers_ || driver_->struct_size < sizeof(risc_driver_service_v2))return false;
  const auto* extended=reinterpret_cast<const risc_driver_service_v2*>(driver_);
  if(extended->extension_tag!=RISC_DRIVER_SERVICE_TAG_V1 ||
     extended->extension_version!=RISC_DRIVER_SERVICE_VERSION_V1 || !extended->service)return false;
  extended->service(budgetMs);return true;
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
  if (!driver_ || !nativeTaskSafe() || !leaseSafe()) return;
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
  if (consumers_ || streamCleanupRetained_ || nativeRootRetained_ || !nativeTaskPrepareClose() || !leaseSafe()) return false;
  auto hostSafe=[&](){
    if(nativeTaskSafe() && leaseSafe())return true;
    api_=nullptr;state_=State::Failed;return false;
  };
  revokeLease();
  if(!hostSafe())return false; // Revocation is not physical retirement.
  risc_runtime_retention_guard();
  if (!revokeStreams()) { api_ = nullptr; state_ = State::Failed; return false; }
  if(!hostSafe())return false;
  if (state_ == State::Failed && handle_ && driver_) {
    if (!hasQuiesce(driver_) || !driver_->quiesce()) { reportQuiescence(); return false; }
    if(!hostSafe())return false;
    driver_->stop();
    if(!hostSafe())return false;
    driver_ = nullptr;
  } else if (state_ == State::Active && driver_) {
    if (hasQuiesce(driver_) && !driver_->quiesce()) {
      reportQuiescence();
      api_ = nullptr;
      state_ = State::Failed;
      return false;
    }
    if(!hostSafe())return false;
    driver_->stop();
    if(!hostSafe())return false;
    driver_ = nullptr;
  }
  if (!closeStreams()) { api_ = nullptr; state_ = State::Failed; return false; }
  streamSessions_ = nullptr;
  nativeRootValid_=false;nativeTaskCommitted_=false;
  api_ = nullptr;
  if (!closeMapped()) {
    state_ = State::Failed;
    return false;
  }
  state_ = State::Absent;
  return true;
}
} // namespace RuntimeProviders
