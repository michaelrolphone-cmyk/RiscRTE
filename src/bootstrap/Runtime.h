#pragma once
#include "RiscMemoryHeapV1.h"
#include "RiscRandomV1.h"
#include <RiscAppCatalogV1.h>
#include <RiscInstallationV1.h>
#include <RiscInstallationMediaV1.h>
#include "Board.h"
#include "AppPolicyLimits.h"
#include "AppRequirementLimits.h"
#include "InstalledFiles.h"
#include "AppDataBackend.h"
#include "AtomicKeyValueConfig.h"
#include "PreferenceStoreConfig.h"
#if RISC_RUNTIME_PREFERENCE_STORE
#include <RiscPreferenceStoreV1.h>
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
#include <RiscKeyValueAtomicV1.h>
#include <RiscKeyValueAvailabilityV1.h>
namespace RiscExperimental { class NativeAtomicKeyValue; }
#endif
#include <RiscBoundAppDataAsyncV1.h>
#include "TcpListenerBackend.h"
#include "EntropyBackend.h"
#include "runtime/storage/AppDataExport.h"
#include "runtime/storage/SdFirstKeyValue.h"
#include "runtime/storage/ScalableSettings.h"
#include "FileOpenState.h"
#include "FileSourceBackend.h"
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
namespace RiscUpdate { struct NativeAppDataExportPolicyV1; struct NativeAppDataExportPolicyV2; }
namespace RiscExperimental { class NativeRuntimeStoragePin; }
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
  int32_t (*journalRead)(void*,uint32_t,void*,uint32_t,uint32_t*)=nullptr;
  int32_t (*journalWrite)(void*,uint32_t,const void*,uint32_t)=nullptr;
  int32_t (*legacyNamespace)(void*,uint32_t,bool*)=nullptr;
  const RiscStorage::ScalableSettings::Backend* scalableSettings=nullptr;
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
  // Temporary native-resource exclusion, never retention or context revocation.
  bool (*providerServicesReady)()=nullptr;
  bool (*providerServiceBegin)()=nullptr;
  void (*providerServiceEnd)()=nullptr;
  // Optional cheap owner clock; older ports retain the health fallback.
  bool (*monotonicMs)(uint32_t*)=nullptr;
  int32_t (*memoryHeap)(risc_memory_heap_snapshot_v1*)=nullptr;
  int32_t (*randomBytes)(void*,uint32_t)=nullptr;
  // Compiled-in control, never app-writable. End acknowledges only completed
  // teardown. A retained or nonreturning invocation keeps its native deadline.
  void (*controlBegin)(bool child)=nullptr;
  bool (*controlEnd)()=nullptr;
  bool (*controlExitRequested)()=nullptr;
#if RISC_NATIVE_SETTINGS_LIFECYCLE
  // Explicit native owner checkpoint. Runs outside provider service leases;
  // false means observed terminal custody, never temporary Busy. Accepted work
  // advances one step; the native coordinator fences new atomic snapshots.
  // admitNew=false fences new work while a host/foreground atomic grant can
  // hold an edit snapshot. It never suppresses polling accepted custody.
  bool (*nativeOwnerCheckpoint)(bool admitNew)=nullptr;
  // Bounded accepted-work/discovery polling: ordinary yield never starts
  // maintenance. Shutdown drains before any graph teardown.
  bool (*nativeDiscoveryCheckpoint)()=nullptr;
  bool (*nativeSettingsShutdown)()=nullptr;
#endif
  // Optional native-owned private scratch, never an app-selected path.
  const FileSourceBackend* fileSource=nullptr;
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
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  // Trusted firmware only, before prepare or in bindPlatforms. Copies app ID;
  // endpoint/namespace gate must outlive all invocations and successful shutdown.
  bool registerAtomicKeyValue(const char* appId,uint32_t nameSpace,RiscExperimental::NativeAtomicKeyValue&);
  // Optional namespace routing for already-admitted KV1/KV2 readers. All
  // legacy writes into that namespace are denied; no implicit intent/fallback.
  bool registerAtomicKeyValueReadThrough(uint32_t nameSpace,RiscExperimental::NativeAtomicKeyValue&);
  struct KeyValueAvailability {
    void* context=nullptr;
    int32_t (*snapshot)(void*,uint32_t,risc_key_value_availability_state_v1*)=nullptr;
  };
  // Optional RAM-only availability gate, bound before prepare to an exact
  // read-through namespace. Missing registration preserves the old contract.
  bool registerKeyValueAvailability(uint32_t nameSpace,KeyValueAvailability);
  // Owner checkpoint only, no callbacks/I/O. Includes host, foreground and
  // bound-provider KV grants. Status-only grants never hold a data snapshot.
  bool keyValueReconciliationSafe(uint32_t nameSpace) const;
  bool keyValueRetentionObserved() const; // out-of-line native/host boundary
