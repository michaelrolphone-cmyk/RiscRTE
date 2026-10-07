bool Runtime::registerRealtime(const risc_realtime_control_api_v1* api){
 if((attempted_ && !registrationOpen_) || !port_.owner() || realtimeBackend_ || !api ||
    api->api_version!=1 || api->struct_size!=sizeof(*api) || !api->read || !api->seed)return false;
 realtimeBackend_=api;return true;
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
 if(!out || out->struct_size!=sizeof(*out))return RISC_REALTIME_INVALID;
 risc_realtime_snapshot_v1 sample{};sample.struct_size=sizeof(sample);
 const auto* backend=r->realtimeBackend_;
 const auto result=backend->read(backend->context,&sample);
 if(result!=RISC_REALTIME_OK)return result;
 if(sample.struct_size!=sizeof(sample) || sample.validity>RISC_REALTIME_VALID || sample.reserved ||
    sample.epoch_seconds<0 || sample.epoch_seconds>INT32_MAX || sample.nanoseconds>=1000000000u ||
    sample.monotonic_after_us<sample.monotonic_before_us ||
    (!sample.validity && (sample.epoch_seconds || sample.nanoseconds)))return RISC_REALTIME_IO;
 *out=sample;return RISC_REALTIME_OK;
}
