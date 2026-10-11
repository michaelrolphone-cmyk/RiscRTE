void* Runtime::loadRemovableApp(const AppPolicy& policy) {
#if defined(ESP_PLATFORM) || defined(RISC_REMOVABLE_APP_TEST)
  // Resolve only a provider selected by the complete, validated boot graph.
  // Storage authority here belongs to the loader, not to the launched app.
  if(policy.sourceDriver<0 || !port_.owner() || !providerStorageSafe() || !graph_.activationSafe()) {
    fail("SD app volume busy");return nullptr;
  }
  const auto& driver=drivers_[policy.sourceDriver];
  auto grant=graph_.acquireFrom(driver.id,"storage.volume",1,driver.instance);
  if(!grant.slot) {
    if(!graph_.activationSafe())retained_=true;
    fail("SD app volume unavailable");return nullptr;
  }
  const auto* volume=static_cast<const risc_storage_volume_api_v1*>(graph_.interfaceFor(grant));
  struct ReadContext {
    Runtime* runtime;
    std::chrono::steady_clock::time_point start,lastYield;
    unsigned calls=0;
  } context{this,std::chrono::steady_clock::now(),std::chrono::steady_clock::now(),0};
  RemovableAppImage image;
  const bool read=image.read(volume,policy.sourcePath,{
    [](size_t n)->void*{return esp_elf_malloc(n,false);},
    [](void* p){esp_elf_free(p);},
    [](void* raw)->bool {
      auto& c=*static_cast<ReadContext*>(raw);auto* r=c.runtime;
      const auto now=std::chrono::steady_clock::now();
      if(now-c.start>std::chrono::seconds(30) || !r->port_.owner() || !r->providerStorageSafe() || !r->graph_.activationSafe())return false;
      if(++c.calls>=8 || now-c.lastYield>=std::chrono::milliseconds(2)) {
        r->port_.delay(1);c.calls=0;c.lastYield=std::chrono::steady_clock::now();
      }
      return r->port_.owner() && r->providerStorageSafe() && r->graph_.activationSafe();
    },&context});
  if(image.closeRetained) {
    retainedAppSource_=grant;retained_=true;fail(image.error);return nullptr;
  }
  if(!graph_.release(grant)) {
    if(image.bytes)esp_elf_free(image.bytes);
    retainedAppSource_=grant;retained_=true;fail("SD app volume release retained; restart required");return nullptr;
  }
  if(!read){fail(image.error);return nullptr;}
  // No SD handles or pointers survive this boundary. The normal ELF validator
  // and relocation/import checks still apply; no pathname cache is involved.
  if(!esp_elf_validate_file(image.bytes,image.size)) {
    esp_elf_free(image.bytes);fail("SD app ELF malformed or incompatible");return nullptr;
  }
  struct Snapshot {uint8_t* bytes;size_t size;} snapshot{image.bytes,image.size};
  void* module=esp_dlopen_reader_instance(policy.elf,[](void* raw,const char*,elf_file_t* file)->int {
    auto& s=*static_cast<Snapshot*>(raw);
    if(!s.bytes)return -1;
    file->payload=s.bytes;file->size=s.size;s.bytes=nullptr;return 0;
  },&snapshot);
  // A registry allocation/name failure can occur before the reader is called.
  if(snapshot.bytes)esp_elf_free(snapshot.bytes);
  if(!module)fail("SD app ELF relocation or imports failed");
  return module;
#else
  (void)policy;fail("SD app loading requires the native snapshot loader");return nullptr;
#endif
}

void* Runtime::openDriverImage(void* raw,const char* path,const void* table) {
#if defined(ESP_PLATFORM) || defined(RISC_REMOVABLE_APP_TEST)
  auto& d=*static_cast<Driver*>(raw);auto* r=d.sourceOwner;
  if(!r || r->retained_ || !r->port_.owner() || !r->providerStorageSafe())return nullptr;
  struct Context{Runtime*r;std::chrono::steady_clock::time_point start;}context{r,std::chrono::steady_clock::now()};
  RemovableAppImage image;
  const bool read=image.read(static_cast<const risc_storage_volume_api_v1*>(table),d.sourcePath,{
    [](size_t n)->void*{return esp_elf_malloc(n,false);},[](void*p){esp_elf_free(p);},
    [](void*raw)->bool{auto&c=*static_cast<Context*>(raw);
      if(!c.r->port_.owner() || !c.r->providerStorageSafe() || std::chrono::steady_clock::now()-c.start>std::chrono::seconds(30))return false;
      c.r->port_.delay(1);return c.r->port_.owner() && c.r->providerStorageSafe();},&context});
  if(image.closeRetained){r->retained_=true;r->providerFileRetained_=true;r->fail("SD provider close retained");return nullptr;}
  void* module=nullptr;
  if(read && esp_elf_validate_file(image.bytes,image.size)) {
    module=esp_dlopen_reader_instance(path,[](void*raw,const char*,elf_file_t*file)->int{
      auto&s=*static_cast<RemovableAppImage*>(raw);file->payload=s.bytes;file->size=s.size;s.bytes=nullptr;return 0;
    },&image);
  }
  if(image.bytes)esp_elf_free(image.bytes);
  if(module || !r->providerStorageSafe())return module;
  return d.internalFallback?esp_dlopen_instance(path):nullptr;
#else
  (void)raw;(void)path;(void)table;return nullptr;
#endif
}
