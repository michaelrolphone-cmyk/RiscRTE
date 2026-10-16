#include <RiscInternalPackageVolumeV1.h>
#include "Runtime.h"
#include "AppDeclarations.h"
#include "RemovableAppImage.h"
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include "runtime/drivers/NativeProviderPolicySnapshotV1.h"
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <RiscUsbPhyResourceV1.h>
#include "diagnostics/Performance.h"
#include "diagnostics/StageLog.h"
#include "KeyValueGeneration.h"
#include "runtime/update/Version.h"
#include "runtime/update/CohortMigration.h"
#include "runtime/update/NativeAppDataExportPolicyV1.h"
#include "runtime/update/NativeAppDataExportPolicyV2.h"
#include "runtime/resources/ScopedBufferWipe.h"
#include <RiscDiagnosticSourceV1.h>
#include <esp_dlfcn.h>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <atomic>
#include "../../lib/hal/RuntimeImageCacheConfig.h"
#if defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST) || defined(RISC_REMOVABLE_APP_TEST)
#include <private/esp_dlcache.h>
#endif
#if defined(ESP_PLATFORM) || defined(RISC_REMOVABLE_APP_TEST) || defined(RISC_APP_IMAGE_CACHE_TEST)
#include <esp_elf.h>
#include <private/elf_platform.h>
extern "C" bool esp_elf_validate_file(const uint8_t*,size_t);
extern "C" bool esp_elf_admit_managed_app(const char*,const uint8_t*,size_t);
#endif
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
#include "native/NativeAppMemory.h"
extern "C" void native_app_memory_relocation(bool);
#endif
namespace {
RiscBoot::Runtime* currentRuntime=nullptr;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE || RISC_RUNTIME_PREFERENCE_STORE || RISC_RUNTIME_STORAGE_FILES
// Reject foreign workers before reading mutable Runtime/invocation state.
std::atomic<bool(*)()> atomicKeyValueOwner{nullptr};
#endif
#if RISC_APP_IMAGE_CACHE
std::atomic<RiscBoot::Runtime*> imagePressureRuntime{nullptr};
std::atomic<bool(*)()> imagePressureOwner{nullptr};
#endif
#if defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST)
void detachImagePressure() {
#if RISC_APP_IMAGE_CACHE
  imagePressureOwner.store(nullptr,std::memory_order_release);
  imagePressureRuntime.store(nullptr,std::memory_order_release);
#endif
}
#endif
// Pointer-sized opaque integers are never dereferenced; unlike a reusable slot
// pointer, a copied context cannot silently become a new grant after release.
uintptr_t keyValueGeneration=0;
bool keyValueKey(const char* key) {
  if (!key || !*key) return false;
  for (unsigned i=0;i<=RISC_KEY_VALUE_KEY_MAX;++i) {
    const char c=key[i]; if (!c) return true;
    if (i==RISC_KEY_VALUE_KEY_MAX || !((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='.' || c=='-')) return false;
  }
  return false;
}
}
extern "C" const risc_runtime_api_v1* risc_runtime_get_api(uint32_t version) {
  static const risc_runtime_api_v1 api={1,sizeof(api),
    [](risc_runtime_health_v1* h){return currentRuntime && currentRuntime->health(h);},
    [](uint32_t ms){if(currentRuntime) currentRuntime->yield(ms);},
    [](const char* line){return currentRuntime && currentRuntime->diagnostic(line);},
    [](const char* path){return currentRuntime && currentRuntime->launch(path);},
    [](const char* cap,uint32_t version,uint64_t instance,risc_runtime_capability_v1* out){return currentRuntime && currentRuntime->acquire(cap,version,instance,out);},
    [](risc_runtime_capability_v1* grant){return currentRuntime && currentRuntime->release(grant);},
    [](){return currentRuntime && currentRuntime->confirmBoot();},
    [](){return currentRuntime && currentRuntime->retainInvocation();},
    [](uint32_t id,uint32_t phase,uint32_t value)->uint32_t {
      return currentRuntime && currentRuntime->active() ? RiscPerf::interaction(id,phase,value) : 0;
    },
    [](risc_stream_client_v1* out){return currentRuntime && currentRuntime->streamClient(out);},
    [](){return currentRuntime && currentRuntime->launchDefault();},
    [](risc_resident_client_v1* out){return currentRuntime && currentRuntime->residentClient(out);},
    [](risc_failure_evidence_client_v1* out){return currentRuntime && currentRuntime->failureEvidenceClient(out);},
    [](risc_diagnostic_checkpoint_client_v1* out){return currentRuntime && currentRuntime->diagnosticCheckpointClient(out);},
    [](uint32_t* out){return currentRuntime && currentRuntime->monotonicMs(out);},
    [](){return currentRuntime && currentRuntime->exitRequested();},
    [](risc_runtime_launch_failure_v1* out){return currentRuntime && currentRuntime->takeLaunchFailure(out);},
    [](){return currentRuntime && currentRuntime->initialDefault();}};
  return version==1 && currentRuntime && currentRuntime->active() ? &api : nullptr;
}
extern "C" bool risc_runtime_reclaim_app_images() {
#if RISC_APP_IMAGE_CACHE
  // Foreign tasks reject before reading/dereferencing a Runtime pointer. The
  // owner function is compiled-in and outlives the session it guards.
  const auto owner=imagePressureOwner.load(std::memory_order_acquire);
  if(!owner || !owner())return false;
  auto* runtime=imagePressureRuntime.load(std::memory_order_acquire);
  return runtime && runtime->reclaimAppImages();
#else
  return false;
#endif
}
namespace RiscBoot {
#include "FailureEvidenceRuntime.inc"
#include "DiagnosticCheckpointRuntime.inc"
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
#include "AtomicKeyValueRuntime.inc"
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
#include "PreferenceStoreRuntime.inc"
#endif
#if RISC_RUNTIME_STORAGE_FILES
#include "StorageFilesRuntime.inc"
#endif
Runtime::~Runtime() {
#if RISC_RUNTIME_STORAGE_FILES
  if(!storageFilesExitSafe(true))std::abort();
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
  if(!preferenceExitSafe(true))std::abort();
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(!atomicExitSafe(true))std::abort();
#endif
  if(settings_ && settings_->retained())std::abort();
  // Native owners retain the entire metadata candidate after lost I/O custody.
  // As with GraphV2, destruction is a caller contract violation, never cleanup.
  if(retainedNativeProviderRead_ || retainedNativeProviderBytes_)std::abort();
  if(!fileSourceExitSafe() || fileSource_.token)std::abort();
  revokeProviders();
  RuntimeProviders::NativeProviderPolicySnapshotV1::destroy(nativePolicies_);
}
namespace {
bool elfPath(const char* p) { size_t n=strlen(p); return n>4 && !strcmp(p+n-4,".elf"); }
}
#include "AppCatalogRuntime.inc"
#include "FileOpenRuntime.inc"
#include "FileSourceRuntime.inc"
bool Runtime::manifest(JsonObjectConst m,Driver& d) {
  if (!keys(m,{"type","id","version","driver_abi","architecture","file_name","requires","provides"},
        {"hardware_compatibility","status","notes","description","display_name","physical_verification","part","legacy_manual_only"}) ||
      !eq(m["type"],"driver") || !eq(m["architecture"],"xtensa-esp32s3") ||
      !m["driver_abi"].is<unsigned>() || m["driver_abi"].as<unsigned>()!=2 ||
      !text(m["id"],d.id,sizeof(d.id)) || !m["requires"].is<JsonArrayConst>() || !m["provides"].is<JsonArrayConst>()) return fail("invalid driver manifest");
  if (!text(m["version"],d.version,sizeof(d.version)) || !RuntimePackages::safeVersion(d.version)) return fail("invalid driver version");
  char filename[128]{}; if (!text(m["file_name"],filename,sizeof(filename)) || strchr(filename,'/') || !elfPath(filename)) return fail("invalid driver executable");
  char* slash=strrchr(d.elf,'/'); if (!slash) return false;
  *(slash+1)=0; if (strlen(d.elf)+strlen(filename)>=sizeof(d.elf)) return false;
  strcat(d.elf,filename);
  // Same directory as the manifest, with strict normalized basename.
  char checked[256]; if (!path("",filename,checked,sizeof(checked))) return false;
  JsonArrayConst provides=m["provides"], required=m["requires"];
  if (provides.size()!=1 || required.size()>RuntimeProviders::GraphV2::kMaxRequirements) return fail("driver capability bounds");
  JsonObjectConst p=provides[0]; int64_t api;
  if (!keys(p,{"capability","api"}) || !text(p["capability"],d.provides,sizeof(d.provides)) ||
      !integer(p["api"],1,UINT32_MAX,api) || !strcmp(d.provides,"hardware.device") ||
      !strcmp(d.provides,RISC_BOUND_KEY_VALUE_CAPABILITY) || !strcmp(d.provides,RISC_BOUND_APP_DATA_CAPABILITY)) return fail("invalid provides");
  d.api=api;
  bool needsHardware=false;
  for (JsonObjectConst req:required) {
    if (!keys(req,{"capability","api"}) || !text(req["capability"],d.names[d.count],96) || !integer(req["api"],1,UINT32_MAX,api)) return fail("invalid requirement");
    for(size_t j=0;j<d.count;++j) if(!strcmp(d.names[j],d.names[d.count])) return fail("duplicate requirement");
    if (!strcmp(d.names[d.count],"hardware.device")) { if(api!=1) return false; needsHardware=true; }
    d.requirements[d.count]={d.names[d.count],static_cast<uint32_t>(api)}; ++d.count;
  }
  if (bool(d.instance)!=needsHardware) return fail("selected hardware/requirement mismatch");
  if (!d.instance) return m["hardware_compatibility"].isNull() || fail("hardware compatibility requires selected instance");
  const Board::Device* dev=board_.device(d.instance);
  if (!dev || !m["hardware_compatibility"].is<JsonArrayConst>()) return fail("missing hardware instance/compatibility");
  JsonArrayConst compatibility=m["hardware_compatibility"]; if (!compatibility.size() || compatibility.size()>16) return false;
  unsigned matches=0;
  for (JsonObjectConst c:compatibility) {
    char compatible[96]{}, type[96]{};
    if (!keys(c,{"compatible","revisions","config_type","config_version"}) ||
        !text(c["compatible"],compatible,96) || !text(c["config_type"],type,96) ||
        !integer(c["config_version"],1,(!strcmp(type,"radio.lora") || !strcmp(type,"touch.i2c") || !strcmp(type,"display.spi"))?2:1,api) || !c["revisions"].is<JsonArrayConst>()) return fail("invalid hardware compatibility");
    JsonArrayConst revisions=c["revisions"]; if (!revisions.size() || revisions.size()>16) return false;
    bool revision=false;
    for (JsonVariantConst v:revisions) { char s[96]; if(!text(v,s,96) || strchr(s,'*')) return false; if(!strcmp(s,dev->revision)) revision=true; }
    if (revision && !strcmp(compatible,dev->compatible) && !strcmp(type,dev->type) && api==dev->hardware.config_version) ++matches;
  }
  return matches==1 || fail("incompatible/ambiguous selected hardware");
}
bool Runtime::selected(uint64_t instance) const {
  for (size_t i=0;i<driverCount_;++i) if (drivers_[i].instance==instance) return true;
  return false;
}
bool Runtime::uses(uint64_t instance,const char* capability,uint32_t api) const {
  for (size_t i=0;i<driverCount_;++i) if (drivers_[i].instance==instance)
    for (size_t j=0;j<drivers_[i].count;++j) if (drivers_[i].requirements[j].api==api && !strcmp(drivers_[i].requirements[j].capability,capability)) return true;
  return false;
}
bool Runtime::registerPlatform(const char* capability,uint32_t api,Scope scope,uint64_t id,const void* table) {
  if ((attempted_ && !registrationOpen_) || !port_.owner() || !capability || !*capability || strlen(capability)>=96 || !api || !table || platformCount_==MaxPlatforms ||
      (strncmp(capability,"platform.",9) && strcmp(capability,"spi.bus"))) return false;
  // The provider route is always the Runtime-owned readonly broker, never an
  // arbitrary backend/control table supplied through generic registration.
  if (!strcmp(capability,RISC_PLATFORM_REALTIME_CAPABILITY) &&
      (api!=1 || scope!=Scope::Global || id || table!=&providerRealtimeTable_ || !realtimeBackend_)) return false;
  if (!strcmp(capability,RISC_ENTROPY_CAPABILITY) &&
      (api!=1 || scope!=Scope::Global || id || table!=&providerEntropyTable_ || !entropyBackend_))return false;
  if (!strcmp(capability,RISC_TCP_LISTENER_CAPABILITY) &&
      (api!=1 || scope!=Scope::Global || id || table!=&providerTcpTable_ || !tcpBackend_))return false;
  if (!strcmp(capability,RISC_DIAGNOSTIC_SOURCE_CAPABILITY) &&
      (api!=RISC_DIAGNOSTIC_SOURCE_API_V1 || scope!=Scope::Global || id)) return false;
  if (scope==Scope::Global) {
    if (id || (strcmp(capability,"platform.clock") && strcmp(capability,"platform.board") &&
               strcmp(capability,"platform.http-client") && strcmp(capability,"platform.bank-store") &&
               strcmp(capability,"platform.radio.iq.resource") && strcmp(capability,RISC_PLATFORM_REALTIME_CAPABILITY) &&
               strcmp(capability,RISC_TCP_LISTENER_CAPABILITY) && strcmp(capability,RISC_ENTROPY_CAPABILITY) && strcmp(capability,RISC_DIAGNOSTIC_SOURCE_CAPABILITY) &&
               strcmp(capability,RISC_USB_PHY_RESOURCE_CAPABILITY) && strcmp(capability,"platform.package-volume") && strcmp(capability,RISC_INSTALLATION_CAPABILITY))) return false;
  } else if ((scope!=Scope::Device && scope!=Scope::Bus) || !id) return false;
  const auto* header=static_cast<const uint32_t*>(table);
  if (header[0]!=api || header[1]<8) return false;
  if (!strcmp(capability,RISC_DIAGNOSTIC_SOURCE_CAPABILITY) &&
      (header[1]<sizeof(risc_diagnostic_source_api_v1) ||
       !static_cast<const risc_diagnostic_source_api_v1*>(table)->read)) return false;
  if (!strcmp(capability,RISC_INTERNAL_PACKAGE_VOLUME_CAPABILITY)) {
    if(api!=1 || scope!=Scope::Global || id || header[1]<sizeof(risc_internal_package_volume_v1))return false;
    const auto* package=static_cast<const risc_internal_package_volume_v1*>(table);
    if(!package->exit_safe || !risc_storage_volume_download(package->volume))return false;
  }
  if (!strcmp(capability,RISC_USB_PHY_RESOURCE_CAPABILITY)) {
    if(api!=RISC_USB_PHY_RESOURCE_API_V1 || scope!=Scope::Global || id || header[1]<sizeof(risc_usb_phy_resource_api_v1))return false;
    const auto* usb=static_cast<const risc_usb_phy_resource_api_v1*>(table);
    if(usb->controller_kind!=RISC_USB_PHY_ESP32S3_OTG || usb->reserved || !usb->is_owner || !usb->claim || !usb->release)return false;
  }
  for (size_t i=0;i<platformCount_;++i) {
    const auto& p=platforms_[i];
    if (!strcmp(p.capability,capability) && p.api==api && p.scope==scope && p.id==id) return false;
  }
  auto& p=platforms_[platformCount_++]; strcpy(p.capability,capability);
  p.api=api; p.scope=scope; p.id=id; p.table=table; return true;
}
bool Runtime::validateGraph() {
  bool edges[MaxDrivers][MaxDrivers]{};
  for(size_t i=0;i<driverCount_;++i) {
    Driver& d=drivers_[i];
    for(size_t j=0;j<i;++j) {
      if (!strcmp(d.id,drivers_[j].id) && (strcmp(d.version,drivers_[j].version) || strcmp(d.elf,drivers_[j].elf))) return fail("package instances disagree on artifact/version");
      if ((!strcmp(d.id,drivers_[j].id) && (!d.instance || !drivers_[j].instance)) ||
          (d.instance && d.instance==drivers_[j].instance)) return fail("duplicate package singleton/hardware owner");
    }
    const auto* hw=d.instance?board_.device(d.instance):nullptr;
    bool needsStorage=false,needsFiles=false;
    for(size_t r=0;r<d.count;++r) {
      auto& req=d.requirements[r]; if(!strcmp(req.capability,"hardware.device")) continue;
      if (!strcmp(req.capability,RISC_BOUND_KEY_VALUE_CAPABILITY)) {
        auto& storage=providerStorage_[i];
        if ((req.api!=RISC_BOUND_KEY_VALUE_API_V1 && req.api!=RISC_BOUND_KEY_VALUE_API_V2) || !storage.count ||
            !settings_ ||
            settingsMaxBlobSize()<(req.api==RISC_BOUND_KEY_VALUE_API_V2?RISC_BOUND_KEY_VALUE_V2_BLOB_MAX:RISC_BOUND_KEY_VALUE_BLOB_MAX))
          return fail("provider key-value policy/backend unavailable");
        if (hw) for(size_t b=0;b<hw->bindingCount;++b)
          if (!strcmp(hw->bindings[b].capability,req.capability))
            return fail("provider key-value is not a hardware binding");
        needsStorage=true;
        storage.table.api_version=req.api;
        req.trustedApi=&storage.table;
        continue;
      }
      if (!strcmp(req.capability,RISC_BOUND_APP_DATA_CAPABILITY)) {
        auto& storage=providerStorage_[i];
        const auto* backend=port_.appData;
        if(req.api!=RISC_BOUND_APP_DATA_API_V1 || !storage.fileCount || !backend ||
           !backend->stat || !backend->read || !backend->replace || !backend->exitSafe)
          return fail("provider app-data policy/backend unavailable");
        if(hw)for(size_t b=0;b<hw->bindingCount;++b)
          if(!strcmp(hw->bindings[b].capability,req.capability))
            return fail("provider app-data is not a hardware binding");
        needsFiles=true;req.trustedApi=&storage.fileTable;
        continue;
      }
      uint64_t wanted=0;
      if(hw) for(size_t b=0;b<hw->bindingCount;++b) if(!strcmp(hw->bindings[b].capability,req.capability)) wanted=hw->bindings[b].instance;
      const Platform* platform=nullptr;
      for (size_t p=0;p<platformCount_;++p) {
        const auto& candidate=platforms_[p];
        if (candidate.api!=req.api || strcmp(candidate.capability,req.capability)) continue;
        const bool match=candidate.scope==Scope::Global ||
          (candidate.scope==Scope::Device && candidate.id==d.instance) ||
          (candidate.scope==Scope::Bus && candidate.id==board_.deviceBus(d.instance));
        if (!match) continue;
        if (platform || wanted) return fail("ambiguous platform/dependency binding");
        platform=&candidate;
      }
      if (platform) {
        req.trustedApi=platform->table;
        if (!strcmp(req.capability,RISC_ENTROPY_CAPABILITY)) {
          auto& storage=providerStorage_[i];storage.owner=this;storage.needsEntropy=true;
          storage.entropy=providerEntropyTable_;req.trustedApi=&storage.entropy;
        }
        if (!strcmp(req.capability,RISC_TCP_LISTENER_CAPABILITY)) {
          auto& storage=providerStorage_[i];storage.owner=this;storage.needsTcp=true;
          storage.tcp=providerTcpTable_;req.trustedApi=&storage.tcp;
        }
        if (!strcmp(req.capability,RISC_PLATFORM_REALTIME_CAPABILITY)) {
          auto& storage=providerStorage_[i];
          storage.owner=this;storage.needsRealtime=true;
          storage.realtime=providerRealtimeTable_;
          req.trustedApi=&storage.realtime;
        }
        continue;
      }
      // Missing native providers do not become fake devices or first-match ELFs.
      if (!strncmp(req.capability,"platform.",9) || !strcmp(req.capability,"spi.bus")) return fail("missing scoped trusted platform provider");
      if(hw && !wanted) return fail("hardware dependency requires explicit instance binding");
      int found=-1;
      for(size_t j=0;j<driverCount_;++j) if(drivers_[j].api==req.api && !strcmp(drivers_[j].provides,req.capability) && (!wanted || drivers_[j].instance==wanted)) {
        if(found>=0) return fail("ambiguous dependency");
        found=static_cast<int>(j);
      }
      if(found<0) return fail("missing dependency");
      req.providerId=drivers_[found].id; req.providerInstance=drivers_[found].instance;
      if (!strcmp(req.capability,"i2c.bus") && board_.deviceBus(d.instance)!=board_.deviceBus(drivers_[found].instance)) return fail("I2C dependency bus scope mismatch");
      edges[i][found]=true;
    }
    if (providerStorage_[i].count && !needsStorage) return fail("undeclared provider key-value policy");
    if (providerStorage_[i].fileCount && !needsFiles) return fail("undeclared provider app-data policy");
    if(hw) for(size_t b=0;b<hw->bindingCount;++b) {
      bool used=false; for(size_t r=0;r<d.count;++r) if(!strcmp(d.requirements[r].capability,hw->bindings[b].capability)) used=true;
      if(!used) return fail("unused hardware binding");
    }
  }
  // Source access is a loader dependency, not an extra capability passed to
  // the provider. Keep the entire mount closure internal, even with fallback.
  for(size_t i=0;i<driverCount_;++i)if(drivers_[i].sourcePath[0]) {
    auto& d=drivers_[i];d.sourceDriver=-1;
    for(size_t j=0;j<driverCount_;++j)
      if(drivers_[j].instance==d.sourceVolume && drivers_[j].api==1 &&
         !strcmp(drivers_[j].provides,d.sourceFiles?"storage.files":"storage.volume")) {
        if(d.sourceDriver>=0)return fail("ambiguous driver image source");
        d.sourceDriver=int(j);
      }
    if(d.sourceDriver<0)return fail("driver image source unavailable");
  }
  // Bounded transitive closure catches every cycle before ANY ELF entry point.
  for(size_t k=0;k<driverCount_;++k) for(size_t i=0;i<driverCount_;++i) for(size_t j=0;j<driverCount_;++j) edges[i][j]=edges[i][j] || (edges[i][k] && edges[k][j]);
  for(size_t i=0;i<driverCount_;++i) if(edges[i][i]) return fail("driver dependency cycle");
  for(size_t i=0;i<driverCount_;++i)if(drivers_[i].sourceDriver>=0) {
    const size_t source=size_t(drivers_[i].sourceDriver);
    for(size_t j=0;j<driverCount_;++j)
      if((j==source || edges[source][j]) && drivers_[j].sourceDriver>=0)
        return fail("image source bootstrap dependency must remain internal");
  }
  return true;
}
bool Runtime::appPolicies(JsonVariantConst value,JsonVariantConst suppliedManifest,size_t replacement) {
  const bool replacing=replacement<policyCount_;
  if(replacing && value.size()!=1)return fail("replacement requires one app policy");
  if (value.isNull()) return true;
  if (!value.is<JsonArrayConst>() || (!replacing && value.size()>MaxAppPolicies-policyCount_)) return fail("invalid app capability policy");
  if(!policies_ && value.size()){
    policyCapacity_=value.size();policies_=metadataArray<AppPolicy>(policyCapacity_);
    if(!policies_)return fail("app policy allocation failed");
  }
  if(!replacing && value.size()>policyCapacity_-policyCount_)return fail("app policy capacity exhausted");
  for (JsonObjectConst item:value.as<JsonArrayConst>()) {
    const size_t slot=replacing?replacement:policyCount_;
    auto& policy=policies_[slot]; char relative[193]; JsonDocument doc;
    if (!keys(item,{"manifest","grants"},{"app_data_export","image_source","catalog_authority"}) || !text(item["manifest"],relative,sizeof(relative)) ||
        !path(root_,relative,policy.elf,sizeof(policy.elf)) || (suppliedManifest.isNull() && !readJson(metadataSource(),policy.elf,doc,&metadataCloseRetained_))) return fail("invalid app policy manifest path");
    JsonObjectConst manifest=suppliedManifest.isNull()?doc.as<JsonObjectConst>():suppliedManifest.as<JsonObjectConst>(); char filename[128];
    if (!keys(manifest,{"type","id","version","architecture","file_name","entry","requires"},{"description","display_name","icon","supported_file_types","runtime_lifecycle","launcher_visible","min_firmware_version","optional"}) ||
        !eq(manifest["type"],"application") || !eq(manifest["architecture"],"xtensa-esp32s3") || !eq(manifest["entry"],"app_main") ||
        (!manifest["runtime_lifecycle"].isUnbound() && !eq(manifest["runtime_lifecycle"],"foreground") && !eq(manifest["runtime_lifecycle"],"legacy")) ||
        !text(manifest["id"],policy.id,sizeof(policy.id)) || !RuntimePackages::safeId(policy.id) ||
        !text(manifest["version"],policy.version,sizeof(policy.version)) || !RuntimePackages::safeVersion(policy.version) ||
        !text(manifest["file_name"],filename,sizeof(filename)) || !RuntimePackages::safeArtifact(filename) || !elfPath(filename) ||
        !manifest["requires"].is<JsonArrayConst>() || manifest["requires"].size()>MaxAppRequirements ||
        !item["grants"].is<JsonArrayConst>() || item["grants"].size()>MaxAppPolicyGrants) return fail("invalid app identity/declarations");
    const AppDeclarations declarations(manifest);
    if(const char* error=declarations.error(MaxAppRequirements))return fail(error);
    appCatalogMetadata(manifest,policy.catalog);
    FileOpenMetadata fileMetadata;
    if(!fileOpenMetadata(manifest,fileMetadata))return fail("invalid app file associations");
    if(fileMetadata.count) {
      if(!fileHandlers_)fileHandlers_=metadataArray<FileOpenMetadata>(policyCapacity_);
      if(!fileHandlers_)return fail("file association allocation failed");
      fileHandlers_[slot]=fileMetadata;
    }
    char* slash=strrchr(policy.elf,'/'); if (!slash) return false;
    *(slash+1)=0;
    if (strlen(policy.elf)+strlen(filename)>=sizeof(policy.elf)) return fail("app path too long");
    strcat(policy.elf,filename);
    for (size_t i=0;i<policyCount_;++i) if (i!=slot && !(installation_ && installation_[0].call && policies_[i].catalogAuthority && !strcmp(policies_[i].id,policy.id)) && (!strcmp(policies_[i].id,policy.id) || !strcmp(policies_[i].elf,policy.elf))) return fail("duplicate app identity/path policy");
    if(!item["catalog_authority"].isUnbound()) {
      if(!item["catalog_authority"].is<bool>() || !item["catalog_authority"].as<bool>() || !installation_ ||
         suppliedManifest.isNull()==false || item["image_source"].isNull() || !item["app_data_export"].isNull())return fail("invalid catalog authority reservation");
      policy.catalogAuthority=true;policy.enabled=false;
    }
    if(!item["image_source"].isNull()) {
      auto source=item["image_source"].as<JsonObjectConst>();int64_t instance=0;char relativeImage[192];
      if(!keys(source,{"volume_instance","path"},{"capability"}) ||
         (!source["capability"].isUnbound() && !eq(source["capability"],"storage.files") && !eq(source["capability"],"storage.volume")) || !integer(source["volume_instance"],0,INT32_MAX,instance) ||
         !text(source["path"],relativeImage,sizeof(relativeImage)) || !InstalledFiles::valid(relativeImage,false) ||
         !elfPath(relativeImage) || !strcmp(policy.elf,invocation_->current_))return fail("invalid app image source");
      policy.sourceFiles=eq(source["capability"],"storage.files");
      for(size_t d=0;d<driverCount_;++d)if(drivers_[d].instance==uint64_t(instance) &&
          !strcmp(drivers_[d].provides,policy.sourceFiles?"storage.files":"storage.volume") && drivers_[d].api==1) {
        if(policy.sourceDriver>=0)return fail("ambiguous app image source");
        policy.sourceDriver=static_cast<PolicyIndex>(d);
      }
      if(policy.sourceDriver<0)return fail("app image source unavailable");
      snprintf(policy.sourcePath,sizeof(policy.sourcePath),"/%s",relativeImage);
    }
    for (size_t declaration=0;declaration<declarations.size();++declaration) {
      const JsonObjectConst request=declarations[declaration];
      char requested[96];int64_t api=0;
      if (!keys(request,{"capability","api"}) || !text(request["capability"],requested,sizeof(requested)) ||
          !integer(request["api"],1,UINT32_MAX,api)) return fail("invalid app requirement");
      if (!strcmp(requested,RISC_BOUND_KEY_VALUE_CAPABILITY)) return fail("provider key-value denied to app");
      // A manifest declares each capability once. The owner may independently
      // authorize more than one positive KV namespace, still within MaxAppPolicyGrants
      // total grants; no other capability's uniqueness rule is broadened.
      const bool keyValue=!strcmp(requested,RISC_KEY_VALUE_CAPABILITY);
#if RISC_RUNTIME_STORAGE_FILES
      const bool files=!strcmp(requested,RISC_STORAGE_FILES_CAPABILITY);
#else
      const bool files=false;
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
      const bool preferenceStore=!strcmp(requested,RISC_PREFERENCE_STORE_CAPABILITY);
#else
      const bool preferenceStore=false;
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
      const bool atomicKeyValue=!strcmp(requested,RISC_KEY_VALUE_ATOMIC_CAPABILITY);
      const bool availability=!strcmp(requested,RISC_KEY_VALUE_AVAILABILITY_CAPABILITY);
#else
      const bool atomicKeyValue=false,availability=false;
#endif
      const bool sharedData=!strcmp(requested,RISC_SHARED_DATA_CAPABILITY);
      for (size_t i=0;i<policy.count;++i) if (!strcmp(policy.grants[i].capability,requested) &&
          (!keyValue || policy.grants[i].api==uint32_t(api))) return fail("duplicate app requirement");
      unsigned matches=0;
      for (JsonObjectConst allowed:item["grants"].as<JsonArrayConst>()) {
        int64_t allowedApi=0,instance=0;char capability[96];
        if (!text(allowed["capability"],capability,sizeof(capability)) ||
            !(
#if RISC_RUNTIME_STORAGE_FILES
              !strcmp(capability,RISC_STORAGE_FILES_CAPABILITY)?keys(allowed,{"capability","api","instance_id","backend_capability","backend_instance","label"},{"backend_provider"}):
#endif
              strcmp(capability,RISC_SHARED_DATA_CAPABILITY)?keys(allowed,{"capability","api","instance_id"}):keys(allowed,{"capability","api","instance_id","file"})) ||
            !integer(allowed["api"],1,UINT32_MAX,allowedApi) || !integer(allowed["instance_id"],0,INT32_MAX,instance)) return fail("invalid app grant");
        if (!strcmp(capability,RISC_BOUND_KEY_VALUE_CAPABILITY)) return fail("provider key-value denied to app");
        if (!strcmp(capability,RISC_BOUND_APP_DATA_CAPABILITY)) return fail("provider app-data denied to app");
        if (strcmp(capability,requested) || allowedApi!=api) continue;
        if (++matches>1 && !keyValue && !atomicKeyValue && !availability && !preferenceStore && !sharedData && !files) return fail("app requirement not uniquely authorized");
        if (policy.count==MaxAppPolicyGrants) return fail("too many app grants");
        for (size_t i=0;i<policy.count;++i) {
          const auto& earlier=policy.grants[i];
          if (!strcmp(earlier.capability,capability) && earlier.api==uint32_t(api) && earlier.instance==uint64_t(instance))
            return fail("duplicate app grant");
        }
        auto& grant=policy.grants[policy.count];
        grant.api=api;grant.instance=instance;
#if RISC_RUNTIME_STORAGE_FILES
        if(files) {
          int64_t backend=0;char backendCapability[96],backendProvider[96]{};
          if(grant.api!=1 || !grant.instance || !integer(allowed["backend_instance"],0,INT32_MAX,backend) ||
             !text(allowed["label"],grant.sharedFile,sizeof(grant.sharedFile)) ||
             !text(allowed["backend_capability"],backendCapability,sizeof(backendCapability)) ||
             (strcmp(backendCapability,"storage.volume") && strcmp(backendCapability,RISC_STORAGE_FILES_CAPABILITY)) ||
             (!allowed["backend_provider"].isUnbound() && !text(allowed["backend_provider"],backendProvider,sizeof(backendProvider))))return fail("invalid file namespace mapping");
          for(size_t d=0;d<driverCount_;++d)if(!strcmp(drivers_[d].provides,backendCapability) &&
              drivers_[d].api==1 && drivers_[d].instance==uint64_t(backend) && (!backendProvider[0] || !strcmp(backendProvider,drivers_[d].id))) {
            if(grant.platform>=0)return fail("ambiguous file namespace backend");
            grant.platform=static_cast<PolicyIndex>(d);
          }
          if(grant.platform<0)return fail("file namespace backend unavailable");
          grant.driver=StorageFilesDriver;grant.capability=RISC_STORAGE_FILES_CAPABILITY;
        } else if(!strcmp(capability,RISC_STORAGE_FILES_CATALOG_CAPABILITY)) {
          if(grant.api!=1 || grant.instance)return fail("invalid files catalog authority");
          grant.driver=StorageFilesCatalogDriver;grant.capability=RISC_STORAGE_FILES_CATALOG_CAPABILITY;
        } else
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
        if(preferenceStore) {
          if(grant.api!=1 || !grant.instance || !preferenceAvailable(policy.id,uint32_t(grant.instance)))
            return fail("preference app/backend/namespace unavailable");
          grant.driver=PreferenceStoreDriver;grant.capability=RISC_PREFERENCE_STORE_CAPABILITY;
        } else
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
        if (availability) {
          if(grant.api!=1 || !grant.instance || !keyValueAvailability(uint32_t(grant.instance)))
            return fail("key-value availability namespace unavailable");
          grant.driver=KeyValueAvailabilityDriver;grant.capability=RISC_KEY_VALUE_AVAILABILITY_CAPABILITY;
        } else if (atomicKeyValue) {
          if(grant.api!=1 || !grant.instance || !atomicBinding(policy.id,uint32_t(grant.instance)))
            return fail("atomic key-value app/backend/namespace unavailable");
          grant.driver=AtomicKeyValueDriver;grant.capability=RISC_KEY_VALUE_ATOMIC_CAPABILITY;
        } else
#endif
        if (keyValue) {
          if ((grant.api!=RISC_KEY_VALUE_API_V1 && grant.api!=RISC_KEY_VALUE_API_V2) || !grant.instance || !settings_ ||
              settingsMaxBlobSize()<(grant.api==RISC_KEY_VALUE_API_V2?RISC_KEY_VALUE_V2_BLOB_MAX:RISC_KEY_VALUE_BLOB_MAX)) return fail("app key-value backend/namespace unavailable");
          grant.keyValue=true;grant.capability=RISC_KEY_VALUE_CAPABILITY;
        } else if (!strcmp(capability,RISC_PROVIDER_PROMOTION_CAPABILITY)) {
          if(grant.api!=1 || grant.instance || strcmp(policy.elf,invocation_->current_))return fail("invalid default provider-promotion authority");
          grant.driver=PromotionDriver;grant.capability=RISC_PROVIDER_PROMOTION_CAPABILITY;
        } else if (!strcmp(capability,RISC_REALTIME_CAPABILITY) || !strcmp(capability,RISC_REALTIME_CONTROL_CAPABILITY)) {
          if(grant.api!=1 || grant.instance || !realtimeBackend_)return fail("realtime backend/authority unavailable");
          const bool control=!strcmp(capability,RISC_REALTIME_CONTROL_CAPABILITY);
          grant.driver=control?RealtimeControlDriver:RealtimeDriver;
          grant.capability=control?RISC_REALTIME_CONTROL_CAPABILITY:RISC_REALTIME_CAPABILITY;
        } else if (!strcmp(capability,RISC_RETAINED_WAKE_CAPABILITY)) {
          if(grant.api!=1 || grant.instance || !port_.retainedWake)return fail("retained-wake backend/authority unavailable");
          if(!retainedCohort_[0]){
            RiscUpdate::CohortIdentity identity{};
            if(!RiscUpdate::readCohort(metadataSource(),root_,identity,&metadataCloseRetained_))return fail("retained-wake cohort unavailable");
            char digest[65]{};for(unsigned b=0;b<32;++b)snprintf(digest+2*b,3,"%02x",identity.firmwareSha[b]);
            const int n=snprintf(retainedCohort_,sizeof(retainedCohort_),"%s|%s|%s|%s|%s|%s|%u|%u|%s",
              identity.product.product,identity.product.version,identity.product.source_repo,identity.product.source_revision,
              identity.runtimeVersion,identity.layout,unsigned(identity.storeAbi),unsigned(identity.firmwareSize),digest);
            if(n<=0 || size_t(n)>=sizeof(retainedCohort_))return fail("retained-wake cohort too large");
          }
          grant.driver=RetainedWakeDriver;grant.capability=RISC_RETAINED_WAKE_CAPABILITY;
        } else if (!strcmp(capability,RISC_RANDOM_CAPABILITY)) {
          if(grant.api!=1 || grant.instance || !port_.randomBytes)return fail("random bytes capability unavailable");
          grant.driver=RandomDriver;grant.capability=RISC_RANDOM_CAPABILITY;
        } else if (!strcmp(capability,RISC_APP_CATALOG_CAPABILITY)) {
          if(grant.api!=RISC_APP_CATALOG_API_V1 || grant.instance)return fail("invalid app catalog authority");
          grant.driver=AppCatalogDriver;grant.capability=RISC_APP_CATALOG_CAPABILITY;
        } else if (!strcmp(capability,RISC_MEMORY_HEAP_CAPABILITY)) {
          if(grant.api!=1 || grant.instance || !port_.memoryHeap)return fail("memory heap capability unavailable");
          grant.driver=MemoryHeapDriver;grant.capability=RISC_MEMORY_HEAP_CAPABILITY;
        } else if (!strcmp(capability,RISC_FILE_OPEN_SOURCE_CAPABILITY)) {
          const auto* source=port_.fileSource;
          if(grant.api!=1 || grant.instance || !source || !source->begin || !source->write || !source->finish ||
             !source->readAt || !source->discard || !source->exitSafe)return fail("file-source backend/authority unavailable");
          grant.fileOpenSource=true;grant.capability=RISC_FILE_OPEN_SOURCE_CAPABILITY;
        } else if (!strcmp(capability,"file.open")) {
          if(grant.api!=T5_FILE_OPEN_API_VERSION || grant.instance)return fail("invalid file-open authority");
          grant.fileOpen=true;grant.capability="file.open";
        } else if (!strcmp(capability,RISC_APP_DATA_EXPORT_CAPABILITY)) {
          const auto* backend=port_.appData;
          if(grant.api!=RISC_APP_DATA_EXPORT_API_V1 || grant.instance || !backend || !backend->stat || !backend->read || !backend->replace || !backend->exitSafe)
            return fail("app-data export backend/authority unavailable");
          grant.driver=AppDataExportDriver;grant.capability=RISC_APP_DATA_EXPORT_CAPABILITY;
        } else if (sharedData) {
          const auto* backend=port_.appData;
          if(grant.api!=RISC_APP_DATA_API_V1 || !grant.instance || !backend || !backend->stat || !backend->read || !backend->replace || !backend->exitSafe ||
             !text(allowed["file"],grant.sharedFile,sizeof(grant.sharedFile)))return fail("shared-data backend/file unavailable");
          for(size_t n=0;grant.sharedFile[n];++n){const unsigned char c=grant.sharedFile[n];const bool alpha=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');if(!alpha&&(!n||(c!='.'&&c!='_'&&c!='-')))return fail("invalid shared-data file");}
          grant.driver=SharedDataDriver;grant.capability=RISC_SHARED_DATA_CAPABILITY;
        } else if (!strcmp(capability,RISC_APP_DATA_CAPABILITY)) {
          const auto* backend=port_.appData;
          if(grant.api!=RISC_APP_DATA_API_V1 || !grant.instance || !backend || !backend->stat || !backend->read || !backend->replace || !backend->exitSafe)return fail("app-data backend/namespace unavailable");
          // A namespace belongs to one admitted app identity. No accidental
          // sharing through copied policy numbers or provider-global tables.
          for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g)if(p!=slot && !(installation_ && installation_[0].call && policies_[p].catalogAuthority && !strcmp(policies_[p].id,policy.id)) && policies_[p].grants[g].driver==AppDataDriver && policies_[p].grants[g].instance==grant.instance)return fail("app-data namespace already owned");
          grant.driver=AppDataDriver;grant.capability=RISC_APP_DATA_CAPABILITY;
        } else if (!strcmp(capability,"storage.installed-files")) {
          if(grant.instance || grant.api!=1)return fail("invalid installed-files authority");
          grant.installedFiles=true;grant.capability="storage.installed-files";
        } else if (!strcmp(capability,"platform.clock")) {
          for (size_t p=0;p<platformCount_;++p) if (platforms_[p].scope==Scope::Global && !strcmp(platforms_[p].capability,capability) && platforms_[p].api==grant.api) {
            if (grant.platform>=0 || grant.instance) return fail("ambiguous app platform clock");
            grant.platform=static_cast<PolicyIndex>(p);
          }
          if (grant.platform<0) return fail("app platform clock unavailable");
          grant.capability=platforms_[grant.platform].capability;
        } else {
          if (!strcmp(capability,"storage.files") || !strcmp(capability,"storage.files.catalog"))return fail("generic files broker unavailable");
          if (!strcmp(capability,"storage.preferences"))return fail("preference store broker unavailable");
          if (!strcmp(capability,"storage.key-value.atomic"))return fail("atomic key-value broker unavailable");
          if (!strncmp(capability,"platform.",9) || !strcmp(capability,"spi.bus") || !strcmp(capability,"hardware.device")) return fail("raw platform capability denied to app");
          for (size_t d=0;d<driverCount_;++d) if (!strcmp(drivers_[d].provides,capability) && drivers_[d].api==grant.api && (!grant.instance || drivers_[d].instance==grant.instance)) {
            if (grant.driver>=0) return fail("ambiguous app provider");
            grant.driver=static_cast<PolicyIndex>(d);
          }
          if (grant.driver<0) return fail("app provider unavailable");
          grant.capability=drivers_[grant.driver].provides;
        }
        ++policy.count;
      }
      if (!matches && declarations.required(declaration)) return fail("app requirement not uniquely authorized");
    }
    if (policy.count!=item["grants"].size()) return fail("undeclared app grant");
    if(!replacing)++policyCount_;
    appCatalogChanged();
  }
  return true;
}
bool Runtime::acquire(const char* capability,uint32_t api,uint64_t instance,risc_runtime_capability_v1* out) {
#if RISC_RUNTIME_STORAGE_FILES
  if(storageFilesCalling_)return false;
#endif
  if (promotionRunning_ || graph_.lifecycleBusy() || !active() || !invocation_->appPolicy_ || !out || out->struct_size<sizeof(*out) || !capability || !api || grantGeneration_==UINT32_MAX) return false;
  const bool catalog=!strcmp(capability,RISC_APP_CATALOG_CAPABILITY);
  Invocation* const original=invocation_;
  // A native catalog safety hook cannot claim any still-uncommitted grant
  // slot recursively, even through a different authorized capability.
  if(original->appCatalogBusy_)return false;
  struct CatalogAcquisition {
    bool* busy;
    ~CatalogAcquisition(){if(busy)*busy=false;}
  } catalogCall{catalog?&original->appCatalogBusy_:nullptr};
  if(catalogCall.busy)*catalogCall.busy=true;
  auto catalogCustody=[&](){return currentRuntime==this && invocation_==original &&
    original->active_ && original->entryRunning_ && !retained_ && port_.owner();};
  auto catalogRetain=[&](){retained_=true;fail("app catalog admission retained");(void)appExitBarrier(false);return false;};
  out->slot=out->generation=0;out->api=nullptr;
#if RISC_NATIVE_SETTINGS_LIFECYCLE || RISC_NATIVE_SERIAL_FILES
  if(nativeCheckpointRunning_)return false;
#endif
  if(!appDataExitSafe()){(void)appExitBarrier(false);return false;}
  if(catalog && !catalogCustody())return catalogRetain();
  const AppGrantPolicy* allowed=nullptr;
  for (size_t i=0;i<invocation_->appPolicy_->count;++i) {
    const auto& p=invocation_->appPolicy_->grants[i];
    const uint64_t selected=p.driver>=0?drivers_[p.driver].instance:p.instance;
    if (!strcmp(p.capability,capability) && p.api==api && (!instance || selected==instance)) {
      // instance0 means unique, never last/first match. Explicit namespaces
      // remain independently bound to their original owner-provisioned ID.
      if (allowed) return false;
      allowed=&p;
    }
  }
  if (!allowed) return false;
  size_t slot=0;while(slot<16 && invocation_->appGrants_[slot].live)++slot;
  if (slot==16) return false;
  auto& grant=invocation_->appGrants_[slot];
#if RISC_RUNTIME_STORAGE_FILES
  if(allowed->driver==StorageFilesCatalogDriver){
    if(!invocation_->entryRunning_ || retained_)return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    grant.storageFilesCatalog={1,sizeof(grant.storageFilesCatalog),context,storageFilesSnapshot,storageFilesGet};grant.api=&grant.storageFilesCatalog;
  } else if(allowed->driver==StorageFilesDriver){
    if(!invocation_->entryRunning_ || retained_ || !providerStorageSafe())return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    grant.storageFilesAdapter.reset(new(std::nothrow) StorageFilesVolumeAdapter);
    if(!grant.storageFilesAdapter)return false;
    if(!acquireProviderDriver(allowed->platform,drivers_[allowed->platform].provides,1,grant)) {
      if(!retained_)grant={};
      return false;
    }
    const bool backendBound=!strcmp(drivers_[allowed->platform].provides,RISC_STORAGE_FILES_CAPABILITY)
      ?grant.storageFilesAdapter->bindFiles(static_cast<const risc_storage_files_v1*>(grant.api))
      :grant.storageFilesAdapter->bind(static_cast<const risc_storage_volume_api_v1*>(grant.api));
    if(!backendBound) {
      if(!graph_.release(grant.provider)){retained_=true;fail("file backend admission retained");(void)appExitBarrier(false);return false;}
      grant={};return false;
    }
    grant.storageFiles=storageFilesTable(context);grant.api=&grant.storageFiles;
  } else
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
  if(allowed->driver==PreferenceStoreDriver){
#if RISC_NATIVE_SETTINGS_LIFECYCLE && RISC_RUNTIME_ATOMIC_KEY_VALUE
    serviceNativeSettings();if(!active() || retained_)return false;
#endif
    if(retained_ || !invocation_->entryRunning_ || !providerStorageSafe() ||
       !preferenceAvailable(invocation_->appPolicy_->id,uint32_t(allowed->instance)))return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    grant.preferences={1,sizeof(grant.preferences),context,preferenceState,preferenceGet,preferenceSubmit,preferenceCollect,preferenceCancel};
    grant.preferenceNamespace=uint32_t(allowed->instance);grant.api=&grant.preferences;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
    if(auto* binding=atomicBinding(invocation_->appPolicy_->id,grant.preferenceNamespace))grant.atomicEndpoint=&binding->endpoint;
#endif
  } else
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(allowed->driver==KeyValueAvailabilityDriver){
    if(!keyValueAvailability(uint32_t(allowed->instance)))return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    grant.availability={1,sizeof(grant.availability),context,keyValueAvailabilitySnapshot};
    grant.availabilityNamespace=allowed->instance;grant.api=&grant.availability;
  } else if(allowed->driver==AtomicKeyValueDriver){
#if RISC_NATIVE_SETTINGS_LIFECYCLE
    // Start/poll old work before publishing the first draft-capable grant.
    // An accepted sweep fences atomic state/get/submit until full collection.
    // Existing host/foreground grants fence admission inside this checkpoint.
    serviceNativeSettings();
    if(!active() || retained_)return false;
#endif
    auto* binding=atomicBinding(invocation_->appPolicy_->id,uint32_t(allowed->instance));
    if(!binding || retained_ || !invocation_->entryRunning_ || !providerStorageSafe())return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    grant.atomic={1,sizeof(grant.atomic),context,atomicState,atomicGet,atomicSubmit,atomicCollect,atomicCancel};
    grant.atomicEndpoint=&binding->endpoint;grant.api=&grant.atomic;
  } else
#endif
  if(allowed->driver==AppCatalogDriver){
    if(invocation_->appCatalogContext_ || !invocation_->entryRunning_ || retained_)return false;
    if(!providerStorageSafe() || !catalogCustody())return catalogRetain();
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    invocation_->appCatalogContext_=context;
    invocation_->appCatalogTable_={1,sizeof(invocation_->appCatalogTable_),context,appCatalogSnapshot,appCatalogGet};
    grant.api=&invocation_->appCatalogTable_;
  } else if(allowed->driver==RandomDriver){

    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    grant.random={1,sizeof(grant.random),context,randomFill};grant.api=&grant.random;
  } else if(allowed->driver==MemoryHeapDriver){
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    grant.memoryHeap={1,sizeof(grant.memoryHeap),context,memoryHeapSnapshot};grant.api=&grant.memoryHeap;
  } else if (allowed->keyValue) {
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    grant.keyValueNamespace=allowed->instance;
    grant.keyValue={allowed->api,sizeof(risc_key_value_v1),context,keyValueGet,keyValuePut};
    grant.api=&grant.keyValue;
  } else if (allowed->driver==PromotionDriver) {
    if(invocation_->promotionContext_ || !invocation_->defaultRunning_ || !invocation_->entryRunning_ || !promotionSafe())return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    invocation_->promotionContext_=context;invocation_->promotionTable_={1,sizeof(invocation_->promotionTable_),context,promoteProviders};
    grant.api=&invocation_->promotionTable_;
  } else if (allowed->driver==RealtimeControlDriver) {
    if(invocation_->realtimeControlContext_ || !invocation_->entryRunning_ || retained_ || !providerStorageSafe())return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    invocation_->realtimeControlContext_=context;
    invocation_->realtimeControlTable_={1,sizeof(invocation_->realtimeControlTable_),context,realtimeRead,realtimeSeed};
    grant.api=&invocation_->realtimeControlTable_;
  } else if (allowed->driver==RealtimeDriver) {
    if(invocation_->realtimeContext_ || !invocation_->entryRunning_ || retained_ || !providerStorageSafe())return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    invocation_->realtimeContext_=context;invocation_->realtimeTable_={1,sizeof(invocation_->realtimeTable_),context,realtimeRead};
    grant.api=&invocation_->realtimeTable_;
  } else if (allowed->driver==RetainedWakeDriver) {
    if(invocation_->retainedWakeContext_ || !providerStorageSafe() || !port_.retainedWake->ready())return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    invocation_->retainedWakeContext_=context;
#if RISC_RETAINED_WAKE_BYTES > 128
    invocation_->retainedWakeTable_={{1,sizeof(invocation_->retainedWakeTable_),context,retainedWakeRead,retainedWakeStage,retainedWakeClear},
      RISC_RETAINED_WAKE_EXTENDED_TAG,1,RISC_RETAINED_WAKE_BYTES,0,retainedWakeReadBytes,retainedWakeStageBytes};
#else
    invocation_->retainedWakeTable_={1,sizeof(invocation_->retainedWakeTable_),context,retainedWakeRead,retainedWakeStage,retainedWakeClear};
#endif
    grant.api=&invocation_->retainedWakeTable_;
  } else if (allowed->fileOpenSource) {
    grant.api=fileOpenSourceApi();
  } else if (allowed->fileOpen) {
    grant.api=fileOpenApi();
  } else if (allowed->driver==AppDataExportDriver) {
    if(!acquireAppDataExport()) {
      if(invocation_->appDataExport_ && invocation_->appDataExport_->retained()) {
        retained_=true;fail("app-data export admission retained");(void)appExitBarrier(false);
      }
      return false;
    }
    grant.api=&invocation_->appDataExportTable_;
  } else if (allowed->driver==AppDataDriver || allowed->driver==SharedDataDriver) {
    if(invocation_->appDataContext_ || !providerStorageSafe())return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    invocation_->appDataContext_=context;invocation_->appDataNamespace_=uint32_t(allowed->instance);invocation_->appDataSharedFile_=allowed->driver==SharedDataDriver?allowed->sharedFile:nullptr;
    invocation_->appDataTable_={RISC_APP_DATA_API_V1,sizeof(risc_app_data_v1),context,appDataStat,appDataRead,appDataReplace};grant.api=&invocation_->appDataTable_;
  } else if (allowed->installedFiles) {
    if(!invocation_->installedFiles_ || invocation_->installedVolumeContext_ || !providerStorageSafe())return false;
    void* context=nextKeyValueContext(keyValueGeneration);if(!context)return false;
    if(!invocation_->installedFiles_->end())return false;
    invocation_->installedVolumeContext_=context;invocation_->installedVolume_=volumeTable(context);grant.api=&invocation_->installedVolume_;
  } else if (allowed->driver>=0) {
    if(!acquireProviderDriver(allowed->driver,capability,api,grant))return false;
  } else grant.api=platforms_[allowed->platform].table;
  const auto* header=static_cast<const uint32_t*>(grant.api);
  if (!header || header[0]!=api || header[1]<8) {
    if (grant.provider.slot && !graph_.release(grant.provider)) {
      retained_=true;fail("invalid app interface retained");if(residentEnabled_)(void)appExitBarrier(false);return false;
    }
    grant={}; return false;
  }
  grant.live=true;grant.generation=++grantGeneration_;grant.invocation=invocation_->streams_.context();
#if RISC_RUNTIME_STORAGE_FILES
  if(allowed->driver!=StorageFilesCatalogDriver)
#endif
  serviceProviders();
  if(!appExitBarrier(false))return false;
  if(catalog && !catalogCustody())return catalogRetain();
  out->slot=slot+1;out->generation=grant.generation;out->api=grant.api;return true;
}
bool Runtime::acquireProviderDriver(PolicyIndex index,const char* capability,uint32_t api,AppGrant& grant) {
    serviceProviders();
    if(!appExitBarrier(false))return false;
    const auto& driver=drivers_[index];
    if(demandRetention_ && !demandActivation_ && !grants_[index].slot) {
      // Only the first real acquisition after promotion creates session custody.
      // Keep the normal loader/dependency ordering and serialize start callbacks.
      if(!promotionSafe())return false;
      promotionRunning_=true;
      auto& pin=grants_[index];
      pin=graph_.acquireFrom(driver.id,capability,api,driver.instance);
      const bool safe=promotionSafe();
      promotionRunning_=false;
      if(!safe) {
        retained_=true;invocation_->active_=false;fail("provider activation retained; restart required");
        if(residentEnabled_)(void)appExitBarrier(false);
        return false;
      }
      if(!pin.slot)return fail(graph_.lastError());
    }
    grant.provider=graph_.acquireFrom(driver.id,capability,api,driver.instance);
    if (!grant.provider.slot) {serviceProviders();(void)appExitBarrier(false);return false;}
    grant.api=graph_.interfaceFor(grant.provider);
    return true;
}
int32_t Runtime::randomFill(void* context,void* bytes,uint32_t length){
 Runtime* r=currentRuntime;if(!r||!r->active()||r->retained_||!context||!r->port_.randomBytes)return RISC_RANDOM_CONTEXT;
 bool found=false;for(const auto& g:r->invocation_->appGrants_)if(g.live&&g.invocation==r->invocation_->streams_.context()&&g.api==&g.random&&g.random.context==context)found=true;
 if(!found)return RISC_RANDOM_CONTEXT;
 if(!bytes||!length||length>RISC_RANDOM_MAX_BYTES)return RISC_RANDOM_INVALID;
 return r->port_.randomBytes(bytes,length)==RISC_RANDOM_OK?RISC_RANDOM_OK:RISC_RANDOM_UNAVAILABLE;
}
int32_t Runtime::memoryHeapSnapshot(void* context,risc_memory_heap_snapshot_v1* out) {
  Runtime* r=currentRuntime;
  if(!r || !r->active() || r->retained_ || !context || !r->port_.memoryHeap)return RISC_MEMORY_CONTEXT;
  bool found=false;
  for(const auto& grant:r->invocation_->appGrants_)
    if(grant.live && grant.invocation==r->invocation_->streams_.context() && grant.api==&grant.memoryHeap && grant.memoryHeap.context==context)found=true;
  if(!found)return RISC_MEMORY_CONTEXT;
  if(!out || out->struct_size<sizeof(*out) || out->memory_class>RISC_MEMORY_DMA)return RISC_MEMORY_INVALID;
  risc_memory_heap_snapshot_v1 value{};value.struct_size=sizeof(value);value.memory_class=out->memory_class;
  const int32_t rc=r->port_.memoryHeap(&value);
  if(rc!=RISC_MEMORY_OK)return rc==RISC_MEMORY_UNSUPPORTED?rc:RISC_MEMORY_INVALID;
  if(value.largest_block>value.free_bytes || value.free_bytes>value.total_bytes)return RISC_MEMORY_INVALID;
  *out=value;return RISC_MEMORY_OK;
}
int32_t Runtime::keyValueGet(void* context,const char* key,void* buffer,uint32_t capacity,uint32_t* outSize) {
  if (outSize) *outSize=0;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE || RISC_RUNTIME_PREFERENCE_STORE || RISC_RUNTIME_STORAGE_FILES
  const auto owner=atomicKeyValueOwner.load(std::memory_order_acquire);if(!owner || !owner())return RISC_KEY_VALUE_CONTEXT;
#endif
  Runtime* r=currentRuntime;
  if (!r || !r->active() || !context || !r->settings_) return RISC_KEY_VALUE_CONTEXT;
  const AppGrant* matched=nullptr;
  for (const auto& grant:r->invocation_->appGrants_) if (grant.live && grant.keyValueNamespace && grant.keyValue.context==context) matched=&grant;
  if (!matched) return RISC_KEY_VALUE_CONTEXT;
  if (!outSize || !keyValueKey(key) || (!buffer && capacity)) return RISC_KEY_VALUE_INVALID;
  const uint32_t limit=matched->keyValue.api_version==RISC_KEY_VALUE_API_V2?RISC_KEY_VALUE_V2_BLOB_MAX:RISC_KEY_VALUE_BLOB_MAX;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(r->atomicReadThrough(matched->keyValueNamespace))return r->atomicLegacyGet(matched->keyValueNamespace,key,buffer,capacity,outSize,limit);
#endif
  return r->namespaceKeyValueGet(matched->keyValueNamespace,key,buffer,capacity,outSize,limit);
}
bool Runtime::settingsReadSafe() const {return settings_&&!retained_&&!settings_->retained()&&port_.owner&&port_.owner();}
uint32_t Runtime::settingsMaxBlobSize() {
  if(!settingsCallSafe())return 0;
  const uint32_t size=settings_->maxBlobSize();return settingsCallSafe()?size:0;
}
bool Runtime::settingsCallSafe() {
  if(settings_&&settings_->retained()){
    retained_=true;fail("settings provider custody retained; restart required");(void)appExitBarrier(false);
  }
  return settings_&&!retained_&&port_.owner&&port_.owner();
}
int32_t Runtime::namespaceKeyValueGet(uint32_t ns,const char* key,void* buffer,uint32_t capacity,uint32_t* outSize,uint32_t limit,bool provider) {
  if(outSize)*outSize=0;
  if(!settingsCallSafe())return RISC_KEY_VALUE_CONTEXT;
  uint8_t bytes[RISC_KEY_VALUE_V2_BLOB_MAX];RiscRuntime::ScopedBufferWipe wipe(bytes);uint32_t size=0;
  if(!outSize||(!buffer&&capacity)||limit>sizeof bytes)return RISC_KEY_VALUE_INVALID;
  const int32_t rc=settings_->get(ns,key,bytes,limit,&size,limit,provider);
  if(!settingsCallSafe())return RISC_KEY_VALUE_CONTEXT;
  if(rc==RISC_KEY_VALUE_NOT_FOUND||rc==RISC_KEY_VALUE_BUSY)return rc;
  if(rc!=RISC_KEY_VALUE_OK||!size||size>limit)return RISC_KEY_VALUE_IO;
  if(capacity<size){*outSize=size;return RISC_KEY_VALUE_BUFFER_SMALL;}
  std::memcpy(buffer,bytes,size);*outSize=size;return RISC_KEY_VALUE_OK;
}
int32_t Runtime::keyValuePut(void* context,const char* key,const void* data,uint32_t size) {
#if RISC_RUNTIME_ATOMIC_KEY_VALUE || RISC_RUNTIME_PREFERENCE_STORE || RISC_RUNTIME_STORAGE_FILES
  const auto owner=atomicKeyValueOwner.load(std::memory_order_acquire);if(!owner || !owner())return RISC_KEY_VALUE_CONTEXT;
#endif
  Runtime* r=currentRuntime;
  if (!r || !r->active() || !context || !r->settings_) return RISC_KEY_VALUE_CONTEXT;
  const AppGrant* matched=nullptr;
  for (const auto& grant:r->invocation_->appGrants_) if (grant.live && grant.keyValueNamespace && grant.keyValue.context==context) matched=&grant;
  if (!matched) return RISC_KEY_VALUE_CONTEXT;
  const uint32_t limit=matched->keyValue.api_version==RISC_KEY_VALUE_API_V2?RISC_KEY_VALUE_V2_BLOB_MAX:RISC_KEY_VALUE_BLOB_MAX;
  if (!keyValueKey(key) || !data || !size || size>limit) return RISC_KEY_VALUE_INVALID;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(r->atomicReadThrough(matched->keyValueNamespace))return RISC_KEY_VALUE_CONTEXT;
#endif
  return r->namespaceKeyValuePut(matched->keyValueNamespace,key,data,size);
}
int32_t Runtime::namespaceKeyValuePut(uint32_t ns,const char* key,const void* data,uint32_t size,bool preference,bool provider) {
#if RISC_RUNTIME_PREFERENCE_STORE
  auto* tracked=preferenceNamespace(ns);
  if(tracked && ((!preference && tracked->pending) || tracked->revision==UINT64_MAX))return RISC_KEY_VALUE_BUSY;
#else
  (void)preference;
#endif
  if(!settingsCallSafe())return RISC_KEY_VALUE_CONTEXT;
  const int32_t result=settings_->put(ns,key,data,size,provider);
#if RISC_RUNTIME_PREFERENCE_STORE
  // A failed commit can have changed storage. Only BUSY promises no I/O.
  if(tracked && result!=RISC_KEY_VALUE_BUSY)++tracked->revision;
#endif
  if(!settingsCallSafe())return RISC_KEY_VALUE_CONTEXT;
  return result==RISC_KEY_VALUE_OK || result==RISC_KEY_VALUE_BUSY ? result : RISC_KEY_VALUE_IO;
}

bool Runtime::providerPolicy(JsonObjectConst selection,ProviderStorage& storage) {
  JsonVariantConst value=selection["key_value"];
  // An omitted optional map differs from explicit null.
  if (value.isUnbound()) return true;
  if (!value.is<JsonArrayConst>() || !value.size() || value.size()>ProviderStorage::MaxKeys) return fail("invalid provider key-value map");
  for (JsonObjectConst item:value.as<JsonArrayConst>()) {
    auto& entry=storage.keys[storage.count]; int64_t nameSpace=0;
    if (!keys(item,{"key","namespace","access"}) ||
        !text(item["key"],entry.key,sizeof(entry.key)) || !keyValueKey(entry.key) ||
        !integer(item["namespace"],1,INT32_MAX,nameSpace) ||
        (!eq(item["access"],"read") && !eq(item["access"],"read-write"))) return fail("invalid provider key-value entry");
    for (size_t i=0;i<storage.count;++i) if (!strcmp(entry.key,storage.keys[i].key)) return fail("duplicate provider key-value key");
    entry.nameSpace=static_cast<uint32_t>(nameSpace);
    entry.writable=eq(item["access"],"read-write");
    ++storage.count;
  }
  storage.owner=this;
  storage.table={RISC_BOUND_KEY_VALUE_API_V1,sizeof(risc_bound_key_value_v1),nullptr,boundKeyValueGet,boundKeyValuePut};
  return true;
}
bool Runtime::providerStorageSafe() const {
  if((settings_ && settings_->retained()) || entropyRetained_ || tcpRetained_ || metadataCloseRetained() || !appDataExitSafe())return false;
  if(invocation_->installedFiles_ && invocation_->installedFiles_->retained())return false;
  if(port_.providerStorageSafe)return port_.providerStorageSafe();
  return !port_.appExitSafe || port_.appExitSafe();
}
bool Runtime::beginProvider(void* context) {
  auto* storage=static_cast<ProviderStorage*>(context);
  Runtime* r=currentRuntime;
  if (!storage || !r || storage->owner!=r || storage->live ||
      !r->port_.owner() || r->retained_ || !r->providerStorageSafe()) return false;
  // A graph-wide retained-storage fence also covers providers that access files
  // indirectly through another provider. They need no callable storage token.
  if(!storage->count && !storage->fileCount && !storage->needsRealtime && !storage->needsTcp && !storage->needsEntropy){storage->live=true;return true;}
  void* token=nextKeyValueContext(keyValueGeneration);
  if (!token) return false;
  storage->table.context=token;
  if(storage->fileCount)storage->fileTable.base.context=token;
  if(storage->needsRealtime)storage->realtime.context=token;
  if(storage->needsTcp)storage->tcp.context=token;
  if(storage->needsEntropy)storage->entropy.context=token;
  storage->live=true;
  return true;
}
void Runtime::revokeProvider(void* context) {
  if(context){
    auto* storage=static_cast<ProviderStorage*>(context);
    storage->live=false;
    if(storage->needsEntropy && storage->entropy.context && storage->owner->entropyBackend_ &&
       !storage->owner->entropyBackend_->idle(storage->owner->entropyBackend_->context,reinterpret_cast<uintptr_t>(storage->entropy.context)))
      storage->owner->entropyRetained_=true;
    if(storage->needsTcp && storage->tcp.context && storage->owner->tcpBackend_ &&
       !storage->owner->tcpBackend_->idle(storage->owner->tcpBackend_->context,reinterpret_cast<uintptr_t>(storage->tcp.context)))
      storage->owner->tcpRetained_=true;
  }
}
void Runtime::revokeProviders() {
  for (auto& storage:providerStorage_) revokeProvider(&storage);
}
Runtime::ProviderStorage* Runtime::providerContext(void* context) {
#if RISC_RUNTIME_ATOMIC_KEY_VALUE || RISC_RUNTIME_PREFERENCE_STORE || RISC_RUNTIME_STORAGE_FILES
  const auto owner=atomicKeyValueOwner.load(std::memory_order_acquire);if(!owner || !owner())return nullptr;
#endif
  Runtime* r=currentRuntime;
  if (!r || !context || !r->port_.owner() || !r->settings_) return nullptr;
  if (r->retained_ || !r->providerStorageSafe()) {
    // Native retention can become observable inside app_main, before the
    // outer appExitBarrier. Revoke without invoking any physical cleanup.
    r->revokeProviders();
    return nullptr;
  }
  for (auto& storage:r->providerStorage_)
    if (storage.owner==r && storage.live && storage.table.context==context) return &storage;
  return nullptr;
}
int32_t Runtime::boundKeyValueGet(void* context,const char* key,void* buffer,uint32_t capacity,uint32_t* outSize) {
  if (outSize) *outSize=0;
  const ProviderStorage* storage=providerContext(context);
  if (!storage) return RISC_BOUND_KEY_VALUE_CONTEXT;
  if (!outSize || !keyValueKey(key) || (!buffer && capacity)) return RISC_BOUND_KEY_VALUE_INVALID;
  const ProviderKey* matched=nullptr;
  for (size_t i=0;i<storage->count;++i) if (!strcmp(storage->keys[i].key,key)) matched=&storage->keys[i];
  if (!matched) return RISC_BOUND_KEY_VALUE_CONTEXT;
  const uint32_t limit=storage->table.api_version==RISC_BOUND_KEY_VALUE_API_V2?RISC_BOUND_KEY_VALUE_V2_BLOB_MAX:RISC_BOUND_KEY_VALUE_BLOB_MAX;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(storage->owner->atomicReadThrough(matched->nameSpace))return storage->owner->atomicLegacyGet(matched->nameSpace,key,buffer,capacity,outSize,limit);
#endif
  return storage->owner->namespaceKeyValueGet(matched->nameSpace,key,buffer,capacity,outSize,limit,true);
}
int32_t Runtime::boundKeyValuePut(void* context,const char* key,const void* data,uint32_t size) {
  const ProviderStorage* storage=providerContext(context);
  if (!storage) return RISC_BOUND_KEY_VALUE_CONTEXT;
  const uint32_t limit=storage->table.api_version==RISC_BOUND_KEY_VALUE_API_V2?RISC_BOUND_KEY_VALUE_V2_BLOB_MAX:RISC_BOUND_KEY_VALUE_BLOB_MAX;
  if (!keyValueKey(key) || !data || !size || size>limit) return RISC_BOUND_KEY_VALUE_INVALID;
  const ProviderKey* matched=nullptr;
  for (size_t i=0;i<storage->count;++i) if (!strcmp(storage->keys[i].key,key)) matched=&storage->keys[i];
  if (!matched || !matched->writable) return RISC_BOUND_KEY_VALUE_CONTEXT;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(storage->owner->atomicReadThrough(matched->nameSpace))return RISC_BOUND_KEY_VALUE_CONTEXT;
#endif
  return storage->owner->namespaceKeyValuePut(matched->nameSpace,key,data,size,false,true);
}

bool Runtime::release(risc_runtime_capability_v1* out) {
#if RISC_RUNTIME_STORAGE_FILES
  if(storageFilesCalling_)return false;
#endif
  if (promotionRunning_ || graph_.lifecycleBusy() || !active() || !out || out->struct_size<sizeof(*out) || !out->slot || out->slot>16) return false;
  if(!appDataExitSafe()){(void)appExitBarrier();return false;}
  auto& grant=invocation_->appGrants_[out->slot-1];
  if (!grant.live || grant.generation!=out->generation || grant.api!=out->api || grant.invocation!=invocation_->streams_.context()) return false;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(grant.atomicEndpoint && !grant.atomicEndpoint->exitSafe()) {
    if(grant.atomicEndpoint->retained())(void)atomicResult(RISC_ATOMIC_RETAINED);
    return false; // Pending receipt stays collectible by this exact live grant.
  }
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
  if(grant.preferenceOperation)return false;
#endif
#if RISC_RUNTIME_STORAGE_FILES
  if(grant.storageFilesAdapter && !grant.storageFilesAdapter->exitSafe()) {
    if(grant.storageFilesAdapter->retained())(void)storageFilesResult(RISC_FILES_RETAINED);
    return false;
  }
#endif
  RuntimeStreams::AppStreamBinding binding{grant.invocation,out->slot,grant.generation,grant.api,grant.provider};
  uint64_t providerContext=0;
  const bool streamProvider=grant.provider.slot && graph_.streamSessionsFor(grant.provider,&providerContext);
  if (!invocation_->streams_.closeGrant(binding)) return false;
  if (grant.provider.slot && !graph_.release(grant.provider)) {
    if(streamProvider)streamRetain(this);
    else if(residentEnabled_)(void)appExitBarrier();
    return false;
  }
  if(grant.api==&invocation_->appDataExportTable_ && !endAppDataExport()) {
    retained_=true;fail("app-data export cleanup retained");(void)appExitBarrier();return false;
  }
  if(grant.api==&invocation_->installedVolume_){if(!invocation_->installedFiles_->end()){if(residentEnabled_)(void)appExitBarrier();return false;}invocation_->installedVolumeContext_=nullptr;}
  if(grant.api==&invocation_->appDataTable_){if(!appDataExitSafe()){if(residentEnabled_)(void)appExitBarrier();return false;}invocation_->appDataContext_=nullptr;invocation_->appDataNamespace_=0;invocation_->appDataSharedFile_=nullptr;}
  if(grant.api==&invocation_->appCatalogTable_)invocation_->appCatalogContext_=nullptr;
  if(grant.api==&invocation_->promotionTable_)invocation_->promotionContext_=nullptr;
  if(grant.api==&invocation_->realtimeTable_)invocation_->realtimeContext_=nullptr;
  if(grant.api==&invocation_->realtimeControlTable_)invocation_->realtimeControlContext_=nullptr;
  if(grant.api==&invocation_->retainedWakeTable_){port_.retainedWake->cancel();invocation_->retainedWakeContext_=nullptr;}
#if RISC_NATIVE_SETTINGS_LIFECYCLE && RISC_RUNTIME_ATOMIC_KEY_VALUE
  const bool releasedAtomic=grant.atomicEndpoint!=nullptr || (grant.keyValueNamespace && keyValueAvailability(grant.keyValueNamespace) && !keyValueAuthoritative(grant.keyValueNamespace));
#endif
  grant={};out->slot=out->generation=0;out->api=nullptr;
#if RISC_NATIVE_SETTINGS_LIFECYCLE && RISC_RUNTIME_ATOMIC_KEY_VALUE
  // The last live atomic grant no longer protects a draft. This is a native-only
  // settled boundary; preserve the existing ordinary provider-service schedule.
  if(releasedAtomic){serviceNativeSettings();if(retained_)return false;}
#endif
  return true;
}
bool Runtime::revokeApp() {
#if RISC_RUNTIME_STORAGE_FILES
  if(!storageFilesExitSafe(true)){(void)appExitBarrier();return false;}
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
  if(!preferenceExitSafe(true)){(void)appExitBarrier();return false;}
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  if(!atomicExitSafe(true)){(void)appExitBarrier();return false;}
#endif
  if(!invocation_->streams_.closeAll())return false;
  for (auto& grant:invocation_->appGrants_) if (grant.live) {
    if (grant.provider.slot && !graph_.release(grant.provider)) {
      retained_=true;(void)appExitBarrier();return false;
    }
    if(grant.api==&invocation_->appDataExportTable_ && !endAppDataExport()) {
      retained_=true;fail("app-data export cleanup retained");(void)appExitBarrier();return false;
    }
    if(grant.api==&invocation_->appDataTable_) {
      if(!appDataExitSafe()){retained_=true;(void)appExitBarrier();return false;}
      invocation_->appDataContext_=nullptr;invocation_->appDataNamespace_=0;invocation_->appDataSharedFile_=nullptr;
    }
    if(grant.api==&invocation_->installedVolume_) {
      if(!invocation_->installedFiles_->end()){retained_=true;(void)appExitBarrier();return false;}
      invocation_->installedVolumeContext_=nullptr;
    }
    if(grant.api==&invocation_->appCatalogTable_)invocation_->appCatalogContext_=nullptr;
    if(grant.api==&invocation_->promotionTable_)invocation_->promotionContext_=nullptr;
    if(grant.api==&invocation_->realtimeTable_)invocation_->realtimeContext_=nullptr;
    if(grant.api==&invocation_->realtimeControlTable_)invocation_->realtimeControlContext_=nullptr;
    if(grant.api==&invocation_->retainedWakeTable_){port_.retainedWake->cancel();invocation_->retainedWakeContext_=nullptr;}
    grant={};
  }
  invocation_->appPolicy_=nullptr;return true;
}
#include "AppStreamsRuntime.inc"
#include "InstalledFilesRuntime.inc"
#include "RemovableAppsRuntime.inc"
#include "AppDataRuntime.inc"
#include "AppDataExportRuntime.inc"
#include "BoundAppDataRuntime.inc"
#include "RetainedWakeRuntime.inc"
#include "RealtimeRuntime.inc"
#include "TcpListenerRuntime.inc"
#include "EntropyRuntime.inc"
#include "ProviderPromotionRuntime.inc"
#include "ResidentShellRuntime.inc"
bool Runtime::openSettings() {
  // Explicit selection never downgrades after unavailable or failed open.
  if(port_.settings)settings_=port_.settings->open?port_.settings->open(port_.settings->context):nullptr;
  else if(port_.keyValue&&port_.keyValue->openSettings)settings_=port_.keyValue->openSettings(*port_.keyValue);
  // Opening is a provider callback too: a returned session may already hold
  // terminal custody, even when this boot grants no settings capabilities.
  return (!port_.settings&&!port_.keyValue)||settingsCallSafe();
}
bool Runtime::prepare(const char* root) {
  RiscPerf::Scope trace(10,11);
  if(attempted_ || metadataCloseRetained() || !port_.owner() || !root || strlen(root)>=sizeof(root_) || root[0]!='/' || !metadataSource(root).bound()) return fail("invalid boot invocation");
  attempted_=true; strcpy(root_,root);
  if (!RuntimeProviders::nativeProviderPolicySetValid(port_.nativeProviders)) return fail("invalid native provider selection");
  if(port_.nativeProviders.count) {
    nativePolicies_=RuntimeProviders::NativeProviderPolicySnapshotV1::capture(port_.nativeProviders);
    if(!nativePolicies_)return fail("native provider selection snapshot failed");
    port_.nativeProviders=nativePolicies_->view();
  }
  char filename[256], relative[193]; JsonDocument config, boardDoc;
  if(!path(root_,"boot.json",filename,sizeof(filename)) || !readJson(metadataSource(),filename,config,&metadataCloseRetained_)) return fail("boot.json unreadable/invalid");
  JsonObjectConst c=config.as<JsonObjectConst>();
  if (!c["port"].isNull() && !board_.port(c["port"])) return fail(board_.error());
  if(!RiscUpdate::validCohortMigration(c["cohort_migration"]))return fail("invalid cohort migration policy");
  if(!keys(c,{"board","default_app","drivers"},{"port","app_capabilities","cohort_migration","provider_activation","resident_shell","installation"}) || !text(c["board"],relative,sizeof(relative)) ||
      !path(root_,relative,filename,sizeof(filename)) || !readJson(metadataSource(),filename,boardDoc,&metadataCloseRetained_) || !board_.load(boardDoc.as<JsonObjectConst>())) return fail(board_.error()[0]?board_.error():"board manifest unreadable/invalid");
  // Activation policy never filters admission, registration or image inspection.
  const auto activation=c["provider_activation"];
  if(!activation.isUnbound() && !eq(activation,"eager") && !eq(activation,"demand") && !eq(activation,"demand-retained"))
    return fail("invalid provider activation policy");
  demandRetention_=eq(activation,"demand-retained");
  demandActivation_=eq(activation,"demand") || demandRetention_;
  if(!text(c["default_app"],relative,sizeof(relative)) || !elfPath(relative) || !path(root_,relative,invocation_->current_,sizeof(invocation_->current_))) return fail("invalid default app path");
  if(!c["drivers"].is<JsonArrayConst>() || c["drivers"].size()>MaxDrivers) return fail("invalid driver list");
  // Read all manifests and validate mappings before registering/activating modules.
  for(JsonObjectConst item:c["drivers"].as<JsonArrayConst>()) {
    Driver& d=drivers_[driverCount_]; int64_t instance=0;
    if(!keys(item,{"manifest"},{"instance_id","key_value","app_data","boot_start","image_source"}) || !text(item["manifest"],relative,sizeof(relative)) ||
        !path(root_,relative,d.elf,sizeof(d.elf)) || (!item["instance_id"].isNull() && !integer(item["instance_id"],1,INT32_MAX,instance))) return fail("invalid driver selection");
    if(!item["boot_start"].isUnbound()) {
      if(!demandActivation_ || !eq(item["boot_start"],"cold"))return fail("invalid driver boot start policy");
      d.coldBootStart=true;
    }
    d.instance=instance; JsonDocument manifestDoc;
    if(!readJson(metadataSource(),d.elf,manifestDoc,&metadataCloseRetained_) || !manifest(manifestDoc.as<JsonObjectConst>(),d)) return fail(error_[0]?error_:"driver manifest unreadable/invalid");
    if(!item["image_source"].isUnbound()) {
      const auto source=item["image_source"].as<JsonObjectConst>();int64_t volume=0;char checked[256];
      if(!keys(source,{"volume_instance","path","internal_fallback"},{"capability"}) ||
         (!source["capability"].isUnbound() && !eq(source["capability"],"storage.files") && !eq(source["capability"],"storage.volume")) ||
         !integer(source["volume_instance"],eq(source["capability"],"storage.files")?0:1,INT32_MAX,volume) ||
         !text(source["path"],d.sourcePath,sizeof(d.sourcePath)) ||
         strncmp(d.sourcePath,"Drivers/",8) || !elfPath(d.sourcePath) ||
         !path("",d.sourcePath,checked,sizeof(d.sourcePath)) ||
         !source["internal_fallback"].is<bool>())return fail("invalid driver image source");
      strcpy(d.sourcePath,checked);
      d.sourceFiles=eq(source["capability"],"storage.files");
      d.sourceVolume=uint64_t(volume);d.internalFallback=source["internal_fallback"].as<bool>();d.sourceOwner=this;
    }
    if(!providerPolicy(item,providerStorage_[driverCount_]) ||
       !providerFilePolicy(item,providerStorage_[driverCount_])) return false;
    ++driverCount_;
  }
  bootDriverCount_=driverCount_;
  registrationOpen_=true;
  const bool bound=(!port_.bindPlatforms || port_.bindPlatforms(*this)) && openSettings() && registerProviderRealtime() && configureInstallation(c["installation"]);
  registrationOpen_=false;
  if (!bound) return fail("trusted platform binding failed");
  if(!validateGraph() || !appPolicies(c["app_capabilities"]) || !configureAppDataExports(c["app_capabilities"]) || !residentPolicy(c["resident_shell"]) || !configureInstalledFiles(c)) return false;
  if(!installationNamespaceRangeSafe())return fail("installation namespace range overlaps existing authority");
  bootPolicyCount_=policyCount_;
  for(size_t i=0;i<driverCount_;++i) {
    Driver& d=drivers_[i];d.sourceOwner=this;
    RuntimeProviders::SpecV2 spec{d.id,d.elf,d.provides,d.api,d.requirements,d.count};
    if(d.sourceDriver>=0){spec.openImage=openDriverImage;spec.imageContext=&drivers_[i];spec.imageProvider=drivers_[d.sourceDriver].id;spec.imageInstance=d.sourceVolume;spec.imageCapability=d.sourceFiles?"storage.files":"storage.volume";}
    spec.hardware=d.instance?&board_.device(d.instance)->hardware:nullptr;
    // Every image callback needs the shared custody fence, including failed
    // reads before a module handle exists and providers without other leases.
    providerStorage_[i].owner=this;
    spec.lease={&providerStorage_[i],beginProvider,revokeProvider,providerResourcesSafe};
    const RuntimeProviders::NativeProviderPolicyV1* selected=nullptr;
    for(size_t p=0;p<port_.nativeProviders.count;++p) {
      const auto& policy=port_.nativeProviders.entries[p];
      char selectedPath[256];
      if(!path(root_,policy.relativeElfPath,selectedPath,sizeof(selectedPath))) return fail("native provider path invalid");
      if(!strcmp(d.id,policy.driverId) || !strcmp(d.elf,selectedPath)) {
        if(selected || strcmp(d.id,policy.driverId) || strcmp(d.elf,selectedPath) ||
           strcmp(d.version,policy.version) || strcmp(d.provides,policy.capability) || d.api!=policy.api ||
           d.count!=policy.requirementCount) return fail("native provider selection mismatch");
        for(size_t r=0;r<d.count;++r)
          if(strcmp(d.requirements[r].capability,policy.requirements[r].capability) ||
             d.requirements[r].api!=policy.requirements[r].api) return fail("native provider requirement mismatch");
        selected=&policy;
      }
    }
    if(selected && d.sourceDriver>=0)return fail("native provider must remain internal");
    if(!selected && d.sourceDriver<0){spec.openImage=openDriverImage;spec.imageContext=&d;}
    if(selected ? !registerNativeProvider(d,spec,*selected) : !graph_.addVerified(spec))
      return fail(error_[0]?error_:"driver registration failed");
  }
  strcpy(default_,invocation_->current_); prepared_=true;
  if(installation_ && !reserveInstalledApps(MaxAppPolicies))return fail("installed registry reservation failed");
  return true;
}
bool Runtime::readRuntimeImage(const char* name,RuntimeFileImage& image,size_t expected,bool pinned) {
  struct Context {Runtime* runtime;std::chrono::steady_clock::time_point start,lastYield;size_t sinceYield=0;};
  const auto now=std::chrono::steady_clock::now();Context context{this,now,now,0};
  RuntimeFileImage::Hooks hooks{
    [](size_t n)->void* {
#if defined(ESP_PLATFORM) || defined(RISC_REMOVABLE_APP_TEST) || defined(RISC_APP_IMAGE_CACHE_TEST)
      return esp_elf_malloc(n,false);
#else
      return std::malloc(n);
#endif
    },
    [](void* p){
#if defined(ESP_PLATFORM) || defined(RISC_REMOVABLE_APP_TEST) || defined(RISC_APP_IMAGE_CACHE_TEST)
      esp_elf_free(p);
#else
      std::free(p);
#endif
    },
    [](void* raw)->bool {auto* r=static_cast<Context*>(raw)->runtime;return r->port_.owner() && r->providerStorageSafe();},
    [](void* raw,size_t n)->bool {
      auto& c=*static_cast<Context*>(raw);const auto at=std::chrono::steady_clock::now();
      if(at-c.start>=std::chrono::seconds(30)){errno=ETIMEDOUT;return false;}
      c.sinceYield+=n;
      if(c.sinceYield>=32*1024 || at-c.lastYield>=std::chrono::milliseconds(2)) {
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#else
        if(c.runtime->port_.delay)c.runtime->port_.delay(1);
#endif
        c.sinceYield=0;c.lastYield=std::chrono::steady_clock::now();
      }
      return std::chrono::steady_clock::now()-c.start<std::chrono::seconds(30);
    },&context};
  if(pinned) {
    hooks.allocate=[](size_t n)->void* {
#ifdef ESP_PLATFORM
      void* p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
      return p?p:heap_caps_malloc(n,MALLOC_CAP_8BIT);
#else
      return std::malloc(n);
#endif
    };
    hooks.free=[](void* p){
#ifdef ESP_PLATFORM
      heap_caps_free(p);
#else
      std::free(p);
#endif
    };
  }
  const bool ok=image.read(metadataSource(),name,hooks,expected);
  if(image.retained){retainedNativeProviderRead_=image.handle?image.handle:UINT32_MAX;retainedNativeProviderBytes_=image.bytes;metadataCloseRetained_=true;}
  errno=image.error;return ok;
}
int Runtime::readInternalImage(void* raw,const char* name,::elf_file* file) {
  if(!raw || !file){errno=EINVAL;return -1;}
  auto& runtime=*static_cast<Runtime*>(raw);RuntimeFileImage image;
  RiscPerf::emit(40);
  if(!runtime.readRuntimeImage(name,image)){RiscPerf::emit(44,uint32_t(-1));errno=image.error;return -1;}
  RiscPerf::emit(41,uint32_t(image.size));RiscPerf::emit(45);
  const bool valid=esp_elf_validate_file(image.bytes,image.size);
  if(valid)RiscPerf::emit(46);
  if(!valid ||
     !esp_elf_admit_managed_app(name,image.bytes,image.size) || !runtime.providerStorageSafe()) {
#if defined(ESP_PLATFORM) || defined(RISC_REMOVABLE_APP_TEST) || defined(RISC_APP_IMAGE_CACHE_TEST)
    esp_elf_free(image.bytes);
#else
    std::free(image.bytes);
#endif
    RiscPerf::emit(44,uint32_t(-1));errno=ENOEXEC;return -1;
  }
  file->payload=image.bytes;file->size=image.size;return 0;
}
void* Runtime::openInternalImage(const char* name,bool cached) {
  // Cache lookup may skip media entirely, but never the terminal custody fence.
  if(!port_.owner() || !providerStorageSafe())return nullptr;
#if defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST)
  if(cached)return esp_dlopen_cached_reader_instance(&appImages_,name,readInternalImage,this);
#else
  (void)cached;
#endif
  return esp_dlopen_reader_instance(name,readInternalImage,this);
}
bool Runtime::registerNativeProvider(const Driver& d,RuntimeProviders::SpecV2& spec,
                                     const RuntimeProviders::NativeProviderPolicyV1& policy) {
  // Graph admission copies/hashes this one provider-backed input snapshot.
  RuntimeFileImage image;
  bool ok=readRuntimeImage(d.elf,image,policy.elfLength,true);
  auto* bytes=image.bytes;
  if(ok) {
    spec.requiredOsCpuAbi=policy.osCpuAbi;spec.verifiedElfBytes=bytes;
    spec.verifiedElfLength=policy.elfLength;spec.declaredImports=policy.imports;
    spec.declaredImportCount=policy.importCount;memcpy(spec.contentSha256,policy.sha256,32);
    ok=RuntimePackages::DeviceProviderExecutorV2::registerNativePolicy(graph_,spec,policy);
  }
  if(bytes && !image.retained) {
#ifdef ESP_PLATFORM
    heap_caps_free(bytes);
#else
    std::free(bytes);
#endif
  }
  return ok || fail("native provider image admission failed");
}
const RuntimeProviders::NativeProviderPolicyV1* Runtime::nativeProviderPolicyForImage(const char* image) const {
  if(!prepared_ || metadataCloseRetained() || !image || !port_.owner())return nullptr;
  const RuntimeProviders::NativeProviderPolicyV1* selected=nullptr;
  for(size_t i=0;i<driverCount_;++i) {
    const auto& d=drivers_[i];
    if(strcmp(d.elf,image))continue;
    const auto* policy=graph_.nativePolicyFor(d.id,d.provides,d.api,d.instance);
    if(!policy)return nullptr;
    // Multiple independent hardware instances may share one pinned image.
    if(selected && (strcmp(selected->driverId,policy->driverId) ||
       memcmp(selected->sha256,policy->sha256,32)))return nullptr;
    selected=policy;
  }
  return selected;
}
bool Runtime::inspectImages(bool (*inspect)(void*,const char*,bool),void* context) const {
  if(!prepared_ || invocation_->active_ || retained_ || metadataCloseRetained() || !port_.owner() || !inspect)return false;
  if(!inspect(context,default_,false))return false;
  for(size_t i=0;i<policyCount_;++i){
    if(policies_[i].sourceDriver>=0 || !strcmp(policies_[i].elf,default_))continue;
    if(!port_.owner() || !inspect(context,policies_[i].elf,false))return false;
  }
  for(size_t i=0;i<driverCount_;++i){
    bool duplicate=false;for(size_t j=0;j<i;++j)if(!strcmp(drivers_[j].elf,drivers_[i].elf)){duplicate=true;break;}
    if(!duplicate && (!port_.owner() || !inspect(context,drivers_[i].elf,true)))return false;
  }
  return port_.owner();
}
#include "CohortRuntime.inc"
bool Runtime::installedLaunchPath(const char* relative,char* out,size_t capacity,bool* settingsPending)const {
  if(settingsPending)*settingsPending=false;
  if(!relative||!path(root_,relative,out,capacity))return false;
  auto ready=[&](size_t index){
    if(!settings_)return true;
    const auto& policy=policies_[index];
    for(size_t g=0;g<policy.count;++g){const auto& grant=policy.grants[g];
      if(keyValuePolicy(grant)){
        if(!settingsReadSafe())return false;
        const bool ready=settings_->launchReady(uint32_t(grant.instance),settingsPending);
        if(!settingsReadSafe()||!ready)return false;
      }
    }
    return true;
  };
  for(size_t i=0;i<bootPolicyCount_;++i)if(policies_[i].catalogAuthority&&!strcmp(policies_[i].elf,out)){
    for(size_t j=bootPolicyCount_;j<policyCount_;++j)if(policies_[j].enabled&&!strcmp(policies_[j].id,policies_[i].id)){
      const size_t n=strlen(policies_[j].elf);if(n>=capacity)return false;
      memcpy(out,policies_[j].elf,n+1);return ready(j);
    }
    out[0]=0;return false;
  }
  for(size_t i=bootPolicyCount_;i<policyCount_;++i)if(policies_[i].enabled&&!strcmp(policies_[i].elf,out))return ready(i);
  // The configured default must run to recover cold SD settings. Other bundled
  // applications cannot initialize from BUSY-as-empty or strand the catalog
  // worker inside a foreground client that has no restoration authority.
  for(size_t i=0;i<bootPolicyCount_;++i)if(policies_[i].enabled&&!strcmp(policies_[i].elf,out))
    return !strcmp(out,default_)||ready(i);
  return true;
}
bool Runtime::launch(const char* relative) {
  if(residentEnabled_) {
    char selected[256];
    const bool legacy=invocation_==&mainInvocation_ && !invocation_->role && residentLegacyAllowed(invocation_->current_);
    if((invocation_->role!=RISC_RESIDENT_ROLE_FOREGROUND && !legacy) || !invocation_->entryRunning_ ||
       !relative || !installedLaunchPath(relative,selected,sizeof(selected)) ||
       (!residentAllowed(selected) && !residentLegacyAllowed(selected) && !(legacy && !strcmp(selected,default_))))return false;
  }
  if(invocation_->fileOpen_.phase==FileOpenState::Phase::Requested || invocation_->fileOpen_.phase==FileOpenState::Phase::Receiving)return false;
  if(promotionRunning_ || graph_.lifecycleBusy() || !active() || metadataCloseRetained() || !appDataExitSafe() || (invocation_->installedFiles_ && invocation_->installedFiles_->retained()) || invocation_->queued_[0] || appDataBusy() || !relative || !elfPath(relative) || (port_.appExitSafe && !port_.appExitSafe())) return false;
  char selected[sizeof(invocation_->queued_)]{};
  const bool ok=installedLaunchPath(relative,selected,sizeof(selected));
  if(ok)std::strcpy(invocation_->queued_,selected);
  RISC_STAGE_LOG("app launch-request file=%s result=%s",relative,ok?"accepted":"invalid-path");
  if(ok)RiscPerf::emit(20);
  return ok;
}
bool Runtime::exitRequested()const {return active() && port_.controlExitRequested && port_.controlExitRequested();}
bool Runtime::health(risc_runtime_health_v1* h) { return active() && h && h->struct_size>=sizeof(*h) && port_.health(h); }
bool Runtime::monotonicMs(uint32_t* out) {
  if(!active() || !out)return false;
  if(port_.monotonicMs)return port_.monotonicMs(out);
  risc_runtime_health_v1 h{};h.struct_size=sizeof(h);
  if(!port_.health(&h))return false;
  *out=h.uptime_ms;return true;
}
bool Runtime::launchDefault() {
  if(residentEnabled_ && invocation_->role!=RISC_RESIDENT_ROLE_FOREGROUND &&
     !(invocation_==&mainInvocation_ && !invocation_->role && residentLegacyAllowed(invocation_->current_)))return false;
  if(promotionRunning_ || graph_.lifecycleBusy() || !active() || !invocation_->entryRunning_ ||
     retained_ || !graph_.activationSafe() || !providerStorageSafe() || invocation_->queued_[0] ||
     (port_.appExitSafe && !port_.appExitSafe()) ||
     invocation_->fileOpen_.phase==FileOpenState::Phase::Requested)return false;
  if(invocation_->fileOpen_.phase==FileOpenState::Phase::Receiving &&
     (invocation_->fileOpen_.receiver<0 || size_t(invocation_->fileOpen_.receiver)>=policyCount_ ||
      invocation_->appPolicy_!=&policies_[invocation_->fileOpen_.receiver]))return false;
  strcpy(invocation_->queued_,default_);
  RISC_STAGE_LOG("app default-request file=%s result=accepted",default_);
  RiscPerf::emit(20);
  return true;
}
bool Runtime::confirmBoot() {
  return !promotionRunning_ && active() && invocation_->defaultRunning_ && invocation_->entryRunning_ && !invocation_->queued_[0] && !retained_ &&
    providerStorageSafe() && (!port_.confirmBoot || port_.confirmBoot());
}
bool Runtime::appUpdate(const char* id,const void* bytes,size_t size,UpdateApp& out) const {
  out={};
  if(!active() || !id || !bytes || !size || size>4096 || !providerStorageSafe())return false;
  const AppPolicy* policy=nullptr;
  for(size_t p=0;p<policyCount_;++p)if(!strcmp(id,policies_[p].id))policy=&policies_[p];
  if(!policy || policy->sourceDriver>=0)return false;
  // One declared KV capability may have several explicit namespace grants.
  // Compare the candidate to the unique declarations, preserving every
  // owner-provisioned grant in the immutable boot policy independently.
  size_t required=0;
  for(size_t i=0;i<policy->count;++i){
    bool seen=false;
    for(size_t j=0;j<i;++j)if(policy->grants[j].api==policy->grants[i].api &&
        !strcmp(policy->grants[j].capability,policy->grants[i].capability))seen=true;
    if(!seen)++required;
  }
  JsonDocument doc;
  if(!parse(static_cast<const char*>(bytes),size,doc))return false;
  JsonObjectConst m=doc.as<JsonObjectConst>();char version[32];
  const char* basename=strrchr(policy->elf,'/');if(!basename)return false;++basename;
  if(!keys(m,{"type","id","version","architecture","file_name","entry","requires"},{"description","display_name","icon","supported_file_types","launcher_visible","min_firmware_version","optional"}) ||
     !eq(m["type"],"application") || !eq(m["id"],id) || !eq(m["architecture"],"xtensa-esp32s3") ||
     !eq(m["entry"],"app_main") || !eq(m["file_name"],basename) || !text(m["version"],version,sizeof(version)) ||
     !RuntimePackages::safeVersion(version))return false;
  const AppDeclarations declarations(m);
  if(declarations.error(MaxAppRequirements))return false;
  FileOpenMetadata fileMetadata;
  if(!fileOpenMetadata(m,fileMetadata))return false;
  uint32_t candidate[3],current[3];
  if(!RiscUpdate::parseVersion(version,candidate) || !RiscUpdate::parseVersion(policy->version,current) ||
     RiscUpdate::compareVersion(candidate,current)<=0)return false;
  bool matched[MaxAppPolicyGrants]{};
  size_t matchedDeclarations=0;
  for(size_t declaration=0;declaration<declarations.size();++declaration) {
    const JsonObjectConst req=declarations[declaration];
    char name[96];int64_t api;
    if(!keys(req,{"capability","api"}) || !text(req["capability"],name,sizeof(name)) || !integer(req["api"],1,UINT32_MAX,api))return false;
    size_t g=0;for(;g<policy->count;++g)if(!strcmp(name,policy->grants[g].capability) && uint32_t(api)==policy->grants[g].api)break;
    if(g==policy->count){if(declarations.required(declaration))return false;continue;}
    if(matched[g])return false;
    matched[g]=true;++matchedDeclarations;
  }
  // Every immutable owner-granted declaration remains represented, including
  // optional grants. New optional rows never manufacture update authority.
  if(matchedDeclarations!=required)return false;
  size_t rootLength=strlen(root_);
  if(strncmp(policy->elf,root_,rootLength) || policy->elf[rootLength]!='/' || strlen(policy->elf+rootLength+1)>=sizeof(out.elf))return false;
  strcpy(out.elf,policy->elf+rootLength+1);
  return appManifestPath(size_t(policy-policies_.get()),out.manifest,sizeof(out.manifest));
}
bool Runtime::appManifestPath(size_t index,char* out,size_t capacity) const {
  if(metadataCloseRetained() || index>=policyCount_ || !out)return false;
  // Keep per-app path storage out of scarce static internal DRAM. The immutable
  // selected boot policy supplies this path; no caller filename is accepted.
  char filename[256];JsonDocument boot;
  if(!path(root_,"boot.json",filename,sizeof(filename)) || !readJson(metadataSource(),filename,boot,&metadataCloseRetained_))return false;
  return text(boot["app_capabilities"][index]["manifest"],out,capacity);
}
size_t Runtime::appInventoryCount() const {
  size_t count=0;for(size_t i=0;i<policyCount_;++i)if(policies_[i].enabled&&!policies_[i].catalogAuthority)++count;return count;
}
bool Runtime::appInventoryEntry(size_t index,void* output,size_t capacity,uint32_t* actual) const {
  if(actual)*actual=0;
  for(size_t i=0;i<policyCount_;++i)if(policies_[i].enabled&&!policies_[i].catalogAuthority){
    if(!index)return appInventory(i,output,capacity,actual);
    --index;
  }
  return false;
}
bool Runtime::appInventory(size_t index,void* output,size_t capacity,uint32_t* actual) const {
  if(actual)*actual=0;
  if(!active() || index>=policyCount_ || !output || !actual || !capacity || capacity>4096 || !providerStorageSafe())return false;
  if(policies_[index].catalogAuthority || !policies_[index].enabled)return false;
  if(index>=bootPolicyCount_ && installedMetadata_) {
    const char* manifest=installedMetadata_[index].manifest;
    const size_t n=strlen(manifest);
    if(!n || n>capacity)return false;
    memcpy(output,manifest,n);*actual=uint32_t(n);return true;
  }
  char relative[193],name[256];
  if(!appManifestPath(index,relative,sizeof(relative)) || !path(root_,relative,name,sizeof(name)))return false;
  auto source=metadataSource();uint32_t handle=0;
  if(source.open(name,&handle)!=RISC_FILES_OK)return false;
  uint32_t n=0;bool ok=source.readToEnd(handle,output,uint32_t(capacity),&n) && n;
  if(source.close(handle)!=RISC_FILES_OK)ok=false;
  if(!ok || source.retained()){memset(output,0,n);return false;}
  *actual=n;return true;
}
Runtime::GrantUsage Runtime::grantUsage() const {
  GrantUsage out{};
  out.selectedProviders=driverCount_;out.providerCapacity=MaxDrivers;
  out.graphCapacity=RuntimeProviders::GraphV2::kMaxGrants;
  out.graphLive=graph_.liveGrants();out.graphPeak=graph_.peakLiveGrants();
  for(const auto& pin:grants_)if(graph_.holdsGrant(pin))++out.bootPins;
  auto count=[this](const Invocation& invocation,size_t& live,size_t& providers) {
    for(const auto& grant:invocation.appGrants_) {
      if(grant.live)++live;
      if(graph_.holdsGrant(grant.provider))++providers;
    }
  };
  count(mainInvocation_,out.hostLive,out.hostProviders);
  if(foreground_)count(*foreground_,out.foregroundLive,out.foregroundProviders);
  return out;
}
#if RISC_NATIVE_SETTINGS_LIFECYCLE
void Runtime::serviceNativeSettings(bool discoveryOnly) {
  if(nativeCheckpointRunning_ || (discoveryOnly && !invocation_->entryRunning_))return;
  if((discoveryOnly?bool(port_.nativeDiscoveryCheckpoint):bool(port_.nativeOwnerCheckpoint)) && currentRuntime==this && port_.owner() &&
     !retained_ && !promotionRunning_ && !invocation_->streams_.busy() &&
     !graph_.lifecycleBusy() && appDataExitSafe()) {
    bool admitNew=true;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
    const auto draft=[](const Invocation&i){for(const auto&g:i.appGrants_)if(g.live&&g.atomicEndpoint)return true;return false;};
    admitNew=!draft(mainInvocation_)&&(!foreground_||!draft(*foreground_));
#endif
    nativeCheckpointRunning_=true;
    const bool safe=discoveryOnly?port_.nativeDiscoveryCheckpoint():port_.nativeOwnerCheckpoint(admitNew);
    nativeCheckpointRunning_=false;
    if(!safe){retained_=true;fail("native settings custody retained; restart required");(void)appExitBarrier();return;}
  }
}
#endif
void Runtime::serviceProviders() {
#if RISC_NATIVE_SETTINGS_LIFECYCLE || RISC_NATIVE_SERIAL_FILES
  if(nativeCheckpointRunning_)return;
#endif
#if RISC_NATIVE_SETTINGS_LIFECYCLE
  serviceNativeSettings();
#endif
  if(port_.providerServicesReady && !port_.providerServicesReady())return;
  if(currentRuntime!=this || !port_.owner() || retained_ || promotionRunning_ ||
     invocation_->streams_.busy() || graph_.lifecycleBusy() || !appDataExitSafe() ||
     !providerStorageSafe())return;
  if(port_.providerServiceBegin && (!port_.providerServiceEnd || !port_.providerServiceBegin()))return;
  struct ServiceLease {void (*end)();~ServiceLease(){if(end)end();}} lease{port_.providerServiceBegin?port_.providerServiceEnd:nullptr};
  graph_.service(RISC_DRIVER_SERVICE_MAX_MS);
}
void Runtime::yield(uint32_t ms) {
#if RISC_NATIVE_SETTINGS_LIFECYCLE || RISC_NATIVE_SERIAL_FILES
  // No app/provider polling while a native checkpoint owns its resources.
  if(nativeCheckpointRunning_){if(port_.owner()&&port_.retainedDelay)port_.retainedDelay(ms<1?1:ms>50?50:ms);return;}
#endif
  if(currentRuntime!=this || !port_.owner() || yielding_ || promotionRunning_ ||
     invocation_->streams_.busy() || graph_.lifecycleBusy() || (!invocation_->active_ && !retained_))return;
  yielding_=true;
  struct Guard { bool& flag; ~Guard(){flag=false;} } guard{yielding_};
  if(!retained_) {
#if RISC_NATIVE_SETTINGS_LIFECYCLE
    serviceNativeSettings(true); // accepted work/discovery only; never starts maintenance
    if(retained_){if(port_.retainedDelay)port_.retainedDelay(ms<1?1:ms>50?50:ms);return;}
#endif
    // Poll work is bounded separately; each admitted yield cooperates once.
    if(appDataExitSafe())graph_.poll([](){uint32_t now=0;return currentRuntime->monotonicMs(&now)?now:0;},nullptr);
    // Yield is a display/input polling path. Synchronous provider services may
    // touch storage for up to a second, so run them only at explicit lifecycle
    // boundaries, never between a panel refresh command and its BUSY sample.
    if(!graph_.activationSafe() || !appDataExitSafe())(void)appExitBarrier();
  }
  const uint32_t requested=ms<1?1:ms>50?50:ms;
  // Legacy app helpers may remain on their stack after terminal retention.
  // Keep all authority revoked and custody pinned; only the scheduler may run.
  if(retained_) {
    // Invalid-interface rollback can latch retention before revoking the app.
    // Preserve the old yield-time fence before permitting any raw delay.
    if(invocation_->active_)(void)appExitBarrier();
    if(port_.retainedDelay)port_.retainedDelay(requested);
    return;
  }
  RiscPerf::AggregateScope wait(26,requested);
  port_.delay(requested);
}
bool Runtime::diagnostic(const char* line) { return active() && line && strnlen(line,256)<256 && !strchr(line,'\n') && !strchr(line,'\r') && port_.log(line); }
bool Runtime::retainInvocation() {
  if(currentRuntime!=this || promotionRunning_ || invocation_->streams_.busy() || graph_.lifecycleBusy() || !port_.owner() || (!invocation_->active_ && !retained_))return false;
  retained_=true;
  (void)appExitBarrier();
  return true;
}
bool Runtime::appExitBarrier(bool exiting) {
  // Acquisition continues an existing invocation. Copied provider-owned work
  // may remain in flight; only real exit must require complete native idleness.
  // Custody faults still revoke authority and pin every image immediately.
  const bool graphSafe=graph_.activationSafe();
  const bool otherSafe=!foreground_ || (!foreground_->streams_.retained() && (!foreground_->installedFiles_ || !foreground_->installedFiles_->retained()));
  const bool hostSafe=!mainInvocation_.streams_.retained() && (!mainInvocation_.installedFiles_ || !mainInvocation_.installedFiles_->retained());
  if(
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
     atomicExitSafe(exiting) &&
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
     preferenceExitSafe(exiting) &&
#endif
#if RISC_RUNTIME_STORAGE_FILES
     storageFilesExitSafe(exiting) &&
#endif
     (!settings_||!settings_->retained()) && graphSafe && otherSafe && hostSafe && fileSourceExitSafe() && appDataExportExitSafe() && (!exiting || !appDataBusy()) && !retained_ && !invocation_->streams_.retained() && !metadataCloseRetained() && appDataExitSafe() && (!invocation_->installedFiles_ || !invocation_->installedFiles_->retained()) && (exiting ? (!port_.appExitSafe || port_.appExitSafe()) : providerStorageSafe()))return true;
  char providerReason[192]{};
  if(!graphSafe)std::snprintf(providerReason,sizeof(providerReason),
    "provider retention barrier; %.160s",graph_.lastError());
  // Idempotent signaling must not replace the first retained diagnostic (for
  // example promotion's failure) with a generic later app-stop message.
  const char* reason=retained_ && error_[0] ? error_ :
    retained_ ? "app invocation retained; restart required" :
    !graphSafe ? providerReason :
    "native retention barrier; app and providers retained; restart required";
  // Failed starts can retain a grantless graph; failed releases can retain a
  // pending grant. Neither may reach fini or an implicit cleanup retry here.
  // Revoke app authority without calling provider release/quiesce: the boot
  // references and active invocation memory/images must remain pinned.
  auto fence=[](Invocation& context) {
    context.streams_.revokeAuthority();context.active_=false;context.queued_[0]=0;
    context.residentPolicyPending_=false;
    context.appPolicy_=nullptr;context.fileOpen_={};
    for(auto& grant:context.appGrants_)grant.live=false;
  };
  fence(mainInvocation_);if(foreground_)fence(*foreground_);
  residentContinuation_[0]=0;residentTransferFileOpen_={};
  retained_=true;
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
  native_app_memory_retain_all();
#endif
  revokeProviders();
  fail(reason);
  if(port_.failureEvidence && port_.failureEvidence->retained)
    port_.failureEvidence->retained(RISC_RESIDENT_RETAINED,error_);
  if(residentEnabled_)recordResidentFailure(RISC_RESIDENT_FAILURE_RETAINED,RISC_RESIDENT_RETAINED,
    invocation_->current_,invocation_->token);
  return false;
}
bool Runtime::runOne(const char* name) {
  if(!appExitBarrier())return false;
  if(port_.controlBegin)port_.controlBegin(strcmp(name,default_)!=0);
  struct ControlLifetime {
    Port& port;bool& retained;bool ended=false;
    bool finish(){ended=true;return port.controlEnd?port.controlEnd():false;}
    ~ControlLifetime(){if(!retained&&!ended&&port.controlEnd)(void)port.controlEnd();}
  } control{port_,retained_};
  initialDefaultInvocation_=false;
  invocation_->role=0;
  invocation_->residentPolicyPending_=false;
  invocation_->failureKind=RISC_RESIDENT_FAILURE_NONE;invocation_->status=RISC_RESIDENT_OK;
  if(!invocation_->streams_.beginInvocation())return fail("stream invocation unavailable");
  invocation_->token=invocation_->streams_.context();
  failureBreadcrumb(RISC_FAILURE_PHASE_LOAD,name);
  RiscPerf::invocation(name);
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
  if(!native_app_memory_begin_for(invocation_->streams_.context())) {
    invocation_->streams_.endInvocation();
    invocation_->failureKind=RISC_RESIDENT_FAILURE_ALLOCATION;invocation_->status=RISC_RESIDENT_FAILED;
    return fail("app allocation context unavailable");
  }
  native_app_memory_relocation(true);
#endif
  // An app path may share a basename with a live driver; map a fresh image.
  const auto loadStart=RiscPerf::now();
#if RISC_STAGE_LOGS
  const auto loadUs=RiscDiagnostics::monotonicUs();
#endif
  RISC_STAGE_LOG("app load begin file=%s",name);
  RiscPerf::emit(12);
  const AppPolicy* policy=nullptr;
  for(size_t p=0;p<policyCount_;++p)if(!strcmp(policies_[p].elf,name))policy=&policies_[p];
  if(policy && !policy->enabled)fail("installed app unavailable");
#if defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST)
  // Only exact prepared installed-image paths enter this session's cache.
  // Loose child paths retain the ordinary reader and admission behavior.
  void* module=policy && !policy->enabled ? nullptr : policy && policy->sourceDriver>=0 ? loadRemovableApp(*policy) : (policy || !strcmp(name,default_)) ?
    openInternalImage(name,true) : openInternalImage(name);
#else
  void* module=policy && !policy->enabled ? nullptr : policy && policy->sourceDriver>=0 ? loadRemovableApp(*policy) : openInternalImage(name);
#endif
  RISC_STAGE_LOG("app load end file=%s result=%s elapsed_us=%llu",name,module?"ok":"failed",
                 (unsigned long long)(RiscDiagnostics::monotonicUs()-loadUs));
  RiscPerf::finish(12,13,loadStart,module?1:0);
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
  native_app_memory_relocation(false);
#endif
  if(!module) {
    if(retained_ || !providerStorageSafe()){(void)appExitBarrier();return false;}
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
    if(!native_app_memory_end()){retained_=true;fail("failed app load allocation cleanup retained");(void)appExitBarrier();return false;}
#endif
    RiscPerf::emit(21);
    invocation_->streams_.endInvocation();
    invocation_->failureKind=RISC_RESIDENT_FAILURE_LOAD;invocation_->status=RISC_RESIDENT_FAILED;
    return policy && policy->sourceDriver>=0 && error_[0] ? false : fail("app ELF load failed");
  }
  auto entry=reinterpret_cast<void(*)()>(dlsym(module,"app_main"));
  auto init=reinterpret_cast<int(*)()>(dlsym(module,"app_module_init"));
  auto fini=reinterpret_cast<void(*)()>(dlsym(module,"app_module_fini"));
  const char* admissionFailure=!entry?"missing-app-main":
    bool(init)!=bool(fini)?"unpaired-init-fini":nullptr;
  bool ok=!admissionFailure, initialized=false;
  invocation_->module=module;
  invocation_->failureKind=RISC_RESIDENT_FAILURE_NONE;invocation_->status=RISC_RESIDENT_OK;
  if(residentEnabled_) {
    const bool legacy=invocation_==&mainInvocation_ && residentLegacyAllowed(name);
    const uint32_t role=invocation_==&mainInvocation_?RISC_RESIDENT_ROLE_HOST:RISC_RESIDENT_ROLE_FOREGROUND;
    const auto* descriptor=legacy?nullptr:static_cast<const risc_resident_app_descriptor_v1_t*>(dlsym(module,RISC_RESIDENT_DESCRIPTOR_SYMBOL));
    const char* residentFailure=!policy?"missing-resident-policy":nullptr;
    if(!residentFailure && !legacy) {
      if(!descriptor)residentFailure="missing-resident-descriptor";
      else if(descriptor->api_version!=1)residentFailure="resident-descriptor-version";
      else if(descriptor->struct_size!=sizeof(*descriptor))residentFailure="resident-descriptor-size";
      else if(descriptor->role!=role)residentFailure="resident-descriptor-role";
      else if(descriptor->reserved)residentFailure="resident-descriptor-reserved";
      else if(role==RISC_RESIDENT_ROLE_HOST?strcmp(name,default_)!=0:!residentAllowed(name))
        residentFailure="resident-descriptor-path";
    }
    if(residentFailure) {
      if(!admissionFailure)admissionFailure=residentFailure;
      ok=false;invocation_->failureKind=RISC_RESIDENT_FAILURE_ABI;invocation_->status=RISC_RESIDENT_INCOMPATIBLE;
    }
    invocation_->role=legacy?0:role;
  }
  invocation_->appPolicy_=policy;
  invocation_->active_=true;
  if(ok && init) {
    failureBreadcrumb(RISC_FAILURE_PHASE_INIT,name);
#if RISC_STAGE_LOGS
    const auto initUs=RiscDiagnostics::monotonicUs();
#endif
    RISC_STAGE_LOG("app init begin file=%s",name);
    const auto start=RiscPerf::now();RiscPerf::emit(14);
    const int result=init();initialized=result==0;ok=initialized;
    if(port_.failureEvidence && !appExitBarrier())return false;
    if(!ok){invocation_->failureKind=RISC_RESIDENT_FAILURE_INIT;invocation_->status=RISC_RESIDENT_FAILED;}
    RISC_STAGE_LOG("app init end file=%s result=%d elapsed_us=%llu",name,result,
                   (unsigned long long)(RiscDiagnostics::monotonicUs()-initUs));
    RiscPerf::finish(14,15,start,ok?1:0);
  } else {
    RISC_STAGE_LOG("app init skipped file=%s reason=%s",name,ok?"no-init-hook":admissionFailure);
  }
  if(!appExitBarrier())return false;
  invocation_->defaultRunning_=!strcmp(name,default_);invocation_->entryRunning_=ok;
  if(ok) {
    RISC_STAGE_LOG("app entry begin file=%s",name);
    serviceProviders();
    if(!appExitBarrier())return false;
    port_.log("RTE_APP phase=entry");
    RiscPerf::emit(16);
    failureBreadcrumb(RISC_FAILURE_PHASE_MAIN,name);
    initialDefaultInvocation_=invocation_==&mainInvocation_ && invocation_->defaultRunning_ && !firstDefaultStarted_;
    if(initialDefaultInvocation_)firstDefaultStarted_=true;
    entry();
#if RISC_NATIVE_SERIAL_FILES
    // End the native serial session before any application or provider image
    // can be revoked. Failed close pins all custody through real restart.
    if(!retained_ && port_.serialFilesShutdown) {
      nativeCheckpointRunning_=true;const bool serialSafe=port_.serialFilesShutdown();nativeCheckpointRunning_=false;
      if(!serialSafe) {
        retained_=true;fail("serial file shutdown retained; restart required");
        (void)appExitBarrier(false);return false;
      }
    }
#endif
    if(port_.failureEvidence && !appExitBarrier())return false;
    RISC_STAGE_LOG("app entry returned file=%s",name);
    RiscPerf::emit(17);
    port_.log("RTE_APP phase=returned");
  } else port_.log("RTE_APP phase=init-or-entry-rejected");
  invocation_->entryRunning_=false;invocation_->defaultRunning_=false;
  invocation_->residentPolicyPending_=false;
  // Native RETAINED must be observed before app callbacks or freeing anything.
  // Boot-owned driver grants defer graph quiescence until after app teardown,
  // so waiting for final graph shutdown is too late.
  if(!appExitBarrier())return false;
  RiscPerf::Scope unloadTrace(18,19);
#if RISC_STAGE_LOGS
  const auto unloadUs=RiscDiagnostics::monotonicUs();
#endif
  RISC_STAGE_LOG("app unload begin file=%s",name);
  if(initialized) {failureBreadcrumb(RISC_FAILURE_PHASE_FINI,name);fini();}
  if(!appExitBarrier())return false;
  invocation_->active_=false;
  failureBreadcrumb(RISC_FAILURE_PHASE_CLEANUP,name);
  if (retained_ || !revokeApp()) { retained_=true;fail("app grants retained; image retained");(void)appExitBarrier();return false; }
#if RISC_NATIVE_SETTINGS_LIFECYCLE
  // fini is complete and every invocation grant is revoked, but the module and
  // native allocation custody still exist if the final maintenance retains.
  serviceNativeSettings();
  if(retained_ || !appExitBarrier())return false;
#endif
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
  if(!native_app_memory_end()) { retained_=true;fail("app memory busy; image retained");(void)appExitBarrier();return false; }
#endif
  if(dlclose(module)) { RISC_STAGE_LOG("app unload failed file=%s reason=dlclose",name);retained_=true;fail("app unload failed; restart required");(void)appExitBarrier();return false; }
  RISC_STAGE_LOG("app unload end file=%s result=ok elapsed_us=%llu",name,
                 (unsigned long long)(RiscDiagnostics::monotonicUs()-unloadUs));
  invocation_->module=nullptr;
  if(invocation_==&mainInvocation_){shellCallbacks_={};shellToken_=0;}
  invocation_->streams_.endInvocation();
  failureBreadcrumb(RISC_FAILURE_PHASE_HANDOFF,name);
  const bool nativeExit=port_.controlEnd?control.finish():
    (port_.controlExitRequested && port_.controlExitRequested());
  if(nativeExit) {
    // End snapshots/acknowledges atomically, so a press between the last app
    // callback and cleanup completion cannot be lost to a queued launch.
    // A child cannot replace native exit with a different app handoff. Every
    // fini/grant/allocation/image barrier has completed before Home is queued.
    strcpy(invocation_->queued_,default_);
    if(!fileSourceCleanup())return false;
    invocation_->fileOpen_={};residentContinuation_[0]=0;residentTransferFileOpen_={};
    port_.log("RTE_CONTROL crown=exit cleanup=complete action=home");
  }
  unloadTrace.result(1);
  return ok || fail("app entry/lifecycle invalid");
}
bool Runtime::reclaimAppImages() {
#if RISC_APP_IMAGE_CACHE && (defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST))
  if(!port_.owner() || reclaimingAppImages_ || !appImages_)return false;
  detachImagePressure(); // Detach callback before freeing its inputs.
  reclaimingAppImages_=true;
  const bool reclaimed=esp_dl_image_cache_reclaim(&appImages_);
  reclaimingAppImages_=false;
  return reclaimed;
#else
  return false;
#endif
}
bool Runtime::initialDefault() {
  return active() && port_.owner() && !retained_ && invocation_==&mainInvocation_ &&
    invocation_->defaultRunning_ && invocation_->entryRunning_ && !invocation_->queued_[0] &&
    !promotionRunning_ && graph_.activationSafe() && initialDefaultInvocation_;
}
bool Runtime::takeLaunchFailure(risc_runtime_launch_failure_v1* out) {
  if(!out || out->struct_size<sizeof(*out) || !active() || !port_.owner() ||
     retained_ || invocation_!=&mainInvocation_ || !invocation_->defaultRunning_ ||
     !invocation_->entryRunning_ || invocation_->queued_[0] || promotionRunning_ ||
     !graph_.activationSafe() || !launchFailure_.struct_size)return false;
  *out=launchFailure_;launchFailure_={};return true;
}
bool Runtime::run() {
  if(!prepared_ || currentRuntime || !port_.owner() || retained_ || metadataCloseRetained()) return fail("runtime not launchable");
  prepared_=false; currentRuntime=this;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE || RISC_RUNTIME_PREFERENCE_STORE || RISC_RUNTIME_STORAGE_FILES
  atomicKeyValueOwner.store(port_.owner,std::memory_order_release);
  struct AtomicOwnerLifetime {
    ~AtomicOwnerLifetime(){atomicKeyValueOwner.store(nullptr,std::memory_order_release);}
  } atomicOwnerLifetime;
#endif
  if(residentEnabled_ && port_.priorFailure) {
    risc_resident_failure_v1 prior{};prior.struct_size=sizeof(prior);
    if(port_.priorFailure(&prior) && prior.struct_size==sizeof(prior) && prior.kind==RISC_RESIDENT_FAILURE_PRIOR_RESET) {
      prior.application[sizeof(prior.application)-1]=0;prior.detail[sizeof(prior.detail)-1]=0;lastFailure_=prior;
    }
  }
  bool coldBoot=false;
  for(size_t i=0;i<driverCount_;++i)if(drivers_[i].coldBootStart) {
    if(!port_.coldBoot){currentRuntime=nullptr;return fail("cold provider boot classification unavailable");}
    coldBoot=port_.coldBoot();
    if(!port_.owner()){currentRuntime=nullptr;return fail("cold provider boot owner unavailable");}
    break;
  }
#ifdef ESP_PLATFORM
  static const esp_elfsym symbols[]={{"risc_runtime_get_api",reinterpret_cast<const void*>(&risc_runtime_get_api)},ESP_ELFSYM_END};
  if(esp_elf_register_symbol(symbols)) { revokeProviders(); currentRuntime=nullptr; return fail("runtime API registration failed"); }
#endif
  bool ok=true;
  RISC_STAGE_LOG("providers activation mode=%s selected=%u",demandRetention_?"demand-retained":demandActivation_?"demand":"eager",unsigned(driverCount_));
#if RISC_STAGE_LOGS
  if(demandActivation_)for(size_t i=0;i<driverCount_;++i){
    if(coldBoot && drivers_[i].coldBootStart)continue;
    RISC_STAGE_LOG("provider deferred id=%s reason=%s",drivers_[i].id,
                   drivers_[i].coldBootStart?"non-cold-boot":"demand-until-acquired");
  }
#endif
  for(size_t i=0;i<driverCount_;++i) {
    const bool coldStart=demandActivation_ && coldBoot && drivers_[i].coldBootStart;
    if(demandActivation_ && !coldStart)continue;
    char stage[144];std::snprintf(stage,sizeof(stage),"RTE_PROVIDER id=%s phase=start",drivers_[i].id);port_.log(stage);
    serviceProviders();
    grants_[i]=graph_.acquireFrom(drivers_[i].id,drivers_[i].provides,drivers_[i].api,drivers_[i].instance);
    const bool coldSafe=!coldStart || promotionSafe();
    if(!grants_[i].slot || !coldSafe) {
      ok=fail(coldSafe?graph_.lastError():"cold provider activation retained; restart required");port_.log(error_);
      // A grantless failed start can retain code and dependency custody too.
      // Do not run the final boot cleanup/retry path after that barrier.
      if(!coldSafe || !graph_.activationSafe()) {
        retained_=true;revokeProviders();
        port_.log("RTE_CLEANUP provider-activation=retained");
      }
      break;
    }
    std::snprintf(stage,sizeof(stage),"RTE_PROVIDER id=%s phase=ready",drivers_[i].id);port_.log(stage);
    serviceProviders();
    if(!graph_.activationSafe()){ok=fail("provider service retained; restart required");retained_=true;revokeProviders();break;}
    port_.delay(1);
  }
  // The active store is immutable until restart. Paired updates stage another
  // bank; a new Runtime session owns a new cache even at the same mount path.
#if RISC_APP_IMAGE_CACHE && (defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST))
  if(ok){
    appImages_=esp_dl_image_cache_create();
    if(appImages_){
      imagePressureRuntime.store(this,std::memory_order_release);
      imagePressureOwner.store(port_.owner,std::memory_order_release);
    }
  }
#endif
  // One app at a time; no recursive ELF launch, directory search or fallback.
  while(ok) {
    invocation_->queued_[0]=0;
    const bool isDefault=!strcmp(invocation_->current_,default_);
    const bool completed=runOne(invocation_->current_);
    if(retained_) { ok=false; break; }
    if(residentEnabled_ && !completed && !isDefault)
      recordResidentFailure(invocation_->failureKind?invocation_->failureKind:uint32_t(RISC_RESIDENT_FAILURE_LOAD),
                            invocation_->status==RISC_RESIDENT_INCOMPATIBLE?RISC_RESIDENT_INCOMPATIBLE:RISC_RESIDENT_FAILED,
                            invocation_->current_,invocation_->token);
    if(residentEnabled_ && isDefault && completed && invocation_->queued_[0] && residentLegacyAllowed(invocation_->queued_)) {
      // runForeground staged only copies. Host fini, grant/memory cleanup and
      // dlclose have now all succeeded; no host or child stack is live.
      invocation_->fileOpen_=residentTransferFileOpen_;residentTransferFileOpen_={};
      strcpy(invocation_->current_,invocation_->queued_);
      error_[0]=0;port_.delay(1);continue;
    }
    auto resumeHost=[&]() {
      if(residentEnabled_ && residentAllowed(invocation_->current_)) {
        strcpy(residentContinuation_,invocation_->current_);
        residentTransferFileOpen_=invocation_->fileOpen_;invocation_->fileOpen_={};
        strcpy(invocation_->current_,default_);
      }
    };
    // A file receiver always returns to its fresh caller, including load/init
    // failure and when the receiver happens to be the configured default.
    const bool fileCaller=fileOpenAfterRun(completed);
    if(retained_){ok=false;break;}
    if(fileCaller) { resumeHost();error_[0]=0;port_.delay(1);continue; }
    if(!completed && isDefault) { ok=false; break; }
    if(residentEnabled_ && isDefault && residentContinuation_[0]) {
      ok=fail("resident host did not consume pending continuation");break;
    }
    if(!completed) {
      launchFailure_={};launchFailure_.struct_size=sizeof(launchFailure_);
      for(size_t p=0;p<policyCount_;++p)
        if(!strcmp(policies_[p].elf,invocation_->current_) && policies_[p].sourceDriver>=0 &&
           invocation_->failureKind==RISC_RESIDENT_FAILURE_LOAD)
          launchFailure_.flags=RISC_LAUNCH_FAILURE_EXTERNAL_IMAGE;
      const char* name=invocation_->current_;
      const size_t prefix=strlen(root_);
      if(!strncmp(name,root_,prefix) && name[prefix]=='/')name+=prefix+1;
      snprintf(launchFailure_.application,sizeof(launchFailure_.application),"%.127s",name);
      snprintf(launchFailure_.detail,sizeof(launchFailure_.detail),"%.159s",error_);
      port_.log("RTE_APP child=failed action=reload-default");
    }
    if(completed && invocation_->queued_[0]) strcpy(invocation_->current_,invocation_->queued_);
    else if(!isDefault) strcpy(invocation_->current_,default_);
    else break;
    resumeHost();
    // Default reload is a fresh invocation: no ELF pointers/static state survive.
    error_[0]=0; port_.delay(1);
  }
#if RISC_NATIVE_SETTINGS_LIFECYCLE
  if(!retained_ && port_.nativeSettingsShutdown){
    nativeCheckpointRunning_=true;const bool safe=port_.nativeSettingsShutdown();nativeCheckpointRunning_=false;
    if(!safe){retained_=true;ok=fail("native settings shutdown retained; restart required");}
  }
#endif
#if RISC_NATIVE_SERIAL_FILES
  if(!retained_ && port_.serialFilesShutdown) {
    nativeCheckpointRunning_=true;const bool serialSafe=port_.serialFilesShutdown();nativeCheckpointRunning_=false;
    if(!serialSafe){retained_=true;ok=fail("serial file shutdown retained; restart required");}
  }
#endif
  if(!retained_ && !fileSourceCleanup())ok=false;
  if(!retained_) {
    for(size_t i=driverCount_;i--;) if(grants_[i].slot && !graph_.release(grants_[i])) {
      retained_=true;
      char detail[256];std::snprintf(detail,sizeof(detail),"RTE_CLEANUP id=%.95s driver-quiescence=retained detail=%.108s",drivers_[i].id,graph_.lastError());port_.log(detail);
      if(ok)ok=fail("driver quiescence failed; restart required");
      break;
    }
    if(!retained_ && !graph_.shutdown()) {
      retained_=true;port_.log("RTE_CLEANUP driver-shutdown=retained");
      if(ok)ok=fail("driver shutdown retained; restart required");
    }
  }
  // Cached input bytes are never borrowed by a mapping. Releasing them also
  // preserves an app/provider image retained after failed quiescence.
#if defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST)
  detachImagePressure();esp_dl_image_cache_reclaim(&appImages_);
#endif
  if(retained_ && port_.failureEvidence && port_.failureEvidence->retained)
    port_.failureEvidence->retained(RISC_RESIDENT_RETAINED,error_);
  if(!retained_)failureBreadcrumb(RISC_FAILURE_PHASE_IDLE);
  invocation_->fileOpen_={};revokeProviders(); currentRuntime=nullptr; return ok;
}
}

// The installation coordinator supplies owner-resolved policy separately from
// untrusted package declarations. Discovering a file never confers authority.
bool RiscBoot::Runtime::reserveInstalledApps(size_t capacity) {
  if(currentRuntime==this || !default_[0] || retained_ || !port_.owner() ||
     capacity<policyCount_ || capacity>MaxAppPolicies || installedMetadata_)return false;
  auto policies=metadataArray<AppPolicy>(capacity);
  auto metadata=metadataArray<InstalledAppMetadata>(capacity);
  auto handlers=metadataArray<FileOpenMetadata>(capacity);
  if(!policies || !metadata || !handlers)return false;
  for(size_t i=0;i<policyCount_;++i){policies[i]=policies_[i];if(fileHandlers_)handlers[i]=fileHandlers_[i];}
  policies_=std::move(policies);installedMetadata_=std::move(metadata);
  fileHandlers_=std::move(handlers);policyCapacity_=capacity;return true;
}
bool RiscBoot::Runtime::admitInstalledApp(const char* policyJson,const char* manifestJson) {
 return admitInstalledAppAt(policyJson,manifestJson,policyCount_);
}
bool RiscBoot::Runtime::admitInstalledAppAt(const char* policyJson,const char* manifestJson,size_t slot) {
  if(!installedMetadata_ || !port_.owner() || retained_ || metadataCloseRetained() ||
     !providerStorageSafe() || !graph_.activationSafe() || promotionRunning_ ||
     graph_.lifecycleBusy() || slot>=policyCapacity_ || slot>policyCount_ || !policyJson || !manifestJson ||
     strnlen(policyJson,4097)>4096 || strnlen(manifestJson,4097)>4096)return false;
  // A foreground/child may not mutate the host's registry, and no in-flight
  // continuation can be silently redirected by admitting a duplicate identity.
  if(!(installation_ && installation_[0].call) && currentRuntime && (currentRuntime!=this || !active() || invocation_!=&mainInvocation_ ||
     strcmp(invocation_->current_,default_) || foregroundRunning_ ||
     (foreground_ && foreground_->active_) || invocation_->streams_.busy()))return false;
  JsonDocument policy,manifest,rows;
  if(!parse(policyJson,strlen(policyJson),policy) || !parse(manifestJson,strlen(manifestJson),manifest) ||
     !policy.is<JsonObjectConst>() || !manifest.is<JsonObjectConst>() ||
     policy["image_source"].isNull() || !policy["app_data_export"].isNull())return false;
  rows.to<JsonArray>().add(policy.as<JsonVariantConst>());
  const size_t before=policyCount_;
  const AppPolicy oldPolicy=policies_[slot];const FileOpenMetadata oldHandlers=fileHandlers_[slot];
  policies_[slot]=AppPolicy{};fileHandlers_[slot]=FileOpenMetadata{};
  if(!appPolicies(rows.as<JsonVariantConst>(),manifest.as<JsonVariantConst>(),slot)) {
    policies_[slot]=oldPolicy;fileHandlers_[slot]=oldHandlers;
    policyCount_=before;return false;
  }
  strcpy(installedMetadata_[slot].manifest,manifestJson);
  return true;
}

#include "InstallationRuntime.inc"

#ifdef RISC_INSTALLED_APP_CAPACITY
extern "C" __attribute__((used)) const uint32_t risc_installed_app_capacity=RiscLimits::Apps;
#endif
