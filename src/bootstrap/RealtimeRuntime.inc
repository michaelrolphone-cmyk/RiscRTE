bool Runtime::registerRealtime(const risc_realtime_control_api_v1* api){
 if((attempted_ && !registrationOpen_) || !port_.owner() || realtimeBackend_ || !api ||
    api->api_version!=1 || api->struct_size!=sizeof(*api) || !api->read || !api->seed)return false;
 realtimeBackend_=api;return true;
}
bool Runtime::registerProviderRealtime(){
 bool needed=false;
 for(size_t i=0;i<driverCount_;++i)for(size_t j=0;j<drivers_[i].count;++j)
  if(!strcmp(drivers_[i].requirements[j].capability,RISC_PLATFORM_REALTIME_CAPABILITY))needed=true;
 // No slot or token is consumed for graphs that do not declare this service.
 if(!needed)return true;
 if(!realtimeBackend_)return false;
 providerRealtimeTable_={1,sizeof(providerRealtimeTable_),nullptr,providerRealtimeRead};
 return registerPlatform(RISC_PLATFORM_REALTIME_CAPABILITY,1,Scope::Global,0,&providerRealtimeTable_);
}
int32_t Runtime::providerRealtimeRead(void* context,risc_realtime_snapshot_v1* out){
 auto* r=currentRuntime;
 if(!r || !context || !r->active() || !r->entryRunning_ || r->retained_)return RISC_REALTIME_CONTEXT;
 if(!r->graph_.dependencyReadSafe())return RISC_REALTIME_CONTEXT;
 if(!r->providerStorageSafe()) {
  // A copied token cannot revive if a transient safety barrier later clears.
  for(auto& storage:r->providerStorage_)storage.realtime.context=nullptr;
  return RISC_REALTIME_CONTEXT;
 }
 for(const auto& storage:r->providerStorage_)
  if(storage.owner==r && storage.live && storage.needsRealtime && storage.realtime.context==context)
   return r->readRealtime(out);
 return RISC_REALTIME_CONTEXT;
}
Runtime* Runtime::realtimeContext(void* context,bool control){
 auto* r=currentRuntime;
 if(!r || !context || !r->active() || !r->entryRunning_ || r->retained_ ||
    !r->appPolicy_ || !r->appDataExitSafe() || !r->providerStorageSafe())return nullptr;
 const void* table=nullptr;
 if(context==r->realtimeControlContext_)table=&r->realtimeControlTable_;
 else if(!control && context==r->realtimeContext_)table=&r->realtimeTable_;
 if(table)for(const auto& grant:r->appGrants_)if(grant.live && grant.api==table)return r;
 return nullptr;
}
int32_t Runtime::realtimeSeed(void* context,int64_t seconds,uint32_t nanos){
 auto* r=realtimeContext(context,true);if(!r)return RISC_REALTIME_CONTEXT;
 if(seconds<0 || seconds>INT32_MAX || nanos>=1000000000u || nanos%1000u)return RISC_REALTIME_INVALID;
 const auto* backend=r->realtimeBackend_;
 return backend->seed(backend->context,seconds,nanos);
}
int32_t Runtime::realtimeRead(void* context,risc_realtime_snapshot_v1* out){
 auto* r=realtimeContext(context,false);if(!r)return RISC_REALTIME_CONTEXT;
 return r->readRealtime(out);
}
int32_t Runtime::readRealtime(risc_realtime_snapshot_v1* out){
 if(!out || out->struct_size!=sizeof(*out))return RISC_REALTIME_INVALID;
 risc_realtime_snapshot_v1 sample{};sample.struct_size=sizeof(sample);
 const auto* backend=realtimeBackend_;
 const auto result=backend->read(backend->context,&sample);
 if(result!=RISC_REALTIME_OK)return result;
 if(sample.struct_size!=sizeof(sample) || sample.validity>RISC_REALTIME_VALID || sample.reserved ||
    sample.epoch_seconds<0 || sample.epoch_seconds>INT32_MAX || sample.nanoseconds>=1000000000u ||
    sample.monotonic_after_us<sample.monotonic_before_us ||
    (!sample.validity && (sample.epoch_seconds || sample.nanoseconds)))return RISC_REALTIME_IO;
 *out=sample;return RISC_REALTIME_OK;
}