#endif
  bool registerEntropy(const EntropyBackend*); // provider-only, no generation during admission
  bool registerTcpListener(const TcpListenerBackend*); // compiled-in opt-in only
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
  bool monotonicMs(uint32_t*);
  bool acquire(const char*,uint32_t,uint64_t,risc_runtime_capability_v1*);
  bool release(risc_runtime_capability_v1*);
  void yield(uint32_t);
  bool diagnostic(const char*);
  bool confirmBoot();
  bool exitRequested()const;
  bool retainInvocation();
  bool streamClient(risc_stream_client_v1*);
  bool residentClient(risc_resident_client_v1*);
  bool diagnosticCheckpointClient(risc_diagnostic_checkpoint_client_v1*);
  int32_t diagnosticCheckpointWrite(uint64_t,const char*,uint32_t);
  bool initialDefault();
  bool takeLaunchFailure(risc_runtime_launch_failure_v1*);
  bool failureEvidenceClient(risc_failure_evidence_client_v1*);
  int32_t failureEvidenceRead(uint64_t,risc_failure_evidence_v1*);
  int32_t failureEvidenceAcknowledge(uint64_t,uint32_t,uint32_t);
  // Compiled-in allocation-pressure path only; no app import or grant.
  bool reclaimAppImages();
  struct UpdateApp { char elf[193]{}, manifest[193]{}; };
  // Native update authority: preserve the existing boot-policy identity/grants.
  bool appUpdate(const char* id,const void* manifest,size_t size,UpdateApp&) const;
  size_t appCount() const {return policyCount_;}
  // Compact installed inventory excludes dormant authority and retired slots.
  size_t appInventoryCount() const;
  bool appInventoryEntry(size_t,void*,size_t,uint32_t*) const;
  // Trusted native installation coordinator only; never an app/provider import.
  // Reserve before run(), then admit one copied SD manifest and owner-resolved
  // policy at a default-app checkpoint. Existing identities cannot be replaced
  // while invocations may retain pointers. Driver graph authority is unchanged.
  bool reserveInstalledApps(size_t capacity);
  bool admitInstalledApp(const char* policyJson,const char* manifestJson);
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
  struct AppDataExportDelegation {
    const char* consumer=nullptr;
    const char* label=nullptr;
    const RiscStorage::AppDataExport::Entry* entries=nullptr;
    size_t count=0;
  };
  // Optional trusted native-only initial delegation. Exact consumed maps only;
  // ordinary updates cannot add, widen, remove or reassign export authority.
  // Boot/manifest data never creates this authority for an existing cohort.
  bool validateCohort(Runtime& candidate,const char* root,
                      bool (*admit)(void*,const char*,bool provider),void* context,
                      const AppDataExportDelegation* delegations=nullptr,
                      size_t delegationCount=0) const;
  // Production native-only entry: exact build-owned source/target coordinates
  // may introduce the selected first export. Preserving updates use no grant.
  bool validateNativeCohort(Runtime& candidate,const char* root,
                            bool (*admit)(void*,const char*,bool provider),void* context,
                            const RiscUpdate::NativeAppDataExportPolicyV1&) const;
  bool validateNativeCohort(Runtime& candidate,const char* root,
                            bool (*admit)(void*,const char*,bool),void* context,
                            const RiscUpdate::NativeAppDataExportPolicyV2&) const;
  bool appInventory(size_t index,void*,size_t,uint32_t*) const;
  bool active() const { return
#if RISC_NATIVE_SETTINGS_LIFECYCLE
    !nativeCheckpointRunning_ &&
#endif
    invocation_->active_ && !invocation_->streams_.busy() && port_.owner(); }
  bool retained() const { return retained_; }
  // Compiled-in reset/deep-sleep admission. A suspended native stack has no
  // durable checkpoint; the foreground must finish before RAM can be lost.
  bool residentResetSafe() const {return
#if RISC_NATIVE_SETTINGS_LIFECYCLE
    !nativeCheckpointRunning_ &&
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
    atomicExitSafe(true) &&
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
    preferenceExitSafe(true) &&
#endif
    fileSourceExitSafe() && !fileSource_.token && appDataExportExitSafe() && (!residentEnabled_ || (!foregroundRunning_ && !shellLoading_ && !retained_));}
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
  friend class ::RiscExperimental::NativeRuntimeStoragePin; // compiled-in custody only, never an SDK import
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
    Runtime* sourceOwner=nullptr;uint64_t sourceVolume=0;int sourceDriver=-1;
    char sourcePath[193]{};bool internalFallback=false;
  };
  bool fail(const char* reason) { if (reason != error_) snprintf(error_,sizeof(error_),"%s",reason); return false; }
  bool manifest(JsonObjectConst, Driver&);
  bool registerNativeProvider(const Driver&, RuntimeProviders::SpecV2&, const RuntimeProviders::NativeProviderPolicyV1&);
  bool validateGraph();
  static void* openDriverImage(void*,const char*,const void*);
  void serviceProviders();
