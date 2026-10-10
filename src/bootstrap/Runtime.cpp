#include "Runtime.h"
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
#include "runtime/resources/ScopedBufferWipe.h"
#include <RiscDiagnosticSourceV1.h>
#include <esp_dlfcn.h>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <atomic>
#include "../../lib/hal/RuntimeImageCacheConfig.h"
#if defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST)
#include <private/esp_dlcache.h>
#endif
#ifdef ESP_PLATFORM
#include <esp_elf.h>
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
    [](risc_failure_evidence_client_v1* out){return currentRuntime && currentRuntime->failureEvidenceClient(out);}};
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
Runtime::~Runtime() {
  // Native owners retain the entire metadata candidate after lost I/O custody.
  // As with GraphV2, destruction is a caller contract violation, never cleanup.
  if(retainedNativeProviderRead_ || retainedNativeProviderBytes_)std::abort();
  revokeProviders();
  RuntimeProviders::NativeProviderPolicySnapshotV1::destroy(nativePolicies_);
}
namespace {
bool elfPath(const char* p) { size_t n=strlen(p); return n>4 && !strcmp(p+n-4,".elf"); }
}
#include "FileOpenRuntime.inc"
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
        !integer(c["config_version"],1,(!strcmp(type,"radio.lora") || !strcmp(type,"touch.i2c"))?2:1,api) || !c["revisions"].is<JsonArrayConst>()) return fail("invalid hardware compatibility");
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
  if (!strcmp(capability,RISC_DIAGNOSTIC_SOURCE_CAPABILITY) &&
      (api!=RISC_DIAGNOSTIC_SOURCE_API_V1 || scope!=Scope::Global || id)) return false;
  if (scope==Scope::Global) {
    if (id || (strcmp(capability,"platform.clock") && strcmp(capability,"platform.board") &&
               strcmp(capability,"platform.http-client") && strcmp(capability,"platform.bank-store") &&
               strcmp(capability,"platform.radio.iq.resource") && strcmp(capability,RISC_PLATFORM_REALTIME_CAPABILITY) &&
               strcmp(capability,RISC_DIAGNOSTIC_SOURCE_CAPABILITY) &&
               strcmp(capability,RISC_USB_PHY_RESOURCE_CAPABILITY))) return false;
  } else if ((scope!=Scope::Device && scope!=Scope::Bus) || !id) return false;
  const auto* header=static_cast<const uint32_t*>(table);
  if (header[0]!=api || header[1]<8) return false;
  if (!strcmp(capability,RISC_DIAGNOSTIC_SOURCE_CAPABILITY) &&
      (header[1]<sizeof(risc_diagnostic_source_api_v1) ||
       !static_cast<const risc_diagnostic_source_api_v1*>(table)->read)) return false;
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
            !port_.keyValue || !port_.keyValue->get || !port_.keyValue->put ||
            port_.keyValue->maxBlobSize<(req.api==RISC_BOUND_KEY_VALUE_API_V2?RISC_BOUND_KEY_VALUE_V2_BLOB_MAX:RISC_BOUND_KEY_VALUE_BLOB_MAX))
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
  // Bounded transitive closure catches every cycle before ANY ELF entry point.
  for(size_t k=0;k<driverCount_;++k) for(size_t i=0;i<driverCount_;++i) for(size_t j=0;j<driverCount_;++j) edges[i][j]=edges[i][j] || (edges[i][k] && edges[k][j]);
  for(size_t i=0;i<driverCount_;++i) if(edges[i][i]) return fail("driver dependency cycle");
  return true;
}
bool Runtime::appPolicies(JsonVariantConst value) {
  if (value.isNull()) return true;
  if (!value.is<JsonArrayConst>() || value.size()>MaxAppPolicies) return fail("invalid app capability policy");
  if(value.size()){policies_=metadataArray<AppPolicy>(value.size());if(!policies_)return fail("app policy allocation failed");}
  for (JsonObjectConst item:value.as<JsonArrayConst>()) {
    auto& policy=policies_[policyCount_]; char relative[193]; JsonDocument doc;
    if (!keys(item,{"manifest","grants"}) || !text(item["manifest"],relative,sizeof(relative)) ||
        !path(root_,relative,policy.elf,sizeof(policy.elf)) || !readJson(policy.elf,doc,&metadataCloseRetained_)) return fail("invalid app policy manifest path");
    JsonObjectConst manifest=doc.as<JsonObjectConst>(); char filename[128];
    if (!keys(manifest,{"type","id","version","architecture","file_name","entry","requires"},{"description","display_name","icon","supported_file_types"}) ||
        !eq(manifest["type"],"application") || !eq(manifest["architecture"],"xtensa-esp32s3") || !eq(manifest["entry"],"app_main") ||
        !text(manifest["id"],policy.id,sizeof(policy.id)) || !RuntimePackages::safeId(policy.id) ||
        !text(manifest["version"],policy.version,sizeof(policy.version)) || !RuntimePackages::safeVersion(policy.version) ||
        !text(manifest["file_name"],filename,sizeof(filename)) || !RuntimePackages::safeArtifact(filename) || !elfPath(filename) ||
        !manifest["requires"].is<JsonArrayConst>() || manifest["requires"].size()>MaxAppRequirements ||
        !item["grants"].is<JsonArrayConst>() || item["grants"].size()>MaxAppPolicyGrants) return fail("invalid app identity/declarations");
    FileOpenMetadata fileMetadata;
    if(!fileOpenMetadata(manifest,fileMetadata))return fail("invalid app file associations");
    if(fileMetadata.count) {
      if(!fileHandlers_)fileHandlers_=metadataArray<FileOpenMetadata>(value.size());
      if(!fileHandlers_)return fail("file association allocation failed");
      fileHandlers_[policyCount_]=fileMetadata;
    }
    char* slash=strrchr(policy.elf,'/'); if (!slash) return false;
    *(slash+1)=0;
    if (strlen(policy.elf)+strlen(filename)>=sizeof(policy.elf)) return fail("app path too long");
    strcat(policy.elf,filename);
    for (size_t i=0;i<policyCount_;++i) if (!strcmp(policies_[i].id,policy.id) || !strcmp(policies_[i].elf,policy.elf)) return fail("duplicate app identity/path policy");
    for (JsonObjectConst request:manifest["requires"].as<JsonArrayConst>()) {
      char requested[96];int64_t api=0;
      if (!keys(request,{"capability","api"}) || !text(request["capability"],requested,sizeof(requested)) ||
          !integer(request["api"],1,UINT32_MAX,api)) return fail("invalid app requirement");
      if (!strcmp(requested,RISC_BOUND_KEY_VALUE_CAPABILITY)) return fail("provider key-value denied to app");
      // A manifest declares each capability once. The owner may independently
      // authorize more than one positive KV namespace, still within MaxAppPolicyGrants
      // total grants; no other capability's uniqueness rule is broadened.
      const bool keyValue=!strcmp(requested,RISC_KEY_VALUE_CAPABILITY);
      const bool sharedData=!strcmp(requested,RISC_SHARED_DATA_CAPABILITY);
      for (size_t i=0;i<policy.count;++i) if (!strcmp(policy.grants[i].capability,requested) &&
          (!keyValue || policy.grants[i].api==uint32_t(api))) return fail("duplicate app requirement");
      unsigned matches=0;
      for (JsonObjectConst allowed:item["grants"].as<JsonArrayConst>()) {
        int64_t allowedApi=0,instance=0;char capability[96];
        if (!text(allowed["capability"],capability,sizeof(capability)) ||
            !(strcmp(capability,RISC_SHARED_DATA_CAPABILITY)?keys(allowed,{"capability","api","instance_id"}):keys(allowed,{"capability","api","instance_id","file"})) ||
            !integer(allowed["api"],1,UINT32_MAX,allowedApi) || !integer(allowed["instance_id"],0,INT32_MAX,instance)) return fail("invalid app grant");
        if (!strcmp(capability,RISC_BOUND_KEY_VALUE_CAPABILITY)) return fail("provider key-value denied to app");
        if (!strcmp(capability,RISC_BOUND_APP_DATA_CAPABILITY)) return fail("provider app-data denied to app");
        if (strcmp(capability,requested) || allowedApi!=api) continue;
        if (++matches>1 && !keyValue && !sharedData) return fail("app requirement not uniquely authorized");
        if (policy.count==MaxAppPolicyGrants) return fail("too many app grants");
        for (size_t i=0;i<policy.count;++i) {
          const auto& earlier=policy.grants[i];
          if (!strcmp(earlier.capability,capability) && earlier.api==uint32_t(api) && earlier.instance==uint64_t(instance))
            return fail("duplicate app grant");
        }
        auto& grant=policy.grants[policy.count];
        grant.api=api;grant.instance=instance;
        if (keyValue) {
          if ((grant.api!=RISC_KEY_VALUE_API_V1 && grant.api!=RISC_KEY_VALUE_API_V2) || !grant.instance || !port_.keyValue || !port_.keyValue->get || !port_.keyValue->put ||
              port_.keyValue->maxBlobSize<(grant.api==RISC_KEY_VALUE_API_V2?RISC_KEY_VALUE_V2_BLOB_MAX:RISC_KEY_VALUE_BLOB_MAX)) return fail("app key-value backend/namespace unavailable");
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
            if(!RiscUpdate::readCohort(root_,identity,&metadataCloseRetained_))return fail("retained-wake cohort unavailable");
            char digest[65]{};for(unsigned b=0;b<32;++b)snprintf(digest+2*b,3,"%02x",identity.firmwareSha[b]);
            const int n=snprintf(retainedCohort_,sizeof(retainedCohort_),"%s|%s|%s|%s|%s|%s|%u|%u|%s",
              identity.product.product,identity.product.version,identity.product.source_repo,identity.product.source_revision,
              identity.runtimeVersion,identity.layout,unsigned(identity.storeAbi),unsigned(identity.firmwareSize),digest);
            if(n<=0 || size_t(n)>=sizeof(retainedCohort_))return fail("retained-wake cohort too large");
          }
          grant.driver=RetainedWakeDriver;grant.capability=RISC_RETAINED_WAKE_CAPABILITY;
        } else if (!strcmp(capability,"file.open")) {
          if(grant.api!=T5_FILE_OPEN_API_VERSION || grant.instance)return fail("invalid file-open authority");
          grant.fileOpen=true;grant.capability="file.open";
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
          for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g)if(policies_[p].grants[g].driver==AppDataDriver && policies_[p].grants[g].instance==grant.instance)return fail("app-data namespace already owned");
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
      if (!matches) return fail("app requirement not uniquely authorized");
    }
    if (policy.count!=item["grants"].size()) return fail("undeclared app grant");
    ++policyCount_;
  }
  return true;
}
bool Runtime::acquire(const char* capability,uint32_t api,uint64_t instance,risc_runtime_capability_v1* out) {
  if (promotionRunning_ || graph_.lifecycleBusy() || !active() || !appDataExitSafe() || !invocation_->appPolicy_ || !out || out->struct_size<sizeof(*out) || !capability || !api || grantGeneration_==UINT32_MAX) return false;
  out->slot=out->generation=0;out->api=nullptr;
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
  if (allowed->keyValue) {
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
  } else if (allowed->fileOpen) {
    grant.api=fileOpenApi();
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
    serviceProviders();
    if(!appExitBarrier())return false;
    const auto& driver=drivers_[allowed->driver];
    if(demandRetention_ && !demandActivation_ && !grants_[allowed->driver].slot) {
      // Only the first real acquisition after promotion creates session custody.
      // Keep the normal loader/dependency ordering and serialize start callbacks.
      if(!promotionSafe())return false;
      promotionRunning_=true;
      auto& pin=grants_[allowed->driver];
      pin=graph_.acquireFrom(driver.id,capability,api,driver.instance);
      const bool safe=promotionSafe();
      promotionRunning_=false;
      if(!safe) {
        retained_=true;invocation_->active_=false;fail("provider activation retained; restart required");
        if(residentEnabled_)(void)appExitBarrier();
        return false;
      }
      if(!pin.slot)return fail(graph_.lastError());
    }
    grant.provider=graph_.acquireFrom(driver.id,capability,api,driver.instance);
    if (!grant.provider.slot) {serviceProviders();(void)appExitBarrier();return false;}
    grant.api=graph_.interfaceFor(grant.provider);
  } else grant.api=platforms_[allowed->platform].table;
  const auto* header=static_cast<const uint32_t*>(grant.api);
  if (!header || header[0]!=api || header[1]<8) {
    if (grant.provider.slot && !graph_.release(grant.provider)) {
      retained_=true;fail("invalid app interface retained");if(residentEnabled_)(void)appExitBarrier();return false;
    }
    grant={}; return false;
  }
  grant.live=true;grant.generation=++grantGeneration_;grant.invocation=invocation_->streams_.context();
  serviceProviders();
  if(!appExitBarrier())return false;
  out->slot=slot+1;out->generation=grant.generation;out->api=grant.api;return true;
}
int32_t Runtime::keyValueGet(void* context,const char* key,void* buffer,uint32_t capacity,uint32_t* outSize) {
  if (outSize) *outSize=0;
  Runtime* r=currentRuntime;
  if (!r || !r->active() || !context || !r->port_.keyValue) return RISC_KEY_VALUE_CONTEXT;
  const AppGrant* matched=nullptr;
  for (const auto& grant:r->invocation_->appGrants_) if (grant.live && grant.keyValueNamespace && grant.keyValue.context==context) matched=&grant;
  if (!matched) return RISC_KEY_VALUE_CONTEXT;
  if (!outSize || !keyValueKey(key) || (!buffer && capacity)) return RISC_KEY_VALUE_INVALID;
  const uint32_t limit=matched->keyValue.api_version==RISC_KEY_VALUE_API_V2?RISC_KEY_VALUE_V2_BLOB_MAX:RISC_KEY_VALUE_BLOB_MAX;
  uint8_t temp[RISC_KEY_VALUE_V2_BLOB_MAX]; RiscRuntime::ScopedBufferWipe wipe(temp); uint32_t size=0;
  const auto& backend=*r->port_.keyValue;
  const int32_t result=backend.get(backend.context,matched->keyValueNamespace,key,temp,limit,&size);
  if (result==RISC_KEY_VALUE_NOT_FOUND) return result;
  if (result!=RISC_KEY_VALUE_OK || !size || size>limit) return RISC_KEY_VALUE_IO;
  if (capacity<size) {*outSize=size;return RISC_KEY_VALUE_BUFFER_SMALL;}
  memcpy(buffer,temp,size);*outSize=size;return RISC_KEY_VALUE_OK;
}
int32_t Runtime::keyValuePut(void* context,const char* key,const void* data,uint32_t size) {
  Runtime* r=currentRuntime;
  if (!r || !r->active() || !context || !r->port_.keyValue) return RISC_KEY_VALUE_CONTEXT;
  const AppGrant* matched=nullptr;
  for (const auto& grant:r->invocation_->appGrants_) if (grant.live && grant.keyValueNamespace && grant.keyValue.context==context) matched=&grant;
  if (!matched) return RISC_KEY_VALUE_CONTEXT;
  const uint32_t limit=matched->keyValue.api_version==RISC_KEY_VALUE_API_V2?RISC_KEY_VALUE_V2_BLOB_MAX:RISC_KEY_VALUE_BLOB_MAX;
  if (!keyValueKey(key) || !data || !size || size>limit) return RISC_KEY_VALUE_INVALID;
  const auto& backend=*r->port_.keyValue;
  return backend.put(backend.context,matched->keyValueNamespace,key,data,size)==RISC_KEY_VALUE_OK ? RISC_KEY_VALUE_OK : RISC_KEY_VALUE_IO;
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
  if(metadataCloseRetained_ || !appDataExitSafe())return false;
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
  if(!storage->count && !storage->fileCount && !storage->needsRealtime){storage->live=true;return true;}
  void* token=nextKeyValueContext(keyValueGeneration);
  if (!token) return false;
  storage->table.context=token;
  if(storage->fileCount)storage->fileTable.context=token;
  if(storage->needsRealtime)storage->realtime.context=token;
  storage->live=true;
  return true;
}
void Runtime::revokeProvider(void* context) {
  if (context) static_cast<ProviderStorage*>(context)->live=false;
}
void Runtime::revokeProviders() {
  for (auto& storage:providerStorage_) storage.live=false;
}
Runtime::ProviderStorage* Runtime::providerContext(void* context) {
  Runtime* r=currentRuntime;
  if (!r || !context || !r->port_.owner() || !r->port_.keyValue) return nullptr;
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
  uint8_t temp[RISC_BOUND_KEY_VALUE_V2_BLOB_MAX]; RiscRuntime::ScopedBufferWipe wipe(temp); uint32_t size=0;
  const auto& backend=*storage->owner->port_.keyValue;
  const int32_t result=backend.get(backend.context,matched->nameSpace,key,temp,limit,&size);
  if (result==RISC_BOUND_KEY_VALUE_NOT_FOUND) return result;
  if (result!=RISC_BOUND_KEY_VALUE_OK || !size || size>limit) return RISC_BOUND_KEY_VALUE_IO;
  if (capacity<size) {*outSize=size;return RISC_BOUND_KEY_VALUE_BUFFER_SMALL;}
  memcpy(buffer,temp,size);*outSize=size;return RISC_BOUND_KEY_VALUE_OK;
}
int32_t Runtime::boundKeyValuePut(void* context,const char* key,const void* data,uint32_t size) {
  const ProviderStorage* storage=providerContext(context);
  if (!storage) return RISC_BOUND_KEY_VALUE_CONTEXT;
  const uint32_t limit=storage->table.api_version==RISC_BOUND_KEY_VALUE_API_V2?RISC_BOUND_KEY_VALUE_V2_BLOB_MAX:RISC_BOUND_KEY_VALUE_BLOB_MAX;
  if (!keyValueKey(key) || !data || !size || size>limit) return RISC_BOUND_KEY_VALUE_INVALID;
  const ProviderKey* matched=nullptr;
  for (size_t i=0;i<storage->count;++i) if (!strcmp(storage->keys[i].key,key)) matched=&storage->keys[i];
  if (!matched || !matched->writable) return RISC_BOUND_KEY_VALUE_CONTEXT;
  const auto& backend=*storage->owner->port_.keyValue;
  return backend.put(backend.context,matched->nameSpace,key,data,size)==RISC_BOUND_KEY_VALUE_OK ? RISC_BOUND_KEY_VALUE_OK : RISC_BOUND_KEY_VALUE_IO;
}
bool Runtime::release(risc_runtime_capability_v1* out) {
  if (promotionRunning_ || graph_.lifecycleBusy() || !active() || !appDataExitSafe() || !out || out->struct_size<sizeof(*out) || !out->slot || out->slot>16) return false;
  auto& grant=invocation_->appGrants_[out->slot-1];
  if (!grant.live || grant.generation!=out->generation || grant.api!=out->api || grant.invocation!=invocation_->streams_.context()) return false;
  RuntimeStreams::AppStreamBinding binding{grant.invocation,out->slot,grant.generation,grant.api,grant.provider};
  uint64_t providerContext=0;
  const bool streamProvider=grant.provider.slot && graph_.streamSessionsFor(grant.provider,&providerContext);
  if (!invocation_->streams_.closeGrant(binding)) return false;
  if (grant.provider.slot && !graph_.release(grant.provider)) {
    if(streamProvider)streamRetain(this);
    else if(residentEnabled_)(void)appExitBarrier();
    return false;
  }
  if(grant.api==&invocation_->installedVolume_){if(!invocation_->installedFiles_->end()){if(residentEnabled_)(void)appExitBarrier();return false;}invocation_->installedVolumeContext_=nullptr;}
  if(grant.api==&invocation_->appDataTable_){if(!appDataExitSafe()){if(residentEnabled_)(void)appExitBarrier();return false;}invocation_->appDataContext_=nullptr;invocation_->appDataNamespace_=0;invocation_->appDataSharedFile_=nullptr;}
  if(grant.api==&invocation_->promotionTable_)invocation_->promotionContext_=nullptr;
  if(grant.api==&invocation_->realtimeTable_)invocation_->realtimeContext_=nullptr;
  if(grant.api==&invocation_->realtimeControlTable_)invocation_->realtimeControlContext_=nullptr;
  if(grant.api==&invocation_->retainedWakeTable_){port_.retainedWake->cancel();invocation_->retainedWakeContext_=nullptr;}
  grant={};out->slot=out->generation=0;out->api=nullptr;return true;
}
bool Runtime::revokeApp() {
  if(!invocation_->streams_.closeAll())return false;
  for (auto& grant:invocation_->appGrants_) if (grant.live) {
    if (grant.provider.slot && !graph_.release(grant.provider)) {
      retained_=true;(void)appExitBarrier();return false;
    }
    if(grant.api==&invocation_->appDataTable_) {
      if(!appDataExitSafe()){retained_=true;(void)appExitBarrier();return false;}
      invocation_->appDataContext_=nullptr;invocation_->appDataNamespace_=0;invocation_->appDataSharedFile_=nullptr;
    }
    if(grant.api==&invocation_->installedVolume_) {
      if(!invocation_->installedFiles_->end()){retained_=true;(void)appExitBarrier();return false;}
      invocation_->installedVolumeContext_=nullptr;
    }
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
#include "AppDataRuntime.inc"
#include "BoundAppDataRuntime.inc"
#include "RetainedWakeRuntime.inc"
#include "RealtimeRuntime.inc"
#include "ProviderPromotionRuntime.inc"
#include "ResidentShellRuntime.inc"
bool Runtime::prepare(const char* root) {
  RiscPerf::Scope trace(10,11);
  if(attempted_ || metadataCloseRetained_ || !port_.owner() || !root || strlen(root)>=sizeof(root_) || root[0]!='/') return fail("invalid boot invocation");
  attempted_=true; strcpy(root_,root);
  if (!RuntimeProviders::nativeProviderPolicySetValid(port_.nativeProviders)) return fail("invalid native provider selection");
  if(port_.nativeProviders.count) {
    nativePolicies_=RuntimeProviders::NativeProviderPolicySnapshotV1::capture(port_.nativeProviders);
    if(!nativePolicies_)return fail("native provider selection snapshot failed");
    port_.nativeProviders=nativePolicies_->view();
  }
  char filename[256], relative[193]; JsonDocument config, boardDoc;
  if(!path(root_,"boot.json",filename,sizeof(filename)) || !readJson(filename,config,&metadataCloseRetained_)) return fail("boot.json unreadable/invalid");
  JsonObjectConst c=config.as<JsonObjectConst>();
  if (!c["port"].isNull() && !board_.port(c["port"])) return fail(board_.error());
  if(!RiscUpdate::validCohortMigration(c["cohort_migration"]))return fail("invalid cohort migration policy");
  if(!keys(c,{"board","default_app","drivers"},{"port","app_capabilities","cohort_migration","provider_activation","resident_shell"}) || !text(c["board"],relative,sizeof(relative)) ||
      !path(root_,relative,filename,sizeof(filename)) || !readJson(filename,boardDoc,&metadataCloseRetained_) || !board_.load(boardDoc.as<JsonObjectConst>())) return fail(board_.error()[0]?board_.error():"board manifest unreadable/invalid");
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
    if(!keys(item,{"manifest"},{"instance_id","key_value","app_data","boot_start"}) || !text(item["manifest"],relative,sizeof(relative)) ||
        !path(root_,relative,d.elf,sizeof(d.elf)) || (!item["instance_id"].isNull() && !integer(item["instance_id"],1,INT32_MAX,instance))) return fail("invalid driver selection");
    if(!item["boot_start"].isUnbound()) {
      if(!demandActivation_ || !eq(item["boot_start"],"cold"))return fail("invalid driver boot start policy");
      d.coldBootStart=true;
    }
    d.instance=instance; JsonDocument manifestDoc;
    if(!readJson(d.elf,manifestDoc,&metadataCloseRetained_) || !manifest(manifestDoc.as<JsonObjectConst>(),d)) return fail(error_[0]?error_:"driver manifest unreadable/invalid");
    if(!providerPolicy(item,providerStorage_[driverCount_]) ||
       !providerFilePolicy(item,providerStorage_[driverCount_])) return false;
    ++driverCount_;
  }
  registrationOpen_=true;
  const bool bound=(!port_.bindPlatforms || port_.bindPlatforms(*this)) && registerProviderRealtime();
  registrationOpen_=false;
  if (!bound) return fail("trusted platform binding failed");
  if(!validateGraph() || !appPolicies(c["app_capabilities"]) || !residentPolicy(c["resident_shell"]) || !configureInstalledFiles(c)) return false;
  bool hasProviderFiles=false;
  for(size_t i=0;i<driverCount_;++i)hasProviderFiles=hasProviderFiles || providerStorage_[i].fileCount;
  for(size_t i=0;i<driverCount_;++i) {
    const Driver& d=drivers_[i];
    RuntimeProviders::SpecV2 spec{d.id,d.elf,d.provides,d.api,d.requirements,d.count};
    spec.hardware=d.instance?&board_.device(d.instance)->hardware:nullptr;
    if (providerStorage_[i].count || providerStorage_[i].needsRealtime || hasProviderFiles){
      providerStorage_[i].owner=this;
      spec.lease={&providerStorage_[i],beginProvider,revokeProvider,hasProviderFiles?providerFileSafe:nullptr};
    }
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
    if(selected ? !registerNativeProvider(d,spec,*selected) : !graph_.addVerified(spec))
      return fail(error_[0]?error_:"driver registration failed");
  }
  strcpy(default_,invocation_->current_); prepared_=true; return true;
}
bool Runtime::registerNativeProvider(const Driver& d,RuntimeProviders::SpecV2& spec,
                                     const RuntimeProviders::NativeProviderPolicyV1& policy) {
  // Read one exact candidate into temporary owned storage. Graph admission
  // copies and hashes its own snapshot; no filesystem path reaches relocation.
#ifdef ESP_PLATFORM
  auto* bytes=static_cast<uint8_t*>(heap_caps_malloc(policy.elfLength,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!bytes)bytes=static_cast<uint8_t*>(heap_caps_malloc(policy.elfLength,MALLOC_CAP_8BIT));
#else
  auto* bytes=static_cast<uint8_t*>(std::malloc(policy.elfLength));
#endif
  if(!bytes)return fail("native provider image allocation failed");
  FILE* file=port_.owner() && providerStorageSafe()?fopen(d.elf,"rb"):nullptr;
  bool ok=file!=nullptr;size_t at=0;
  const auto started=std::chrono::steady_clock::now();
  while(ok && at<policy.elfLength) {
    const size_t chunk=std::min(size_t(4096),policy.elfLength-at);
    ok=port_.owner() && providerStorageSafe() && fread(bytes+at,1,chunk,file)==chunk;
    at+=chunk;
#ifdef ESP_PLATFORM
    vTaskDelay(1);
#endif
    if(std::chrono::steady_clock::now()-started>std::chrono::seconds(30))ok=false;
  }
  // Losing the owner or native storage custody fences every further stream
  // operation, including EOF probing and close. The native store owner latches
  // this retained state before destroying a metadata-only candidate.
  if(ok) {
    ok=port_.owner() && providerStorageSafe();
    if(ok)ok=fgetc(file)==EOF;
    if(ok)ok=port_.owner() && providerStorageSafe() && !ferror(file);
  }
  if(file) {
    if(!port_.owner() || !providerStorageSafe()) {
      retainedNativeProviderRead_=file;retainedNativeProviderBytes_=bytes;metadataCloseRetained_=true;ok=false;
    } else {
      // Establish custody before close: an error makes the native ownership
      // uncertain, so never reuse or dereference the FILE afterward.
      retainedNativeProviderRead_=file;retainedNativeProviderBytes_=bytes;
      if(fclose(file)==0) {retainedNativeProviderRead_=nullptr;retainedNativeProviderBytes_=nullptr;}
      else {metadataCloseRetained_=true;ok=false;}
    }
  }
  if(ok)ok=port_.owner() && providerStorageSafe();
  if(ok) {
    spec.requiredOsCpuAbi=policy.osCpuAbi;spec.verifiedElfBytes=bytes;
    spec.verifiedElfLength=policy.elfLength;spec.declaredImports=policy.imports;
    spec.declaredImportCount=policy.importCount;memcpy(spec.contentSha256,policy.sha256,32);
    ok=RuntimePackages::DeviceProviderExecutorV2::registerNativePolicy(graph_,spec,policy);
  }
  if(!retainedNativeProviderBytes_) {
#ifdef ESP_PLATFORM
    heap_caps_free(bytes);
#else
    std::free(bytes);
#endif
  }
  return ok || fail("native provider image admission failed");
}
const RuntimeProviders::NativeProviderPolicyV1* Runtime::nativeProviderPolicyForImage(const char* image) const {
  if(!prepared_ || metadataCloseRetained_ || !image || !port_.owner())return nullptr;
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
  if(!prepared_ || invocation_->active_ || retained_ || metadataCloseRetained_ || !port_.owner() || !inspect)return false;
  if(!inspect(context,default_,false))return false;
  for(size_t i=0;i<policyCount_;++i){
    if(!strcmp(policies_[i].elf,default_))continue;
    if(!port_.owner() || !inspect(context,policies_[i].elf,false))return false;
  }
  for(size_t i=0;i<driverCount_;++i){
    bool duplicate=false;for(size_t j=0;j<i;++j)if(!strcmp(drivers_[j].elf,drivers_[i].elf)){duplicate=true;break;}
    if(!duplicate && (!port_.owner() || !inspect(context,drivers_[i].elf,true)))return false;
  }
  return port_.owner();
}
#include "CohortRuntime.inc"
bool Runtime::launch(const char* relative) {
  if(residentEnabled_) {
    char selected[256];
    const bool legacy=invocation_==&mainInvocation_ && !invocation_->role && residentLegacyAllowed(invocation_->current_);
    if((invocation_->role!=RISC_RESIDENT_ROLE_FOREGROUND && !legacy) || !invocation_->entryRunning_ ||
       !relative || !path(root_,relative,selected,sizeof(selected)) ||
       (!residentAllowed(selected) && !residentLegacyAllowed(selected) && !(legacy && !strcmp(selected,default_))))return false;
  }
  if(invocation_->fileOpen_.phase==FileOpenState::Phase::Requested || invocation_->fileOpen_.phase==FileOpenState::Phase::Receiving)return false;
  if(promotionRunning_ || graph_.lifecycleBusy() || !active() || metadataCloseRetained_ || !appDataExitSafe() || (invocation_->installedFiles_ && invocation_->installedFiles_->retained()) || invocation_->queued_[0] || !relative || !elfPath(relative) || (port_.appExitSafe && !port_.appExitSafe())) return false;
  const bool ok=path(root_,relative,invocation_->queued_,sizeof(invocation_->queued_));
  RISC_STAGE_LOG("app launch-request file=%s result=%s",relative,ok?"accepted":"invalid-path");
  if(ok)RiscPerf::emit(20);
  return ok;
}
bool Runtime::health(risc_runtime_health_v1* h) { return active() && h && h->struct_size>=sizeof(*h) && port_.health(h); }
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
  if(!policy)return false;
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
  if(!keys(m,{"type","id","version","architecture","file_name","entry","requires"},{"description","display_name","icon","supported_file_types"}) ||
     !eq(m["type"],"application") || !eq(m["id"],id) || !eq(m["architecture"],"xtensa-esp32s3") ||
     !eq(m["entry"],"app_main") || !eq(m["file_name"],basename) || !text(m["version"],version,sizeof(version)) ||
     !RuntimePackages::safeVersion(version) || !m["requires"].is<JsonArrayConst>() ||
     m["requires"].size()!=required)return false;
  FileOpenMetadata fileMetadata;
  if(!fileOpenMetadata(m,fileMetadata))return false;
  uint32_t candidate[3],current[3];
  if(!RiscUpdate::parseVersion(version,candidate) || !RiscUpdate::parseVersion(policy->version,current) ||
     RiscUpdate::compareVersion(candidate,current)<=0)return false;
  bool matched[MaxAppPolicyGrants]{};
  for(JsonObjectConst req:m["requires"].as<JsonArrayConst>()) {
    char name[96];int64_t api;
    if(!keys(req,{"capability","api"}) || !text(req["capability"],name,sizeof(name)) || !integer(req["api"],1,UINT32_MAX,api))return false;
    size_t g=0;for(;g<policy->count;++g)if(!strcmp(name,policy->grants[g].capability) && uint32_t(api)==policy->grants[g].api)break;
    if(g==policy->count || matched[g])return false;
    matched[g]=true;
  }
  size_t rootLength=strlen(root_);
  if(strncmp(policy->elf,root_,rootLength) || policy->elf[rootLength]!='/' || strlen(policy->elf+rootLength+1)>=sizeof(out.elf))return false;
  strcpy(out.elf,policy->elf+rootLength+1);
  return appManifestPath(size_t(policy-policies_.get()),out.manifest,sizeof(out.manifest));
}
bool Runtime::appManifestPath(size_t index,char* out,size_t capacity) const {
  if(metadataCloseRetained_ || index>=policyCount_ || !out)return false;
  // Keep per-app path storage out of scarce static internal DRAM. The immutable
  // selected boot policy supplies this path; no caller filename is accepted.
  char filename[256];JsonDocument boot;
  if(!path(root_,"boot.json",filename,sizeof(filename)) || !readJson(filename,boot,&metadataCloseRetained_))return false;
  return text(boot["app_capabilities"][index]["manifest"],out,capacity);
}
bool Runtime::appInventory(size_t index,void* output,size_t capacity,uint32_t* actual) const {
  if(actual)*actual=0;
  if(!active() || index>=policyCount_ || !output || !actual || !capacity || capacity>4096 || !providerStorageSafe())return false;
  char relative[193],name[256];
  if(!appManifestPath(index,relative,sizeof(relative)) || !path(root_,relative,name,sizeof(name)))return false;
  FILE* f=fopen(name,"rb");if(!f)return false;
  size_t n=fread(output,1,capacity,f);
  bool ok=n && fgetc(f)==EOF && !ferror(f);
  if(fclose(f)!=0){metadataCloseRetained_=true;ok=false;}
  if(!ok){memset(output,0,n);return false;}
  *actual=uint32_t(n);return true;
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
void Runtime::serviceProviders() {
  if(currentRuntime!=this || !port_.owner() || retained_ || promotionRunning_ ||
     invocation_->streams_.busy() || graph_.lifecycleBusy() || !appDataExitSafe() ||
     !providerStorageSafe())return;
  graph_.service(RISC_DRIVER_SERVICE_MAX_MS);
}
void Runtime::yield(uint32_t ms) {
  if(currentRuntime!=this || !port_.owner() || yielding_ || promotionRunning_ ||
     invocation_->streams_.busy() || graph_.lifecycleBusy() || (!invocation_->active_ && !retained_))return;
  yielding_=true;
  struct Guard { bool& flag; ~Guard(){flag=false;} } guard{yielding_};
  if(!retained_) {
    // Poll work is bounded separately; each admitted yield cooperates once.
    if(appDataExitSafe())graph_.poll([](){risc_runtime_health_v1 h{}; h.struct_size=sizeof(h); return currentRuntime->health(&h)?h.uptime_ms:0;},nullptr);
    // Yield is a display/input polling path. Synchronous provider services may
    // touch storage for up to a second, so run them only at explicit lifecycle
    // boundaries, never between a panel refresh command and its BUSY sample.
    if(!graph_.activationSafe())(void)appExitBarrier();
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
bool Runtime::appExitBarrier() {
  const bool graphSafe=graph_.activationSafe();
  const bool otherSafe=!foreground_ || (!foreground_->streams_.retained() && (!foreground_->installedFiles_ || !foreground_->installedFiles_->retained()));
  const bool hostSafe=!mainInvocation_.streams_.retained() && (!mainInvocation_.installedFiles_ || !mainInvocation_.installedFiles_->retained());
  if(graphSafe && otherSafe && hostSafe && !retained_ && !invocation_->streams_.retained() && !metadataCloseRetained_ && appDataExitSafe() && (!invocation_->installedFiles_ || !invocation_->installedFiles_->retained()) && (!port_.appExitSafe || port_.appExitSafe()))return true;
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
#if defined(ESP_PLATFORM) || defined(RISC_APP_IMAGE_CACHE_TEST)
  // Only exact prepared installed-image paths enter this session's cache.
  // Loose child paths retain the ordinary reader and admission behavior.
  void* module=(policy || !strcmp(name,default_)) ?
    esp_dlopen_cached_instance(&appImages_,name) : esp_dlopen_instance(name);
#else
  void* module=esp_dlopen_instance(name);
#endif
  RISC_STAGE_LOG("app load end file=%s result=%s elapsed_us=%llu",name,module?"ok":"failed",
                 (unsigned long long)(RiscDiagnostics::monotonicUs()-loadUs));
  RiscPerf::finish(12,13,loadStart,module?1:0);
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
  native_app_memory_relocation(false);
#endif
  if(!module) {
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
    if(!native_app_memory_end()){retained_=true;fail("failed app load allocation cleanup retained");(void)appExitBarrier();return false;}
#endif
    RiscPerf::emit(21);
    invocation_->streams_.endInvocation();
    invocation_->failureKind=RISC_RESIDENT_FAILURE_LOAD;invocation_->status=RISC_RESIDENT_FAILED;
    return fail("app ELF load failed");
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
    entry();
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
bool Runtime::run() {
  if(!prepared_ || currentRuntime || !port_.owner() || retained_ || metadataCloseRetained_) return fail("runtime not launchable");
  prepared_=false; currentRuntime=this;
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
    if(fileOpenAfterRun(completed)) { resumeHost();error_[0]=0;port_.delay(1);continue; }
    if(!completed && isDefault) { ok=false; break; }
    if(residentEnabled_ && isDefault && residentContinuation_[0]) {
      ok=fail("resident host did not consume pending continuation");break;
    }
    if(!completed) port_.log("RTE_APP child=failed action=reload-default");
    if(completed && invocation_->queued_[0]) strcpy(invocation_->current_,invocation_->queued_);
    else if(!isDefault) strcpy(invocation_->current_,default_);
    else break;
    resumeHost();
    // Default reload is a fresh invocation: no ELF pointers/static state survive.
    error_[0]=0; port_.delay(1);
  }
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
