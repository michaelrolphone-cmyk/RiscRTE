/* Scoped addition to ModuleV2, inside namespace RuntimeProviders. No service
 * proxy or hardware methods. Current descriptor/root contracts stay frozen. */
bool ModuleV2::nativeExecutableSpan(uintptr_t& begin,size_t& bytes) const {
  return nativeExecutableSpan(0,begin,bytes);
}
bool ModuleV2::nativeExecutableSpan(unsigned region,uintptr_t& begin,size_t& bytes) const {
#ifdef ESP_PLATFORM
  if(!privileged_image_ || !handle_ || region>1)return false;
  auto* image=static_cast<esp_elf_t*>(handle_);
  if(image->internal_v1.tag && image->internal_v1.tag!=ESP_ELF_INTERNAL_TAG_V1)return false;
  if(region && image->internal_v1.tag!=ESP_ELF_INTERNAL_TAG_V1)return false;
  const auto& section=region ? image->internal_v1.sec[ESP_ELF_INTERNAL_TEXT_V1] : image->sec[ELF_SEC_TEXT];
  const uintptr_t textData=section.addr;
  const size_t textBytes=section.size;
  // Loaded text is a writable D-bus allocation. Reject overflow before the
  // loader's sec.addr+size expression; remap only this exact raw span, never
  // an already-relocated callable address or an architecture-wide range.
  if(!textData || !textBytes || textBytes>UINTPTR_MAX-textData)return false;
  uintptr_t textCode=textData,lastCode=textData+(textBytes-1);
#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
  textCode=elf_remap_text(image,textData);
  lastCode=elf_remap_text(image,textData+(textBytes-1));
#endif
  if(!textCode || textBytes>UINTPTR_MAX-textCode ||
     lastCode!=textCode+(textBytes-1))return false;
  begin=textCode;bytes=textBytes;
  return true;
#else
  (void)region;(void)begin;(void)bytes;return false;
#endif
}

bool ModuleV2::nativeTaskBegin(bool nativeRoot) {
  if(!ownedPolicy_ || ownedPolicy_->osCpuAbi!=5)return true;
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1 && defined(ESP_PLATFORM)
  if(!nativeTaskStaging_ || !nativeRoot || nativeTaskWindow_ || nativeTaskBinding_.token || !privileged_image_ ||
     !handle_ || !streamApi_.streams.context || streamsRevoked_ || !leaseAttempted_ ||
     !lease_.context || !lease_.begin || !lease_.revoke || !lease_.safe ||
     !leaseSafe() || !streamSafe())return false;
  uintptr_t textCode=0;size_t textBytes=0;
  if(!nativeExecutableSpan(textCode,textBytes))return false;
  const auto* image=static_cast<const esp_elf_t*>(handle_);
  esp_cpu_task_mapping_v1 mapping{};
  mapping.module=this;mapping.mapping=streamApi_.streams.context;
  mapping.executable={textCode,textBytes};
  mapping.data={image->sec[ELF_SEC_DATA].addr,image->sec[ELF_SEC_DATA].size};
  mapping.bss={image->sec[ELF_SEC_BSS].addr,image->sec[ELF_SEC_BSS].size};
  int rc;
  if(!image->internal_v1.tag)rc=esp_cpu_task_binding_begin_v1(&mapping,&nativeTaskBinding_);
  else {
    esp_cpu_task_mapping_v2 extended{};
    extended.struct_size=sizeof(extended);extended.tag=ESP_CPU_TASK_MAPPING_TAG_V2;
    extended.legacy=mapping;
    if(image->internal_v1.sec[ESP_ELF_INTERNAL_TEXT_V1].size) {
      uintptr_t internalCode=0;size_t internalBytes=0;
      if(!nativeExecutableSpan(1,internalCode,internalBytes))return false;
      extended.internal_executable={internalCode,internalBytes};
    }
    extended.internal_data={image->internal_v1.sec[ESP_ELF_INTERNAL_DATA_V1].addr,
                            image->internal_v1.sec[ESP_ELF_INTERNAL_DATA_V1].size};
    extended.internal_bss={image->internal_v1.sec[ESP_ELF_INTERNAL_BSS_V1].addr,
                           image->internal_v1.sec[ESP_ELF_INTERNAL_BSS_V1].size};
    rc=esp_cpu_task_binding_begin_v2(&extended,&nativeTaskBinding_);
  }
  nativeTaskWindow_=rc==RISC_CPU_SCOPE_OK;
  return nativeTaskWindow_;
#else
  (void)nativeRoot;return false; // Explicit5 cannot silently use v2/ordinary host.
#endif
}
bool ModuleV2::nativeTaskEnd(bool started) {
  if(!ownedPolicy_ || ownedPolicy_->osCpuAbi!=5)return started;
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
  if(!nativeTaskWindow_)return false;
  nativeTaskWindow_=false;
  return esp_cpu_task_binding_end_v1(nativeTaskBinding_,started)==RISC_CPU_SCOPE_OK && started;
#else
  (void)started;return false;
#endif
}
bool ModuleV2::nativeTaskPrepareClose() {
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
  if(nativeTaskWindow_)return false;
  if(nativeTaskBinding_.token) {
    const int rc=esp_cpu_task_binding_cancel_staged_v1(nativeTaskBinding_);
    if(rc!=RISC_CPU_SCOPE_OK && rc!=RISC_CPU_SCOPE_CONTEXT)return false;
  }
#endif
  // An already committed owner is never cancelled here. Its existing actual
  // tail, Module quiesce and image-wide callback/lease obligations still apply.
  return nativeTaskSafe();
}
bool ModuleV2::nativeTaskSafe() const {
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
  return !nativeTaskWindow_ && esp_cpu_task_binding_safe_v1(nativeTaskBinding_);
#else
  return true;
#endif
}
bool ModuleV2::nativeTaskRelease() {
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
  return !nativeTaskWindow_ && esp_cpu_task_binding_release_v1(&nativeTaskBinding_);
#else
  return true;
#endif
}
bool ModuleV2::commitNativeTaskOwner(const NativeProviderPolicyV1* policy,uint64_t mapping) {
  if(nativeTaskCommitted_ || !ownedPolicy_ || ownedPolicy_->osCpuAbi!=5 || !nativeRootReady(policy,mapping))return false;
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
  if(nativeTaskWindow_ || !esp_cpu_task_binding_commit_v1(nativeTaskBinding_,this,mapping)) {
    retainNativeRoot();return false;
  }
  if(!nativeRootReady(policy,mapping)){retainNativeRoot();return false;}
  nativeTaskCommitted_=true;
  return true; // Entry permission only. Provider readiness is separately polled.
#else
  return false;
#endif
}
