#include "Runtime.h"
#include <esp_dlfcn.h>
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_elf.h>
#include "native/NativeAppMemory.h"
extern "C" void native_app_memory_relocation(bool);
#endif
namespace { RiscBoot::Runtime* currentRuntime=nullptr; }
extern "C" const risc_runtime_api_v1* risc_runtime_get_api(uint32_t version) {
  static const risc_runtime_api_v1 api={1,sizeof(api),
    [](risc_runtime_health_v1* h){return currentRuntime && currentRuntime->health(h);},
    [](uint32_t ms){if(currentRuntime) currentRuntime->yield(ms);},
    [](const char* line){return currentRuntime && currentRuntime->diagnostic(line);},
    [](const char* path){return currentRuntime && currentRuntime->launch(path);},
    [](const char* cap,uint32_t version,uint64_t instance,risc_runtime_capability_v1* out){return currentRuntime && currentRuntime->acquire(cap,version,instance,out);},
    [](risc_runtime_capability_v1* grant){return currentRuntime && currentRuntime->release(grant);}};
  return version==1 && currentRuntime && currentRuntime->active() ? &api : nullptr;
}
namespace RiscBoot {
namespace {
bool elfPath(const char* p) { size_t n=strlen(p); return n>4 && !strcmp(p+n-4,".elf"); }
}
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
  if (provides.size()!=1 || required.size()>16) return fail("driver capability bounds");
  JsonObjectConst p=provides[0]; int64_t api;
  if (!keys(p,{"capability","api"}) || !text(p["capability"],d.provides,sizeof(d.provides)) ||
      !integer(p["api"],1,UINT32_MAX,api) || !strcmp(d.provides,"hardware.device")) return fail("invalid provides");
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
        !integer(c["config_version"],1,1,api) || !c["revisions"].is<JsonArrayConst>()) return fail("invalid hardware compatibility");
    JsonArrayConst revisions=c["revisions"]; if (!revisions.size() || revisions.size()>16) return false;
    bool revision=false;
    for (JsonVariantConst v:revisions) { char s[96]; if(!text(v,s,96) || strchr(s,'*')) return false; if(!strcmp(s,dev->revision)) revision=true; }
    if (revision && !strcmp(compatible,dev->compatible) && !strcmp(type,dev->type)) ++matches;
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
  if ((attempted_ && !registrationOpen_) || !port_.owner() || !capability || !*capability || strlen(capability)>=96 || !api || !table || platformCount_==32 ||
      (strncmp(capability,"platform.",9) && strcmp(capability,"spi.bus"))) return false;
  if (scope==Scope::Global) {
    if (id || (strcmp(capability,"platform.clock") && strcmp(capability,"platform.board"))) return false;
  } else if ((scope!=Scope::Device && scope!=Scope::Bus) || !id) return false;
  const auto* header=static_cast<const uint32_t*>(table);
  if (header[0]!=api || header[1]<8) return false;
  for (size_t i=0;i<platformCount_;++i) {
    const auto& p=platforms_[i];
    if (!strcmp(p.capability,capability) && p.api==api && p.scope==scope && p.id==id) return false;
  }
  auto& p=platforms_[platformCount_++]; strcpy(p.capability,capability);
  p.api=api; p.scope=scope; p.id=id; p.table=table; return true;
}
bool Runtime::validateGraph() {
  bool edges[16][16]{};
  for(size_t i=0;i<driverCount_;++i) {
    Driver& d=drivers_[i];
    for(size_t j=0;j<i;++j) {
      if (!strcmp(d.id,drivers_[j].id) && (strcmp(d.version,drivers_[j].version) || strcmp(d.elf,drivers_[j].elf))) return fail("package instances disagree on artifact/version");
      if ((!strcmp(d.id,drivers_[j].id) && (!d.instance || !drivers_[j].instance)) ||
          (d.instance && d.instance==drivers_[j].instance)) return fail("duplicate package singleton/hardware owner");
    }
    const auto* hw=d.instance?board_.device(d.instance):nullptr;
    for(size_t r=0;r<d.count;++r) {
      auto& req=d.requirements[r]; if(!strcmp(req.capability,"hardware.device")) continue;
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
      if (platform) { req.trustedApi=platform->table; continue; }
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
  if (!value.is<JsonArrayConst>() || value.size()>8) return fail("invalid app capability policy");
  for (JsonObjectConst item:value.as<JsonArrayConst>()) {
    auto& policy=policies_[policyCount_]; char relative[193]; JsonDocument doc;
    if (!keys(item,{"manifest","grants"}) || !text(item["manifest"],relative,sizeof(relative)) ||
        !path(root_,relative,policy.elf,sizeof(policy.elf)) || !readJson(policy.elf,doc)) return fail("invalid app policy manifest path");
    JsonObjectConst manifest=doc.as<JsonObjectConst>(); char filename[128];
    if (!keys(manifest,{"type","id","version","architecture","file_name","entry","requires"},{"description","display_name"}) ||
        !eq(manifest["type"],"application") || !eq(manifest["architecture"],"xtensa-esp32s3") || !eq(manifest["entry"],"app_main") ||
        !text(manifest["id"],policy.id,sizeof(policy.id)) || !RuntimePackages::safeId(policy.id) ||
        !text(manifest["version"],policy.version,sizeof(policy.version)) || !RuntimePackages::safeVersion(policy.version) ||
        !text(manifest["file_name"],filename,sizeof(filename)) || !RuntimePackages::safeArtifact(filename) || !elfPath(filename) ||
        !manifest["requires"].is<JsonArrayConst>() || manifest["requires"].size()>8 ||
        !item["grants"].is<JsonArrayConst>() || item["grants"].size()>8) return fail("invalid app identity/declarations");
    char* slash=strrchr(policy.elf,'/'); if (!slash) return false;
    *(slash+1)=0;
    if (strlen(policy.elf)+strlen(filename)>=sizeof(policy.elf)) return fail("app path too long");
    strcat(policy.elf,filename);
    for (size_t i=0;i<policyCount_;++i) if (!strcmp(policies_[i].id,policy.id) || !strcmp(policies_[i].elf,policy.elf)) return fail("duplicate app identity/path policy");
    for (JsonObjectConst request:manifest["requires"].as<JsonArrayConst>()) {
      auto& grant=policy.grants[policy.count]; int64_t api=0;
      if (!keys(request,{"capability","api"}) || !text(request["capability"],grant.capability,sizeof(grant.capability)) ||
          !integer(request["api"],1,UINT32_MAX,api)) return fail("invalid app requirement");
      grant.api=api;
      for (size_t i=0;i<policy.count;++i) if (!strcmp(policy.grants[i].capability,grant.capability)) return fail("duplicate app requirement");
      unsigned matches=0;
      for (JsonObjectConst allowed:item["grants"].as<JsonArrayConst>()) {
        int64_t allowedApi=0, instance=0; char capability[96];
        if (!keys(allowed,{"capability","api","instance_id"}) || !text(allowed["capability"],capability,sizeof(capability)) ||
            !integer(allowed["api"],1,UINT32_MAX,allowedApi) || !integer(allowed["instance_id"],0,INT32_MAX,instance)) return fail("invalid app grant");
        if (!strcmp(capability,grant.capability) && allowedApi==api) {++matches;grant.instance=instance;}
      }
      if (matches!=1) return fail("app requirement not uniquely authorized");
      if (!strcmp(grant.capability,"platform.clock")) {
        for (size_t p=0;p<platformCount_;++p) if (platforms_[p].scope==Scope::Global && !strcmp(platforms_[p].capability,grant.capability) && platforms_[p].api==grant.api) {
          if (grant.platform>=0 || grant.instance) return fail("ambiguous app platform clock");
          grant.platform=p;
        }
        if (grant.platform<0) return fail("app platform clock unavailable");
      } else {
        if (!strncmp(grant.capability,"platform.",9) || !strcmp(grant.capability,"spi.bus") || !strcmp(grant.capability,"hardware.device")) return fail("raw platform capability denied to app");
        for (size_t d=0;d<driverCount_;++d) if (!strcmp(drivers_[d].provides,grant.capability) && drivers_[d].api==grant.api && (!grant.instance || drivers_[d].instance==grant.instance)) {
          if (grant.driver>=0) return fail("ambiguous app provider");
          grant.driver=d;
        }
        if (grant.driver<0) return fail("app provider unavailable");
      }
      ++policy.count;
    }
    if (policy.count!=item["grants"].size()) return fail("undeclared app grant");
    ++policyCount_;
  }
  return true;
}
bool Runtime::acquire(const char* capability,uint32_t api,uint64_t instance,risc_runtime_capability_v1* out) {
  if (!active() || !appPolicy_ || !out || out->struct_size<sizeof(*out) || !capability || !api || grantGeneration_==UINT32_MAX) return false;
  out->slot=out->generation=0;out->api=nullptr;
  const AppGrantPolicy* allowed=nullptr;
  for (size_t i=0;i<appPolicy_->count;++i) {
    const auto& p=appPolicy_->grants[i];
    const uint64_t selected=p.driver>=0?drivers_[p.driver].instance:0;
    if (!strcmp(p.capability,capability) && p.api==api && (!instance || selected==instance)) allowed=&p;
  }
  if (!allowed) return false;
  size_t slot=0;while(slot<16 && appGrants_[slot].live)++slot;
  if (slot==16) return false;
  auto& grant=appGrants_[slot];
  if (allowed->driver>=0) {
    const auto& driver=drivers_[allowed->driver];
    grant.provider=graph_.acquireFrom(driver.id,capability,api,driver.instance);
    if (!grant.provider.slot) return false;
    grant.api=graph_.interfaceFor(grant.provider);
  } else grant.api=platforms_[allowed->platform].table;
  const auto* header=static_cast<const uint32_t*>(grant.api);
  if (!header || header[0]!=api || header[1]<8) {
    if (grant.provider.slot && !graph_.release(grant.provider)) {retained_=true;return fail("invalid app interface retained");}
    grant={}; return false;
  }
  grant.live=true;grant.generation=++grantGeneration_;
  out->slot=slot+1;out->generation=grant.generation;out->api=grant.api;return true;
}
bool Runtime::release(risc_runtime_capability_v1* out) {
  if (!active() || !out || out->struct_size<sizeof(*out) || !out->slot || out->slot>16) return false;
  auto& grant=appGrants_[out->slot-1];
  if (!grant.live || grant.generation!=out->generation || grant.api!=out->api) return false;
  if (grant.provider.slot && !graph_.release(grant.provider)) return false;
  grant={};out->slot=out->generation=0;out->api=nullptr;return true;
}
bool Runtime::revokeApp() {
  bool ok=true;
  for (auto& grant:appGrants_) if (grant.live) {
    if (grant.provider.slot && !graph_.release(grant.provider)) ok=false;
    else grant={};
  }
  appPolicy_=nullptr;return ok;
}
bool Runtime::prepare(const char* root) {
  if(attempted_ || !port_.owner() || !root || strlen(root)>=sizeof(root_) || root[0]!='/') return fail("invalid boot invocation");
  attempted_=true; strcpy(root_,root);
  char filename[256], relative[193]; JsonDocument config, boardDoc;
  if(!path(root_,"boot.json",filename,sizeof(filename)) || !readJson(filename,config)) return fail("boot.json unreadable/invalid");
  JsonObjectConst c=config.as<JsonObjectConst>();
  if (!c["port"].isNull() && !board_.port(c["port"])) return fail(board_.error());
  if(!keys(c,{"board","default_app","drivers"},{"port","app_capabilities"}) || !text(c["board"],relative,sizeof(relative)) ||
      !path(root_,relative,filename,sizeof(filename)) || !readJson(filename,boardDoc) || !board_.load(boardDoc.as<JsonObjectConst>())) return fail(board_.error()[0]?board_.error():"board manifest unreadable/invalid");
  if(!text(c["default_app"],relative,sizeof(relative)) || !elfPath(relative) || !path(root_,relative,current_,sizeof(current_))) return fail("invalid default app path");
  if(!c["drivers"].is<JsonArrayConst>() || c["drivers"].size()>16) return fail("invalid driver list");
  // Read all manifests and validate mappings before registering/activating modules.
  for(JsonObjectConst item:c["drivers"].as<JsonArrayConst>()) {
    Driver& d=drivers_[driverCount_]; int64_t instance=0;
    if(!keys(item,{"manifest"},{"instance_id"}) || !text(item["manifest"],relative,sizeof(relative)) ||
        !path(root_,relative,d.elf,sizeof(d.elf)) || (!item["instance_id"].isNull() && !integer(item["instance_id"],1,INT32_MAX,instance))) return fail("invalid driver selection");
    d.instance=instance; JsonDocument manifestDoc;
    if(!readJson(d.elf,manifestDoc) || !manifest(manifestDoc.as<JsonObjectConst>(),d)) return fail(error_[0]?error_:"driver manifest unreadable/invalid");
    ++driverCount_;
  }
  if (port_.bindPlatforms) {
    registrationOpen_=true;
    const bool bound=port_.bindPlatforms(*this);
    registrationOpen_=false;
    if (!bound) return fail("trusted platform binding failed");
  }
  if(!validateGraph() || !appPolicies(c["app_capabilities"])) return false;
  for(size_t i=0;i<driverCount_;++i) {
    const Driver& d=drivers_[i];
    RuntimeProviders::SpecV2 spec{d.id,d.elf,d.provides,d.api,d.requirements,d.count};
    spec.hardware=d.instance?&board_.device(d.instance)->hardware:nullptr;
    if(!graph_.addVerified(spec)) return fail("driver registration failed");
  }
  strcpy(default_,current_); prepared_=true; return true;
}
bool Runtime::launch(const char* relative) {
  if(!active() || queued_[0] || !relative || !elfPath(relative)) return false;
  return path(root_,relative,queued_,sizeof(queued_));
}
bool Runtime::health(risc_runtime_health_v1* h) { return active() && h && h->struct_size>=sizeof(*h) && port_.health(h); }
void Runtime::yield(uint32_t ms) {
  if(!active()) return;
  // Owner task dispatch uses the same bounded provider poll as Reader.
  graph_.poll([](){risc_runtime_health_v1 h{}; h.struct_size=sizeof(h); return currentRuntime->health(&h)?h.uptime_ms:0;},[](){currentRuntime->port_.delay(1);});
  port_.delay(ms<1?1:ms>50?50:ms);
}
bool Runtime::diagnostic(const char* line) { return active() && line && strnlen(line,256)<256 && !strchr(line,'\n') && !strchr(line,'\r') && port_.log(line); }
bool Runtime::runOne(const char* name) {
#ifdef ESP_PLATFORM
  if(!native_app_memory_begin()) return fail("app allocation context unavailable");
  native_app_memory_relocation(true);
#endif
  // An app path may share a basename with a live driver; map a fresh image.
  void* module=esp_dlopen_instance(name);
#ifdef ESP_PLATFORM
  native_app_memory_relocation(false);
#endif
  if(!module) {
#ifdef ESP_PLATFORM
    native_app_memory_end();
#endif
    return fail("app ELF load failed");
  }
  auto entry=reinterpret_cast<void(*)()>(dlsym(module,"app_main"));
  auto init=reinterpret_cast<int(*)()>(dlsym(module,"app_module_init"));
  auto fini=reinterpret_cast<void(*)()>(dlsym(module,"app_module_fini"));
  bool ok=entry && bool(init)==bool(fini), initialized=false;
  appPolicy_=nullptr;
  for (size_t p=0;p<policyCount_;++p) if (!strcmp(policies_[p].elf,name)) appPolicy_=&policies_[p];
  active_=true;
  if(ok && init) { initialized=init()==0; ok=initialized; }
  if(ok) entry();
  if(initialized) fini();
  active_=false;
  if (retained_ || !revokeApp()) { retained_=true; return fail("app grants retained; image retained"); }
#ifdef ESP_PLATFORM
  if(!native_app_memory_end()) { retained_=true; return fail("app memory busy; image retained"); }
#endif
  if(dlclose(module)) { retained_=true; return fail("app unload failed; restart required"); }
  return ok || fail("app entry/lifecycle invalid");
}
bool Runtime::run() {
  if(!prepared_ || currentRuntime || !port_.owner() || retained_) return fail("runtime not launchable");
  prepared_=false; currentRuntime=this;
#ifdef ESP_PLATFORM
  static const esp_elfsym symbols[]={{"risc_runtime_get_api",reinterpret_cast<const void*>(&risc_runtime_get_api)},ESP_ELFSYM_END};
  if(esp_elf_register_symbol(symbols)) { currentRuntime=nullptr; return fail("runtime API registration failed"); }
#endif
  bool ok=true;
  for(size_t i=0;i<driverCount_;++i) {
    grants_[granted_]=graph_.acquireFrom(drivers_[i].id,drivers_[i].provides,drivers_[i].api,drivers_[i].instance);
    if(!grants_[granted_].slot) { ok=fail(graph_.lastError()); break; }
    ++granted_; port_.delay(1);
  }
  // One app at a time; no recursive ELF launch, directory search or fallback.
  while(ok) {
    queued_[0]=0;
    const bool isDefault=!strcmp(current_,default_);
    const bool completed=runOne(current_);
    if(retained_) { ok=false; break; }
    if(!completed && isDefault) { ok=false; break; }
    if(!completed) port_.log("RTE_APP child=failed action=reload-default");
    if(completed && queued_[0]) strcpy(current_,queued_);
    else if(!isDefault) strcpy(current_,default_);
    else break;
    // Default reload is a fresh invocation: no ELF pointers/static state survive.
    error_[0]=0; port_.delay(1);
  }
  if(!retained_) {
    while(granted_) if(!graph_.release(grants_[--granted_])) { retained_=true; ok=fail("driver quiescence failed; restart required"); }
    if(!graph_.shutdown()) { retained_=true; ok=fail("driver shutdown retained; restart required"); }
  }
  currentRuntime=nullptr; return ok;
}
}
