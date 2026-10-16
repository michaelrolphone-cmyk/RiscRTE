void* Runtime::loadRemovableApp(const AppPolicy& policy) {
#if defined(ESP_PLATFORM) || defined(RISC_REMOVABLE_APP_TEST)
  // Fallback uses the same boot-owned identity, policy and normal loader. No
  // cached removable bytes or dynamically installed identity can select it.
  auto fallback=[&]()->void* {
    if(!policy.internalFallback || retained_ || !port_.owner() ||
       !providerStorageSafe() || !graph_.activationSafe())return nullptr;
    void* module=openInternalImage(policy.elf);
    if(!module)fail("internal app fallback ELF load failed");
    return module;
  };
  // Resolve only a provider selected by the complete, validated boot graph.
  // Storage authority here belongs to the loader, not to the launched app.
  if(policy.sourceDriver<0 || retained_ || !port_.owner() || !providerStorageSafe() || !graph_.activationSafe()) {
    fail("app image source busy");return nullptr;
  }
  const auto& driver=drivers_[policy.sourceDriver];
  auto grant=graph_.acquireFrom(driver.id,policy.sourceFiles?"storage.files":"storage.volume",1,driver.instance);
  if(!grant.slot) {
    if(!graph_.activationSafe())retained_=true;
    fail("app image source unavailable");return fallback();
  }
  const void* source=graph_.interfaceFor(grant);
  struct ReadContext {
    Runtime* runtime;
    std::chrono::steady_clock::time_point start,lastYield;
    size_t sinceYield=0;
  } context{this,std::chrono::steady_clock::now(),std::chrono::steady_clock::now(),0};
  RemovableAppImage image;
  const bool read=image.read(source,policy.sourceFiles,policy.sourcePath,{
    [](size_t n)->void*{return esp_elf_malloc(n,false);},
    [](void* p){esp_elf_free(p);},
    [](void* raw)->bool {auto*r=static_cast<ReadContext*>(raw)->runtime;return !r->retained_ && r->port_.owner() && r->providerStorageSafe() && r->graph_.activationSafe();},
    [](void* raw,size_t n)->bool {
      auto& c=*static_cast<ReadContext*>(raw);const auto now=std::chrono::steady_clock::now();
      if(now-c.start>=std::chrono::seconds(30))return false;
      c.sinceYield+=n;
      if(c.sinceYield>=32*1024 || now-c.lastYield>=std::chrono::milliseconds(2)) {
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#else
        if(c.runtime->port_.delay)c.runtime->port_.delay(1);
#endif
        c.sinceYield=0;c.lastYield=std::chrono::steady_clock::now();
      }
      return std::chrono::steady_clock::now()-c.start<std::chrono::seconds(30);
    },&context});
  if(image.closeRetained) {
    retainedRemovableRead_=image.retainedSession;retainedRemovableBytes_=image.bytes;
    retainedAppSource_=grant;retained_=true;fail(image.error);return nullptr;
  }
  if(!port_.owner() || !providerStorageSafe() || !graph_.activationSafe() || !graph_.release(grant)) {
    if(image.bytes)esp_elf_free(image.bytes);
    retainedAppSource_=grant;retained_=true;fail("app image source release retained; restart required");return nullptr;
  }
  if(!read){fail(image.error);return fallback();}
  // The publisher binds this immutable boot path to the admitted core image.
  // Verify the owned snapshot, never the mutable source after cleanup.
  if(policy.internalFallback && !coreAppImageMatches(policy.sourcePath,image.bytes,image.size)) {
    esp_elf_free(image.bytes);fail("SD core app digest mismatch");return fallback();
  }
  if(retained_ || !port_.owner() || !providerStorageSafe() || !graph_.activationSafe()) {
    esp_elf_free(image.bytes);retained_=true;fail("SD app load custody lost");return nullptr;
  }
  // No SD handles or pointers survive this boundary. The normal ELF validator
  // and relocation/import checks still apply; no pathname cache is involved.
  if(!esp_elf_validate_file(image.bytes,image.size) || !esp_elf_admit_managed_app(policy.elf,image.bytes,image.size)) {
    esp_elf_free(image.bytes);fail("SD app ELF malformed or incompatible");return fallback();
  }
  struct Snapshot {uint8_t* bytes;size_t size;} snapshot{image.bytes,image.size};
  void* module=esp_dlopen_reader_instance(policy.elf,[](void* raw,const char*,elf_file_t* file)->int {
    auto& s=*static_cast<Snapshot*>(raw);
    if(!s.bytes)return -1;
    file->payload=s.bytes;file->size=s.size;s.bytes=nullptr;return 0;
  },&snapshot);
  // A registry allocation/name failure can occur before the reader is called.
  if(snapshot.bytes)esp_elf_free(snapshot.bytes);
  if(!module){fail("app ELF relocation or imports failed");return fallback();}
  return module;
#else
  (void)policy;fail("source image loading requires the native snapshot loader");return nullptr;
#endif
}

