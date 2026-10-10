#pragma once
#include "Board.h"
#include "AppPolicyLimits.h"
#include "AppRequirementLimits.h"
#include "InstalledFiles.h"
#include "AppDataBackend.h"
#include "FileOpenState.h"
#include "FailureEvidenceBackend.h"
#include "runtime/sleep/RetainedWake.h"
#include <memory>
#include "runtime/drivers/ProviderGraphV2.h"
#include "runtime/streams/ProviderQueueHost.h"
#include "runtime/streams/AppStreamSessions.h"
#include <RiscRuntimeV1.h>
#include <RiscDiagnosticCheckpointV1.h>
#include <RiscResidentShellV1.h>
#include <RiscRealtimeV1.h>
#include <RiscPlatformRealtimeV1.h>
#include <RiscProviderPromotionV1.h>
#include <RiscKeyValueV1.h>
#include <RiscBoundKeyValueV1.h>
#include <RiscKeyValueV2.h>
#include <RiscBoundKeyValueV2.h>
#include <RiscBoundAppDataV1.h>
struct esp_dl_image_cache;
namespace RuntimeProviders { struct NativeProviderPolicySnapshotV1; }
namespace RiscBoot {
class Runtime;
// Optional compiled-in backend. Namespace comes only from validated boot policy.
// Backend must obey the public status/size contract; broker copies reads only
// after success. No app value encoding or fallback policy belongs here.
struct KeyValueBackend {
  void* context;
  int32_t (*get)(void*,uint32_t,const char*,void*,uint32_t,uint32_t*);
  int32_t (*put)(void*,uint32_t,const char*,const void*,uint32_t);
  uint32_t maxBlobSize=RISC_KEY_VALUE_BLOB_MAX;
};
struct Port {
  bool (*owner)();
  bool (*health)(risc_runtime_health_v1*);
  void (*delay)(uint32_t);
  bool (*log)(const char*);
  bool (*bindPlatforms)(Runtime&)=nullptr;
  const KeyValueBackend* keyValue=nullptr;
  // False means native resources must retain the invocation and provider graph.
  // This is a bounded non-mutating retention barrier, not provider quiescence.
  bool (*appExitSafe)()=nullptr;
  // Optional narrower native-retention barrier for bound provider storage.
  // Healthy RF may block app exit without revoking admitted provider KV.
  // If absent, retain the original appExitSafe behavior.
  bool (*providerStorageSafe)()=nullptr;
  // Explicit default-app health acknowledgement, never inferred from exit.
  bool (*confirmBoot)()=nullptr;
  const AppDataBackend* appData=nullptr;
  RiscRetainedWake::Store* retainedWake=nullptr;
  // Optional raw scheduler delay for an already-retained invocation owner.
  // Must not poll diagnostics/providers, inspect storage, or perform cleanup.
  // No fallback to delay: that callback may perform ordinary cooperative work.
  void (*retainedDelay)(uint32_t)=nullptr;
  // Optional native boot classification; read-only and independent of app RTC
  // records. Required at run() only for an explicit boot_start="cold" policy.
  bool (*coldBoot)()=nullptr;
  // Read-only native reset evidence. No crash interception or app recovery.
  bool (*priorFailure)(risc_resident_failure_v1*)=nullptr;
  // Trusted firmware selection only. Boot/manifest metadata cannot populate it.
  RuntimeProviders::NativeProviderPolicySetV1 nativeProviders{};
  const FailureEvidenceBackend* failureEvidence=nullptr;
  // Optional owner-only RAM copy. Must not allocate, call providers, or do I/O.
  int32_t (*diagnosticCheckpoint)(const char*,uint64_t,const char*,uint32_t)=nullptr;
};
class Runtime final {
 public:
  static constexpr size_t MaxAppPolicies=RiscLimits::Apps;
  static constexpr size_t MaxAppPolicyGrants=RISC_APP_POLICY_ROWS;
  static constexpr size_t MaxAppRequirements=RISC_APP_REQUIREMENT_ROWS;
  explicit Runtime(Port p) : port_(p), mainInvocation_(graph_,{this,streamBindingValid,streamRetain,streamYield}) {}
  ~Runtime();
  Runtime(const Runtime&)=delete;
  Runtime& operator=(const Runtime&)=delete;
  Runtime(Runtime&&)=delete;
  Runtime& operator=(Runtime&&)=delete;
  enum class Scope : uint8_t { Global, Device, Bus };
  // Compiled-in port registration only, never exported to apps/driver ELFs.
  // Tables/contexts must remain valid until successful runtime shutdown.
  bool registerPlatform(const char* capability, uint32_t api, Scope scope, uint64_t id, const void* table);
  bool registerRealtime(const risc_realtime_control_api_v1*); // compiled-in backend only
  bool prepare(const char* root);
  // Compiled-in pre-execution admission only. Enumerates the exact prepared
  // default, app-policy and driver image paths, with their expected entry role.
  // No module is mapped or executed. Duplicate instances share one inspection.
  bool inspectImages(bool (*inspect)(void*,const char* path,bool driver),void*) const;
  // Exact prepared driver only; returns the graph-owned immutable policy copy.
  const RuntimeProviders::NativeProviderPolicyV1* nativeProviderPolicyForImage(const char* absolutePath) const;
  bool uses(uint64_t instance,const char* capability,uint32_t api) const;
  bool selected(uint64_t instance) const;
  bool run();
  bool launch(const char* relative);
  bool launchDefault();
  bool health(risc_runtime_health_v1*);
  bool acquire(const char*,uint32_t,uint64_t,risc_runtime_capability_v1*);
  bool release(risc_runtime_capability_v1*);
  void yield(uint32_t);
  bool diagnostic(const char*);
  bool confirmBoot();
  bool retainInvocation();
  bool streamClient(risc_stream_client_v1*);
  bool residentClient(risc_resident_client_v1*);
  bool diagnosticCheckpointClient(risc_diagnostic_checkpoint_client_v1*);
  int32_t diagnosticCheckpointWrite(uint64_t,const char*,uint32_t);
  bool failureEvidenceClient(risc_failure_evidence_client_v1*);
  int32_t failureEvidenceRead(uint64_t,risc_failure_evidence_v1*);
  int32_t failureEvidenceAcknowledge(uint64_t,uint32_t,uint32_t);
  // Compiled-in allocation-pressure path only; no app import or grant.
  bool reclaimAppImages();
  struct UpdateApp { char elf[193]{}, manifest[193]{}; };
  // Native update authority: preserve the existing boot-policy identity/grants.
  bool appUpdate(const char* id,const void* manifest,size_t size,UpdateApp&) const;
  size_t appCount() const {return policyCount_;}
  // Compiled-in owner-task diagnostics only; no app/provider SDK export.
  // Snapshotting never invokes providers, allocates, or changes authority.
  struct GrantUsage {
    size_t selectedProviders=0, providerCapacity=0, graphCapacity=0;
    size_t graphLive=0, graphPeak=0, bootPins=0;
    size_t hostLive=0, foregroundLive=0, hostProviders=0, foregroundProviders=0;
  };
  GrantUsage grantUsage() const;
  // Native-only, side-effect-free admission of a full staged cohort. Candidate
  // code is inspected, never loaded or invoked. Caller owns candidate lifetime.
  bool validateCohort(Runtime& candidate,const char* root,
                      bool (*admit)(void*,const char*,bool provider),void* context) const;
  bool appInventory(size_t index,void*,size_t,uint32_t*) const;
  bool active() const { return invocation_->active_ && !invocation_->streams_.busy() && port_.owner(); }
  bool retained() const { return retained_; }
  // Compiled-in reset/deep-sleep admission. A suspended native stack has no
  // durable checkpoint; the foreground must finish before RAM can be lost.
  bool residentResetSafe() const {return !residentEnabled_ || (!foregroundRunning_ && !shellLoading_ && !retained_);}
  // Native metadata stream ownership is separate from mapped provider/app
  // retention. Metadata-only candidates may be destroyed after this is latched
  // by their native store owner, which must retain its mount/session.
  bool metadataCloseRetained() const { return metadataCloseRetained_; }
  // Terminal native read custody. The native caller must keep this Runtime
  // and its store/session alive; destruction or cleanup retry is unsupported.
  bool nativeProviderReadRetained() const { return retainedNativeProviderRead_!=nullptr; }
  const char* error() const { return error_; }
  Board& board() { return board_; }
  const Board& board() const { return board_; }
 private:
  bool diagnosticCheckpointToken(uint64_t) const;
  bool failureEvidenceToken(uint64_t,bool healthy) const;
  void failureBreadcrumb(uint32_t phase,const char* application=nullptr);
  struct Invocation;
  static constexpr size_t MaxDrivers=RuntimeProviders::GraphV2::kMaxModules, MaxPlatforms=32;
  using PolicyIndex=int8_t;
  static_assert(PolicyIndex(-1)<0 && MaxDrivers-1<=INT8_MAX && MaxPlatforms-1<=INT8_MAX,
                "Policy index must retain -1 and every driver/platform index");
  static_assert(MaxDrivers==RuntimeProviders::GraphV2::kMaxModules,"Driver capacity must match graph");
  static_assert(MaxAppPolicies-1<=INT8_MAX,"File handoff indices must cover every app policy");
  struct Driver {
    char id[96]{}, provides[96]{}, elf[256]{}, version[64]{};
    uint32_t api=0;
    uint64_t instance=0;
    RuntimeProviders::RequirementV2 requirements[RuntimeProviders::GraphV2::kMaxRequirements]{};
    char names[RuntimeProviders::GraphV2::kMaxRequirements][96]{};
    size_t count=0;
    bool coldBootStart=false;
  };
  bool fail(const char* reason) { if (reason != error_) snprintf(error_,sizeof(error_),"%s",reason); return false; }
  bool manifest(JsonObjectConst, Driver&);
  bool registerNativeProvider(const Driver&, RuntimeProviders::SpecV2&, const RuntimeProviders::NativeProviderPolicyV1&);
  bool validateGraph();
  void serviceProviders();
  bool runOne(const char*);
  bool residentPolicy(JsonVariantConst);
  bool residentAllowed(const char*) const;
  bool residentLegacyAllowed(const char*) const;
  bool residentToken(uint64_t,uint32_t,bool entry=true) const;
  int32_t registerShell(uint64_t,const risc_resident_callbacks_v1*);
  int32_t residentLoading(const char* absolute);
  int32_t runForeground(uint64_t,const char*,risc_resident_result_v1*);
  int32_t residentCheckpoint(uint64_t,const risc_resident_request_v1*,risc_resident_reply_v1*);
  int32_t residentExit(uint64_t);
  bool residentFailure(uint64_t,risc_resident_failure_v1*);
  void recordResidentFailure(uint32_t,int32_t,const char*,uint64_t);
  bool selectInvocation(Invocation&);
  bool appPolicies(JsonVariantConst);
  bool revokeApp();
  bool appExitBarrier();
  bool streamBinding(const risc_runtime_capability_v1*,RuntimeStreams::AppStreamBinding&) const;
  static bool streamBindingValid(void*,const RuntimeStreams::AppStreamBinding&,bool);
  static void streamRetain(void*);
  static void streamYield(void*);
  bool providerStorageSafe() const;
  bool appManifestPath(size_t,char*,size_t) const;
  static int32_t keyValueGet(void*,const char*,void*,uint32_t,uint32_t*);
  static int32_t keyValuePut(void*,const char*,const void*,uint32_t);
  struct ProviderKey {
    char key[RISC_BOUND_KEY_VALUE_KEY_MAX+1]{};
    uint32_t nameSpace=0;
    bool writable=false;
  };
  struct ProviderFile {
    char name[RISC_APP_DATA_NAME_MAX+1]{};
    uint32_t nameSpace=0;
    bool writable=false;
  };
  struct ProviderStorage {
    Runtime* owner=nullptr;
    static constexpr size_t MaxKeys=10;
    ProviderKey keys[MaxKeys]{};
    size_t count=0;
    risc_bound_key_value_v1 table{};
    MetadataArray<ProviderFile> files;
    size_t fileCount=0;
    risc_bound_app_data_v1 fileTable{};
    risc_platform_realtime_api_v1 realtime{};
    bool needsRealtime=false;
    bool live=false;
  };
  bool providerPolicy(JsonObjectConst,ProviderStorage&);
  bool providerFilePolicy(JsonObjectConst,ProviderStorage&);
  void revokeProviders();
  static bool providerFileSafe(void*);
  static ProviderStorage* providerFileContext(void*,int32_t&);
  static int32_t boundAppDataStat(void*,const char*,uint32_t*,uint64_t*);
  static int32_t boundAppDataRead(void*,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
  static int32_t boundAppDataReplace(void*,const char*,uint64_t,const void*,uint32_t);
  int32_t providerFileResult(int32_t);
  static bool beginProvider(void*);
  static void revokeProvider(void*);
  static ProviderStorage* providerContext(void*);
  static int32_t boundKeyValueGet(void*,const char*,void*,uint32_t,uint32_t*);
  static int32_t boundKeyValuePut(void*,const char*,const void*,uint32_t);
  struct AppGrantPolicy {
    // The names below point only to this Runtime's already validated fixed
    // driver/platform tables, or the canonical KV literal. Never parsed JSON.
    // Runtime is nonmovable and provider metadata is immutable after prepare.
    const char* capability=nullptr;
    uint32_t api=0; uint64_t instance=0;
    PolicyIndex driver=-1, platform=-1; bool keyValue=false, installedFiles=false, fileOpen=false;
  };
#if UINTPTR_MAX == UINT32_MAX
  static_assert(sizeof(AppGrantPolicy)==24,"App policy target layout changed");
#endif
  bool configureInstalledFiles(JsonObjectConst);
  static Runtime* volumeContext(void*,bool diagnostic=false);
  risc_storage_volume_api_v1 volumeTable(void*);
  static constexpr PolicyIndex AppDataDriver=-2, RetainedWakeDriver=-3, RealtimeDriver=-4, RealtimeControlDriver=-5, PromotionDriver=-6;
  const risc_realtime_control_api_v1* realtimeBackend_=nullptr;
  risc_platform_realtime_api_v1 providerRealtimeTable_{};
  bool registerProviderRealtime();
  static int32_t providerRealtimeRead(void*,risc_realtime_snapshot_v1*);
  int32_t readRealtime(risc_realtime_snapshot_v1*);
  static Runtime* realtimeContext(void*,bool control);
  static int32_t realtimeSeed(void*,int64_t,uint32_t);
  static int32_t realtimeRead(void*,risc_realtime_snapshot_v1*);
  static Runtime* retainedWakeContext(void*);
  static int32_t retainedWakeRead(void*,uint32_t,uint32_t,risc_retained_wake_record_v1*,uint32_t*);
  static int32_t retainedWakeStage(void*,const risc_retained_wake_record_v1*);
  static int32_t retainedWakeReadBytes(void*,uint32_t,uint32_t,void*,uint32_t,uint32_t*,uint32_t*);
  static int32_t retainedWakeStageBytes(void*,uint32_t,uint32_t,const void*,uint32_t);
  static int32_t retainedWakeClear(void*);
  bool retainedWakeIdentity(RiscRetainedWake::Identity&) const;
  char retainedCohort_[512]{};
  bool appDataExitSafe()const;
  static Runtime* appDataContext(void*);
  static int32_t appDataStat(void*,const char*,uint32_t*,uint64_t*);
  static int32_t appDataRead(void*,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
  static int32_t appDataReplace(void*,const char*,uint64_t,const void*,uint32_t);
  struct AppPolicy {
    char id[96]{}, version[64]{}, elf[256]{};
    AppGrantPolicy grants[MaxAppPolicyGrants]{}; size_t count=0;
  };
  MetadataArray<AppPolicy> policies_;
  MetadataArray<FileOpenMetadata> fileHandlers_;
  static const t5_file_open_api_v1* fileOpenApi();
  bool fileOpenReady() const;
  uint32_t fileHandlerCount(const char*) const;
  bool fileHandlerGet(const char*,uint32_t,t5_file_handler_t*) const;
  bool fileOpenRequest(const char*,const char*,uint64_t);
  bool fileOpenTakeResult(int32_t*,uint64_t*);
  bool fileSourcePathGet(char*,size_t) const;
  bool fileOpenAfterRun(bool);
  size_t policyCount_=0;
  struct AppGrant {
    RuntimeProviders::GrantV2 provider{}; const void* api=nullptr;
    uint64_t invocation=0;
    uint32_t generation=0; bool live=false;
    uint32_t keyValueNamespace=0; risc_key_value_v1 keyValue{};
  };
  uint32_t grantGeneration_=0;
  bool providerFileRetained_=false;
  struct Platform {
    char capability[96]{}; uint32_t api=0; Scope scope=Scope::Global;
    uint64_t id=0; const void* table=nullptr;
  } platforms_[MaxPlatforms]{};
  size_t platformCount_=0;
  Port port_;
  RuntimeProviders::NativeProviderPolicySnapshotV1* nativePolicies_=nullptr;
  FILE* retainedNativeProviderRead_=nullptr; // Deliberately never closed after lost native custody.
  uint8_t* retainedNativeProviderBytes_=nullptr; // May remain borrowed by uncertain native I/O.
  Board board_;
  // Must outlive graph destruction, including retained-module retry/abort.
  ProviderStorage providerStorage_[MaxDrivers]{};
  RuntimeProviders::GraphV2 graph_{RuntimeStreams::runtimeProviderStreamHost()};
  // A live record is never moved: returned API table addresses remain stable.
  // Exactly one record has ordinary callback authority on the owner task.
  struct Invocation {
    Invocation(RuntimeProviders::GraphV2& graph,RuntimeStreams::AppStreamSessions::Host host):streams_(graph,host){}
    const AppPolicy* appPolicy_=nullptr;
    AppGrant appGrants_[16]{};
    RuntimeStreams::AppStreamSessions streams_;
    std::unique_ptr<InstalledFiles> installedFiles_;
    risc_storage_volume_api_v1 installedVolume_{};
    void* installedVolumeContext_=nullptr;
    risc_app_data_v1 appDataTable_{};
    void* appDataContext_=nullptr;
    uint32_t appDataNamespace_=0;
    risc_realtime_api_v1 realtimeTable_{};
    void* realtimeContext_=nullptr;
    risc_realtime_control_api_v1 realtimeControlTable_{};
    void* realtimeControlContext_=nullptr;
#if RISC_RETAINED_WAKE_BYTES > 128
    risc_retained_wake_api_v1_extended retainedWakeTable_{};
#else
    risc_retained_wake_api_v1 retainedWakeTable_{};
#endif
    void* retainedWakeContext_=nullptr;
    risc_provider_promotion_api_v1 promotionTable_{};
    void* promotionContext_=nullptr;
    FileOpenState fileOpen_{};
    char current_[256]{},queued_[256]{};
    bool active_=false,defaultRunning_=false,entryRunning_=false;
    bool residentPolicyPending_=false;
    uint64_t token=0;
    uint32_t role=0;
    uint32_t failureKind=RISC_RESIDENT_FAILURE_NONE;
    int32_t status=RISC_RESIDENT_OK;
    void* module=nullptr;
  };
  Invocation mainInvocation_;
  Invocation* invocation_=&mainInvocation_;
  std::unique_ptr<Invocation> foreground_;
  bool residentEnabled_=false,foregroundRunning_=false,shellDispatching_=false,foregroundExit_=false;
  bool shellLoading_=false;
  bool residentForeground_[MaxAppPolicies]{};
  bool residentLegacy_[MaxAppPolicies]{};
  // Copied navigation state crosses a clean host unload. No ELF pointers.
  char residentContinuation_[256]{};
  FileOpenState residentTransferFileOpen_{};
  risc_resident_callbacks_v1 shellCallbacks_{};
  uint64_t shellToken_=0;
  risc_resident_failure_v1 lastFailure_{};

  RuntimeProviders::GrantV2 grants_[MaxDrivers]{};
  Driver drivers_[MaxDrivers]{};
  size_t driverCount_=0;
  char root_[256]{}, default_[256]{}, error_[192]{};
  bool registrationOpen_=false;
  bool demandActivation_=false;
  bool demandRetention_=false;
  bool promotionRunning_=false;
  bool promotionSafe() const;
  static int32_t promoteProviders(void*);
  bool prepared_=false, attempted_=false, retained_=false;
  bool yielding_=false;
  mutable bool metadataCloseRetained_=false;
  esp_dl_image_cache* appImages_=nullptr;
  bool reclaimingAppImages_=false;
};
}