#if RISC_NATIVE_SETTINGS_LIFECYCLE
  void serviceNativeSettings(bool discoveryOnly=false);
#endif
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
  bool appPolicies(JsonVariantConst,JsonVariantConst suppliedManifest=JsonVariantConst(),size_t replacement=MaxAppPolicies);
  bool admitInstalledAppAt(const char*,const char*,size_t);
  bool revokeApp();
  bool appExitBarrier(bool exiting=true);
  bool streamBinding(const risc_runtime_capability_v1*,RuntimeStreams::AppStreamBinding&) const;
  static bool streamBindingValid(void*,const RuntimeStreams::AppStreamBinding&,bool);
  static void streamRetain(void*);
  static void streamYield(void*);
  bool providerStorageSafe() const;
  bool appManifestPath(size_t,char*,size_t) const;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  struct AtomicBinding {
    char app[96]{};uint32_t nameSpace=0;
    RiscExperimental::NativeAtomicKeyValue* endpoint=nullptr;
    KeyValueAvailability availability{};
  };
  #ifndef RISC_RUNTIME_ATOMIC_BINDING_CAPACITY
#define RISC_RUNTIME_ATOMIC_BINDING_CAPACITY 8
#endif
  static_assert(RISC_RUNTIME_ATOMIC_BINDING_CAPACITY>=2 && RISC_RUNTIME_ATOMIC_BINDING_CAPACITY<=64,"Invalid atomic binding capacity");
  AtomicBinding atomicBindings_[RISC_RUNTIME_ATOMIC_BINDING_CAPACITY]{};size_t atomicBindingCount_=0;
  static constexpr PolicyIndex AtomicKeyValueDriver=-11;
  static constexpr PolicyIndex KeyValueAvailabilityDriver=-12;
  const KeyValueAvailability* keyValueAvailability(uint32_t) const;
  bool keyValueAuthoritative(uint32_t) const;
  static int32_t keyValueAvailabilitySnapshot(void*,risc_key_value_availability_state_v1*);
  AtomicBinding* atomicBinding(const char*,uint32_t);
  RiscExperimental::NativeAtomicKeyValue* atomicReadThrough(uint32_t) const;
  int32_t atomicLegacyGet(uint32_t,const char*,void*,uint32_t,uint32_t*,uint32_t);
  bool atomicExitSafe(bool exiting) const;
  struct AppGrant;
  static AppGrant* atomicContext(void*);
  int32_t atomicResult(int32_t);
  static int32_t atomicState(void*,risc_key_value_atomic_state_v1*);
  static int32_t atomicGet(void*,uint64_t,const char*,void*,uint32_t,uint32_t*);
  static int32_t atomicSubmit(void*,uint64_t,const risc_key_value_atomic_mutation_v1*,uint32_t,const void*,uint32_t,uint64_t*);
  static int32_t atomicCollect(void*,uint64_t,risc_key_value_atomic_receipt_v1*);
  static int32_t atomicCancel(void*,uint64_t);
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
  struct PreferenceNamespace {uint32_t id=0;uint64_t revision=1;void* pending=nullptr;bool executing=false;};
  PreferenceNamespace preferenceNamespaces_[64]{};
  uint64_t preferenceOperationGeneration_=0;
  static constexpr PolicyIndex PreferenceStoreDriver=-14;
  struct AppGrant;
  PreferenceNamespace* preferenceNamespace(uint32_t,bool create=false);
  bool preferenceAvailable(const char*,uint32_t);
  bool preferenceExitSafe(bool) const;
  static AppGrant* preferenceContext(void*);
  static int32_t preferenceState(void*,risc_preference_store_state_v1*);
  static int32_t preferenceGet(void*,uint64_t,const char*,void*,uint32_t,uint32_t*);
  static int32_t preferenceSubmit(void*,uint64_t,const risc_preference_store_mutation_v1*,uint32_t,const void*,uint32_t,uint64_t*);
  static int32_t preferenceCollect(void*,uint64_t,risc_preference_store_receipt_v1*);
  static int32_t preferenceCancel(void*,uint64_t);