void* Runtime::openDriverImage(void* raw,const char* path,const void* table) {
  if(!raw)return nullptr;
  auto& d=*static_cast<Driver*>(raw);auto* r=d.sourceOwner;
  if(d.sourceDriver<0)return r?r->openInternalImage(path):nullptr;
#if defined(ESP_PLATFORM) || defined(RISC_REMOVABLE_APP_TEST)
  if(!r || r->retained_ || !r->port_.owner() || !r->providerStorageSafe())return nullptr;
  struct Context{Runtime*r;std::chrono::steady_clock::time_point start,lastYield;size_t sinceYield=0;};
  const auto now=std::chrono::steady_clock::now();Context context{r,now,now,0};
  RemovableAppImage image;
  const bool read=image.read(table,d.sourceFiles,d.sourcePath,{
    [](size_t n)->void*{return esp_elf_malloc(n,false);},[](void*p){esp_elf_free(p);},
    [](void*raw)->bool{auto*r=static_cast<Context*>(raw)->r;return !r->retained_ && r->port_.owner() && r->providerStorageSafe();},
    [](void*raw,size_t n)->bool{auto&c=*static_cast<Context*>(raw);const auto at=std::chrono::steady_clock::now();
      if(at-c.start>=std::chrono::seconds(30))return false;
      c.sinceYield+=n;
      if(c.sinceYield>=32*1024 || at-c.lastYield>=std::chrono::milliseconds(2)) {
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#else
        if(c.r->port_.delay)c.r->port_.delay(1);
#endif
        c.sinceYield=0;c.lastYield=std::chrono::steady_clock::now();
      }
      return std::chrono::steady_clock::now()-c.start<std::chrono::seconds(30);},&context});
  if(image.closeRetained){r->retainedRemovableRead_=image.retainedSession;r->retainedRemovableBytes_=image.bytes;
    r->retained_=true;r->providerFileRetained_=true;r->fail("provider image source custody retained");return nullptr;}
  if(r->retained_ || !r->port_.owner() || !r->providerStorageSafe()) {
    if(image.bytes)esp_elf_free(image.bytes);
    r->retained_=true;r->providerFileRetained_=true;r->fail("SD provider source custody lost");return nullptr;
  }
  // Core mirrors share the app mirror's exact content-addressed leaf grammar.
  // Preserve legacy external/ordinary driver paths. The graph owns and keeps
  // the source dependency pinned throughout this callback and module lifetime.
  uint8_t expected[32]{};
  const bool pinned=d.internalFallback && coreAppImageDigest(d.sourcePath,expected);
  const bool matching=!pinned || (read && coreAppImageMatches(d.sourcePath,image.bytes,image.size));
  if(read && !matching)r->fail("SD core driver digest mismatch");
  void* module=nullptr;
  if(read && matching && esp_elf_validate_file(image.bytes,image.size) && esp_elf_admit_managed_app(path,image.bytes,image.size)) {
    module=esp_dlopen_reader_instance(path,[](void*raw,const char*,elf_file_t*file)->int{
      auto&s=*static_cast<RemovableAppImage*>(raw);file->payload=s.bytes;file->size=s.size;s.bytes=nullptr;return 0;
    },&image);
  }
  if(image.bytes)esp_elf_free(image.bytes);
  if(module || r->retained_ || !r->port_.owner() || !r->providerStorageSafe())return module;
  return d.internalFallback?r->openInternalImage(path):nullptr;
#else
  (void)raw;(void)path;(void)table;return nullptr;
#endif
}
