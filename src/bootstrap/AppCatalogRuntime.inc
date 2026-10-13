// Included inside RiscBoot. Catalog reads expose only metadata already accepted
// by the native admission owner. There is no filesystem or provider traversal.
void Runtime::appCatalogMetadata(JsonObjectConst manifest,AppCatalogMetadata& out) {
  out={};
  // Presentation metadata cannot narrow existing native app admission. Only
  // an explicit Boolean false hides a row; malformed optional fields normalize
  // to the already-validated ID or empty descriptive values.
  if(manifest["launcher_visible"].is<bool>())out.visible=manifest["launcher_visible"].as<bool>();
  auto copy=[](JsonVariantConst value,char* target,size_t capacity) {
    if(!text(value,target,capacity))return false;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(target);*p;++p)
      if(*p<32 || *p==127)return false;
    return true;
  };
  if(!copy(manifest["display_name"],out.displayName,sizeof(out.displayName)))
    strcpy(out.displayName,manifest["id"].as<const char*>());
  if(!copy(manifest["icon"],out.icon,sizeof(out.icon)))out.icon[0]=0;
  if(!copy(manifest["min_firmware_version"],out.minFirmware,sizeof(out.minFirmware)))out.minFirmware[0]=0;
}
void Runtime::appCatalogChanged() {
  // Zero is terminal exhaustion, never a recycled publication identity.
  if(appCatalogGeneration_)++appCatalogGeneration_;
}
bool Runtime::appCatalogRefresh(uint32_t& count) {
  count=0;bool changed=false,publishable=true;
  for(size_t i=0;i<MaxAppPolicies;++i) {
    uint8_t state=0;
    if(i<policyCount_) {
      const auto& policy=policies_[i];
      if(policy.enabled && !policy.catalogAuthority && policy.catalog.visible) {
        char resolved[256];
        const char* relative=policy.elf+strlen(root_)+1;
        const bool representable=strlen(relative)<sizeof(risc_app_catalog_entry_v1::file_name);
        const bool ready=representable && installedLaunchPath(relative,resolved,sizeof(resolved)) && !strcmp(resolved,policy.elf);
        if(!representable)publishable=false;
        state=ready?2:1;++count;
      }
    }
    if(appCatalogState_[i]!=state){appCatalogState_[i]=state;changed=true;}
  }
  if(changed)appCatalogChanged();
  return publishable && appCatalogGeneration_ && count<=RISC_APP_CATALOG_MAX;
}
Runtime* Runtime::appCatalogEnter(void* context,int32_t& status) {
  status=RISC_APP_CATALOG_CONTEXT;
  Runtime* r=currentRuntime;
  if(!r || !context)return nullptr;
  auto* invocation=r->invocation_;
  if(invocation->appCatalogBusy_ || invocation->appCatalogContext_!=context)return nullptr;
  bool live=false;
  for(const auto& grant:invocation->appGrants_)
    if(grant.live && grant.invocation==invocation->streams_.context() && grant.api==&invocation->appCatalogTable_)live=true;
  if(!live)return nullptr;
  if(r->retained_){status=RISC_APP_CATALOG_RETAINED;return nullptr;}
  invocation->appCatalogBusy_=true;
  const bool active=r->port_.owner() && r->invocation_==invocation && invocation->active_ &&
    invocation->entryRunning_ && !invocation->streams_.busy() && !r->promotionRunning_ && !r->graph_.lifecycleBusy();
  if(!active){invocation->appCatalogBusy_=false;return nullptr;}
  const bool safe=r->providerStorageSafe();
  // Native safety hooks may reenter. No bytes are copied if the original grant
  // was revoked or its invocation lost custody while checking the barrier.
  if(currentRuntime!=r || r->invocation_!=invocation || invocation->appCatalogContext_!=context || !invocation->active_ || !r->port_.owner()) {
    invocation->appCatalogBusy_=false;return nullptr;
  }
  if(!safe || r->retained_) {
    r->retained_=true;invocation->active_=false;
    invocation->appCatalogBusy_=false;status=RISC_APP_CATALOG_RETAINED;return nullptr;
  }
  status=RISC_APP_CATALOG_OK;return r;
}
int32_t Runtime::appCatalogSnapshot(void* context,risc_app_catalog_snapshot_v1* out) {
  int32_t status;Runtime* r=appCatalogEnter(context,status);if(!r)return status;
  struct Leave {bool& busy;~Leave(){busy=false;}} leave{r->invocation_->appCatalogBusy_};
  if(!out || out->struct_size<sizeof(*out))return RISC_APP_CATALOG_INVALID;
  uint32_t count=0;if(!r->appCatalogRefresh(count))return RISC_APP_CATALOG_UNAVAILABLE;
  risc_app_catalog_snapshot_v1 value{sizeof(value),count,r->appCatalogGeneration_};
  *out=value;return RISC_APP_CATALOG_OK;
}
int32_t Runtime::appCatalogGet(void* context,uint64_t generation,uint32_t index,risc_app_catalog_entry_v1* out) {
  int32_t status;Runtime* r=appCatalogEnter(context,status);if(!r)return status;
  struct Leave {bool& busy;~Leave(){busy=false;}} leave{r->invocation_->appCatalogBusy_};
  if(!out || out->struct_size<sizeof(*out) || !generation)return RISC_APP_CATALOG_INVALID;
  uint32_t count=0;if(!r->appCatalogRefresh(count))return RISC_APP_CATALOG_UNAVAILABLE;
  if(generation!=r->appCatalogGeneration_)return RISC_APP_CATALOG_STALE;
  if(index>=count)return RISC_APP_CATALOG_NOT_FOUND;
  for(size_t i=0;i<r->policyCount_;++i)if(r->appCatalogState_[i]) {
    if(index--!=0)continue;
    const auto& policy=r->policies_[i];
    risc_app_catalog_entry_v1 value{};value.struct_size=sizeof(value);
    value.flags=r->appCatalogState_[i]==2?RISC_APP_CATALOG_AVAILABLE:0;
    strcpy(value.id,policy.id);
    strcpy(value.display_name,policy.catalog.displayName);
    strcpy(value.file_name,policy.elf+strlen(r->root_)+1);
    strcpy(value.min_firmware_version,policy.catalog.minFirmware);
    strcpy(value.icon,policy.catalog.icon);
    *out=value;return RISC_APP_CATALOG_OK;
  }
  return RISC_APP_CATALOG_NOT_FOUND;
}