#endif
  int32_t namespaceKeyValueGet(uint32_t,const char*,void*,uint32_t,uint32_t*,uint32_t);
  int32_t namespaceKeyValuePut(uint32_t,const char*,const void*,uint32_t,bool preference=false,bool provider=false);
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
    risc_bound_app_data_async_v1 fileTable{};
    uint64_t fileOperation=0;
    risc_platform_realtime_api_v1 realtime{};
    bool needsRealtime=false;
    risc_tcp_listener_v1 tcp{};
    bool needsTcp=false;
    risc_entropy_v1 entropy{};
    bool needsEntropy=false;
    bool live=false;
  };
  const EntropyBackend* entropyBackend_=nullptr;
  risc_entropy_v1 providerEntropyTable_{};
  bool entropyRetained_=false,entropyBusy_=false;
  static int32_t entropyFill(void*,void*,uint32_t);
  const TcpListenerBackend* tcpBackend_=nullptr;
  risc_tcp_listener_v1 providerTcpTable_{};
  bool tcpRetained_=false;
  static ProviderStorage* tcpContext(void*,bool closing=false);
  static bool providerResourcesSafe(void*);
  static int32_t tcpListen(void*,const risc_tcp_listen_v1*,uint64_t*);
  static int32_t tcpAccept(void*,uint64_t,uint64_t*);
  static int32_t tcpRead(void*,uint64_t,void*,uint32_t,uint32_t*);
  static int32_t tcpWrite(void*,uint64_t,const void*,uint32_t,uint32_t*);
  static int32_t tcpClose(void*,uint64_t);
  static int32_t tcpConnect(void*,const risc_tcp_listen_v1*,uint64_t*);
  static int32_t tcpConnected(void*,uint64_t);
  int32_t tcpResult(int32_t);
  bool providerPolicy(JsonObjectConst,ProviderStorage&);
  bool providerFilePolicy(JsonObjectConst,ProviderStorage&);
  void revokeProviders();
  static bool providerFileSafe(void*);
  static ProviderStorage* providerFileContext(void*,int32_t&);
  static int32_t boundAppDataStat(void*,const char*,uint32_t*,uint64_t*);
  static int32_t boundAppDataRead(void*,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
  static int32_t boundAppDataReplace(void*,const char*,uint64_t,const void*,uint32_t);
  static int32_t boundAppDataBeginReplace(void*,const char*,uint64_t,const void*,uint32_t,uint64_t*);
  static int32_t boundAppDataCollectReplace(void*,uint64_t,int32_t*);
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
    PolicyIndex driver=-1, platform=-1; bool keyValue=false, installedFiles=false, fileOpen=false, fileOpenSource=false;
    char sharedFile[RISC_APP_DATA_NAME_MAX+1]{};
  };
  static bool keyValuePolicy(const AppGrantPolicy& g) {
    return g.keyValue
#if RISC_RUNTIME_PREFERENCE_STORE
      || g.driver==PreferenceStoreDriver
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
      || g.driver==AtomicKeyValueDriver
#endif
#endif
      ;
  }
