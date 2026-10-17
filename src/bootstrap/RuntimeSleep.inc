// App-declared settled boundary; hardware transitions remain in mapped providers.
#if RISC_NATIVE_SERIAL_FILES
int32_t Runtime::resumeNativeSleep() {
  int32_t result=RISC_RUNTIME_SLEEP_OK;
  for(size_t n=sizeof(nativeFiles_)/sizeof(nativeFiles_[0]);n;--n) {
    auto& b=nativeFiles_[n-1];if(!b.sleepFrozen)continue;
    if(!b.owner() || !b.safe() || !b.files->exitSafe())return RISC_RUNTIME_SLEEP_RETAINED;
    if(b.sleepAttempted) {
      const int32_t rc=b.sleep->resume_sleep(b.sleep->terminal.power.volume.base.context);
      if(rc==RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE)result=RISC_RUNTIME_SLEEP_MEDIA_UNAVAILABLE;
      else if(rc!=RISC_STORAGE_SLEEP_READY && !(rc==RISC_STORAGE_SLEEP_REFUSED && !b.sleepPrepared))
        return RISC_RUNTIME_SLEEP_RETAINED;
      if(!b.owner() || !b.safe() || !b.files->exitSafe())return RISC_RUNTIME_SLEEP_RETAINED;
    }
    if(!b.files->resumeSleep())return RISC_RUNTIME_SLEEP_RETAINED;
    b.sleepFrozen=b.sleepAttempted=b.sleepPrepared=false;b.sleep=nullptr;
  }
  return result;
}
#endif
// Light sleep may retain a resident foreground stack. This checks settled
// resources without claiming that losing RAM is safe; CpuPort retains the
// separate residentResetSafe guard at actual Deep/reset entry.
bool Runtime::sleepBoundarySafe() const {
  return !sleepCalling_ && !shellLoading_ &&
#if RISC_NATIVE_SETTINGS_LIFECYCLE || RISC_NATIVE_SERIAL_FILES
    !nativeCheckpointRunning_ &&
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
    atomicExitSafe(true) &&
#endif
#if RISC_RUNTIME_PREFERENCE_STORE
    preferenceExitSafe(true) &&
#endif
#if RISC_RUNTIME_STORAGE_FILES
    storageFilesExitSafe(true) &&
#endif
    (!settings_||!settings_->retained()) && fileSourceExitSafe() &&
    !fileSource_.token && appDataExportExitSafe();
}
int32_t Runtime::sleepPrepare(uint64_t* token) {
  if(token)*token=0;
  if(!token || currentRuntime!=this || !port_.owner() || !invocation_->entryRunning_)
    return RISC_RUNTIME_SLEEP_CONTEXT;
  if(retained_)return RISC_RUNTIME_SLEEP_RETAINED;
  if(sleepToken_ || sleepCalling_ || !active() || yielding_ || promotionRunning_ ||
     invocation_->queued_[0] || graph_.lifecycleBusy() || !sleepBoundarySafe() ||
     !providerStorageSafe() || (port_.appExitSafe && !port_.appExitSafe()) || sleepSerial_==UINT64_MAX)
    return RISC_RUNTIME_SLEEP_BUSY;
  const int32_t checkpoint=serviceCheckpoint();
  if(checkpoint==RISC_RUNTIME_CHECKPOINT_RETAINED)return RISC_RUNTIME_SLEEP_RETAINED;
  if(checkpoint!=RISC_RUNTIME_CHECKPOINT_OK)return RISC_RUNTIME_SLEEP_BUSY;
#if RISC_NATIVE_SERIAL_FILES
  if(port_.sleepServicesReady && !port_.sleepServicesReady())return RISC_RUNTIME_SLEEP_BUSY;
  // Validate the whole set before touching any provider. A missing suffix is
  // never permission to leave a mounted controller open or unload its graph.
  for(auto& b:nativeFiles_)if(b.grant.slot) {
    if(!b.files->exitSafe() || !b.owner() || !b.safe())return RISC_RUNTIME_SLEEP_BUSY;
    const auto& d=drivers_[b.driver];
    if(!strcmp(d.provides,"storage.volume")) {
      b.sleep=risc_storage_volume_sleep(static_cast<const risc_storage_volume_api_v1*>(graph_.interfaceFor(b.grant)));
      if(!b.sleep)return RISC_RUNTIME_SLEEP_UNSUPPORTED;
    }
  }
#endif
  sleepToken_=++sleepSerial_;sleepOwner_=invocation_->token;sleepCalling_=true;
  int32_t result=RISC_RUNTIME_SLEEP_OK;
#if RISC_NATIVE_SERIAL_FILES
  for(auto& b:nativeFiles_)if(b.grant.slot) {
    if(!b.files->suspendSleep()){result=RISC_RUNTIME_SLEEP_BUSY;break;}
    b.sleepFrozen=true;
    if(!b.sleep)continue;
    b.sleepAttempted=true;
    b.sleepPrepared=b.sleep->prepare_sleep(b.sleep->terminal.power.volume.base.context);
    if(!b.sleepPrepared || !b.owner() || !b.safe() || !b.files->exitSafe()) {
      result=RISC_RUNTIME_SLEEP_BUSY;break;
    }
  }
  if(result==RISC_RUNTIME_SLEEP_OK)for(auto& b:nativeFiles_)if(b.sleepPrepared) {
    if(!b.sleep->commit_sleep(b.sleep->terminal.power.volume.base.context) ||
       !b.owner() || !b.safe() || !b.files->exitSafe()) {result=RISC_RUNTIME_SLEEP_BUSY;break;}
  }
  if(result!=RISC_RUNTIME_SLEEP_OK && resumeNativeSleep()==RISC_RUNTIME_SLEEP_RETAINED)
    result=RISC_RUNTIME_SLEEP_RETAINED;
#endif
  sleepCalling_=false;
  if(result==RISC_RUNTIME_SLEEP_RETAINED) {
    retained_=true;fail("native sleep preparation retained; restart required");(void)appExitBarrier(false);
    return result;
  }
  if(result!=RISC_RUNTIME_SLEEP_OK){sleepToken_=sleepOwner_=0;return result;}
  *token=sleepToken_;return RISC_RUNTIME_SLEEP_OK;
}
int32_t Runtime::sleepResume(uint64_t token) {
  if(currentRuntime!=this || !port_.owner() || !invocation_->entryRunning_ ||
     !token || token!=sleepToken_ || sleepOwner_!=invocation_->token || sleepCalling_)
    return RISC_RUNTIME_SLEEP_CONTEXT;
  if(retained_)return RISC_RUNTIME_SLEEP_RETAINED;
  // Wake registration and output holds must already be restored. Do not run
  // storage callbacks through a failed panel/native cleanup barrier.
  int32_t result=RISC_RUNTIME_SLEEP_RETAINED;
  if(active() && !yielding_ && !promotionRunning_ && !graph_.lifecycleBusy() &&
     providerStorageSafe() && (!port_.appExitSafe || port_.appExitSafe())) {
    sleepCalling_=true;
#if RISC_NATIVE_SERIAL_FILES
    result=resumeNativeSleep();
#else
    result=RISC_RUNTIME_SLEEP_OK;
#endif
    sleepCalling_=false;
  }
  if(result==RISC_RUNTIME_SLEEP_RETAINED) {
    retained_=true;fail("native sleep resume retained; restart required");(void)appExitBarrier(false);return result;
  }
  sleepToken_=sleepOwner_=0;return result;
}