#if UINTPTR_MAX == UINT32_MAX
  static_assert(sizeof(AppGrantPolicy)==72,"App policy target layout changed");
#endif
  bool configureInstalledFiles(JsonObjectConst);
  static Runtime* volumeContext(void*,bool diagnostic=false);
  risc_storage_volume_api_v1 volumeTable(void*);
  static constexpr PolicyIndex AppDataDriver=-2, RetainedWakeDriver=-3, RealtimeDriver=-4, RealtimeControlDriver=-5, PromotionDriver=-6, AppDataExportDriver=-7, SharedDataDriver=-8, MemoryHeapDriver=-9,RandomDriver=-10,AppCatalogDriver=-13;
  // These are native dispatch tags, not provider slots or public ABI values.
  // Optional brokers must never alias a tag used by an always-built service.
  static_assert([]() constexpr {
    constexpr PolicyIndex tags[]={AppDataDriver,RetainedWakeDriver,RealtimeDriver,
      RealtimeControlDriver,PromotionDriver,AppDataExportDriver,SharedDataDriver,
      MemoryHeapDriver,RandomDriver,AppCatalogDriver,
#if RISC_RUNTIME_PREFERENCE_STORE
      PreferenceStoreDriver,
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
      AtomicKeyValueDriver,KeyValueAvailabilityDriver,
#endif
    };
    for(size_t i=0;i<sizeof(tags)/sizeof(tags[0]);++i) {
      if(tags[i]>=-1)return false;
      for(size_t j=0;j<i;++j)if(tags[i]==tags[j])return false;
    }
    return true;
  }(),"Native app capability dispatch tags must be distinct and negative");
  static int32_t randomFill(void*,void*,uint32_t);
  static int32_t memoryHeapSnapshot(void*,risc_memory_heap_snapshot_v1*);
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
  bool configureAppDataExports(JsonVariantConst);
  bool appDataExportExitSafe() const;
  bool acquireAppDataExport();
  bool endAppDataExport();
  static bool appDataExportOwner(void*);
  static bool appDataExportSafe(void*);
  bool validateAppDataExportCohort(const Runtime&,const AppDataExportDelegation*,size_t,bool exactMigration=false) const;
  bool validateCohortImpl(Runtime&,const char*,bool (*)(void*,const char*,bool),void*,
                         const AppDataExportDelegation*,size_t,
                         const RiscUpdate::NativeAppDataExportPolicyV1*,
                         const RiscUpdate::NativeAppDataExportPolicyV2* =nullptr) const;
  bool needsAppDataExportDelegation(const Runtime&) const;
  bool appDataExitSafe()const;
  bool appDataBusy()const;
  static Runtime* appDataContext(void*);
  static int32_t appDataStat(void*,const char*,uint32_t*,uint64_t*);
  static int32_t appDataRead(void*,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
  static int32_t appDataReplace(void*,const char*,uint64_t,const void*,uint32_t);
  struct AppCatalogMetadata {
    char displayName[96]{}, minFirmware[32]{}, icon[24]{};
    bool visible=true;
  };
  static void appCatalogMetadata(JsonObjectConst,AppCatalogMetadata&);
  static Runtime* appCatalogEnter(void*,int32_t&);
  static int32_t appCatalogSnapshot(void*,risc_app_catalog_snapshot_v1*);
  static int32_t appCatalogGet(void*,uint64_t,uint32_t,risc_app_catalog_entry_v1*);
  void appCatalogChanged();
  bool appCatalogRefresh(uint32_t&);
  uint64_t appCatalogGeneration_=1;
  // Zero means omitted; one means admitted but launch-unavailable; two ready.
  uint8_t appCatalogState_[MaxAppPolicies]{};
  struct AppPolicy {
    char id[96]{}, version[64]{}, elf[256]{};
    bool enabled=true, catalogAuthority=false;
    AppCatalogMetadata catalog{};
    char sourcePath[193]{}; PolicyIndex sourceDriver=-1;
    AppGrantPolicy grants[MaxAppPolicyGrants]{}; size_t count=0;
    const RiscStorage::AppDataExport::Entry* exports=nullptr;
    size_t exportCount=0;
    char exportLabel[64]{};
  };
  MetadataArray<AppPolicy> policies_;
  size_t policyCapacity_=0, bootPolicyCount_=0;
  struct InstalledAppMetadata {char manifest[4097]{};uint32_t namespaceSlot=0;};
  MetadataArray<InstalledAppMetadata> installedMetadata_;
  struct InstallationState {
    risc_installation_media_v1 api{};uint64_t volume=0,internalVolume=0;bool internalEnabled=false;uint32_t medium=0;char grants[4097]{},providerGrants[4097]{};bool providersEnabled=false;uint32_t namespaceFirst=0,namespaceCount=0,namespaceSlot=0;bool call=false;
  };
  MetadataArray<InstallationState> installation_;
  MetadataArray<RiscStorage::SdFirstKeyValue::State> installedSettings_;
  RiscStorage::ScalableSettings::Collection scalableSettings_, sharedSettings_;
  MetadataArray<uint32_t> sharedSettingsNamespaces_;
  RiscStorage::ScalableSettings::State* scalableSettingsState(uint32_t)const;
  uint32_t settingsNamespace(uint32_t slot)const;
  static uint32_t installationSharedSettingsNamespace(void*,uint32_t);
  MetadataArray<RiscStorage::SdFirstKeyValue::Workspace> installedSettingsWorkspace_;
  RiscStorage::SdFirstKeyValue::Journal settingsJournal();
  static uint32_t installationSettingsStatus(void*,uint32_t);
  static int32_t installationSettingsRestore(void*,uint32_t,const char*,const void*,uint32_t,bool);
  static int32_t installationSettingsSnapshot(void*,uint32_t,void*,uint32_t,uint32_t*,uint64_t*);
  static int32_t installationSettingsAck(void*,uint32_t,uint64_t);
  bool configureInstallation(JsonVariantConst);
  bool installationNamespaceRangeSafe() const;
  static int32_t installationNamespaced(void*,const risc_install_candidate_v1*,const uint32_t*,uint32_t,risc_install_result_v1*,bool,bool);
  static int32_t installationAdmit(void*,const char*,const char*,char*,uint32_t);
  static int32_t installationForget(void*,const char*,bool);
  static int32_t installationCommit(void*,const risc_install_candidate_v1*,uint32_t,risc_install_result_v1*,bool);
  static int32_t installationCheck(void*,const risc_install_candidate_v1*,uint32_t,risc_install_result_v1*,bool);
  static int32_t installationTransaction(void*,const risc_install_candidate_v1*,uint32_t,risc_install_result_v1*,bool,bool,const uint32_t* slots=nullptr,const uint32_t* media=nullptr);
  bool installationMutable(size_t)const;
  static int32_t installationProviders(void*,const risc_install_provider_v1*,uint32_t,bool);
  static int32_t installationProvidersMedia(void*,const risc_install_provider_v1*,const uint32_t*,uint32_t,bool);
  static int32_t installationProviderTransaction(void*,const risc_install_provider_v1*,const uint32_t*,uint32_t,bool);
  static int32_t installationNamespacedMedia(void*,const risc_install_candidate_v1*,const uint32_t*,const uint32_t*,uint32_t,risc_install_result_v1*,bool,bool);
  static bool installationAvailable(void*,const char*,uint32_t,uint64_t);
  bool installedLaunchPath(const char*,char*,size_t,bool* settingsPending=nullptr)const;
  void* loadRemovableApp(const AppPolicy&);
  RuntimeProviders::GrantV2 retainedAppSource_{};
  MetadataArray<RiscStorage::AppDataExport::Entry> exportEntries_[MaxAppPolicies];
  MetadataArray<FileOpenMetadata> fileHandlers_;
  static const t5_file_open_api_v1* fileOpenApi();
  bool fileOpenReady() const;
  uint32_t fileHandlerCount(const char*) const;
  bool fileHandlerGet(const char*,uint32_t,t5_file_handler_t*) const;
  bool fileOpenRequest(const char*,const char*,uint64_t);
  bool fileOpenTakeResult(int32_t*,uint64_t*);
  bool fileSourcePathGet(char*,size_t) const;
  bool fileOpenAfterRun(bool);
  static const risc_file_open_source_api_v1* fileOpenSourceApi();
  bool fileSourceReady() const;
  bool fileSourceExitSafe() const;
  bool fileSourceSupported(size_t) const;
  bool fileSourceSupported(const char*) const;
  uint32_t fileSourceHandlerCount(const char*) const;
  bool fileSourceHandlerGet(const char*,uint32_t,t5_file_handler_t*) const;
  bool fileSourceOwned(uint64_t) const;
  int32_t fileSourceBegin(const char*,uint64_t,uint64_t*);
  int32_t fileSourceWrite(uint64_t,const void*,uint32_t,uint32_t*);
  int32_t fileSourceFinish(uint64_t);
  int32_t fileSourceDiscard(uint64_t);
  bool fileSourceRequest(uint64_t,const char*,uint64_t);
  bool fileSourceTakeResult(int32_t*,uint64_t*);
  int32_t fileSourceInfo(risc_file_open_source_info_v1*) const;
  int32_t fileSourceRead(uint64_t,uint64_t,void*,uint32_t,uint32_t*);
  int32_t fileSourceStatus(int32_t);
  bool fileSourceCleanup();
  struct SourceState {
    char name[RISC_FILE_OPEN_SOURCE_NAME_MAX]{};
    uint64_t token=0,size=0,written=0,ownerInvocation=0;
    PolicyIndex owner=-1;
    bool sealed=false;
  } fileSource_;
  uint64_t fileSourceGeneration_=0,fileSourceDiscarded_=0,fileSourceDiscardedOwner_=0;
  size_t policyCount_=0;
  struct AppGrant {
    RuntimeProviders::GrantV2 provider{}; const void* api=nullptr;
    uint64_t invocation=0;
    uint32_t generation=0; bool live=false;
    uint32_t keyValueNamespace=0; risc_key_value_v1 keyValue{};
    risc_memory_heap_api_v1 memoryHeap{};risc_random_api_v1 random{};
#if RISC_RUNTIME_PREFERENCE_STORE
    risc_preference_store_v1 preferences{};
    uint32_t preferenceNamespace=0;
    uint64_t preferenceOperation=0;
    risc_preference_store_receipt_v1 preferenceReceipt{};
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
    risc_key_value_atomic_v1 atomic{};
    risc_key_value_availability_v1 availability{};
    uint32_t availabilityNamespace=0;
    RiscExperimental::NativeAtomicKeyValue* atomicEndpoint=nullptr;
    uint64_t atomicOperation=0;
#endif
  };
  uint32_t grantGeneration_=0;
  bool providerFileRetained_=false;
  struct Platform {
    char capability[96]{}; uint32_t api=0; Scope scope=Scope::Global;
    uint64_t id=0; const void* table=nullptr;
  } platforms_[MaxPlatforms]{};
  size_t platformCount_=0;
  Port port_;
#if RISC_NATIVE_SETTINGS_LIFECYCLE
  bool nativeCheckpointRunning_=false;
#endif
  RuntimeProviders::NativeProviderPolicySnapshotV1* nativePolicies_=nullptr;
  FILE* retainedNativeProviderRead_=nullptr; // Deliberately never closed after lost native custody.
  uint8_t* retainedNativeProviderBytes_=nullptr; // May remain borrowed by uncertain native I/O.
  Board board_;
  // Must outlive graph destruction, including retained-module retry/abort.
  ProviderStorage providerStorage_[MaxDrivers]{};
  RuntimeProviders::GraphV2 graph_{RuntimeStreams::runtimeProviderStreamHost()};
  struct AppDataExportDelete {
    void operator()(RiscStorage::AppDataExport* value) const {
      value->~AppDataExport();std::free(value);
    }
  };
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
    const char* appDataSharedFile_=nullptr;
    std::unique_ptr<RiscStorage::AppDataExport,AppDataExportDelete> appDataExport_;
    risc_app_data_export_v1 appDataExportTable_{};
    bool appDataExportEnding_=false;
    risc_app_catalog_v1 appCatalogTable_{};
    void* appCatalogContext_=nullptr;
    bool appCatalogBusy_=false;
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
  bool firstDefaultStarted_=false,initialDefaultInvocation_=false;
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
  risc_runtime_launch_failure_v1 launchFailure_{};

  RuntimeProviders::GrantV2 grants_[MaxDrivers]{};
  Driver drivers_[MaxDrivers]{};
  size_t driverCount_=0,bootDriverCount_=0;
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
