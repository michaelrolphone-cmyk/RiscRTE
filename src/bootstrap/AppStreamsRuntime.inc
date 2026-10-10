// Generic capability/session brokerage. Protocol requests stay opaque here.
bool Runtime::streamBinding(const risc_runtime_capability_v1* out,RuntimeStreams::AppStreamBinding& binding) const {
  if(!out || out->struct_size<sizeof(*out) || !out->slot || out->slot>16)return false;
  const auto& g=invocation_->appGrants_[out->slot-1];
  if(!g.live || !g.provider.slot || g.generation!=out->generation || g.api!=out->api || g.invocation!=invocation_->streams_.context())return false;
  binding={g.invocation,out->slot,g.generation,g.api,g.provider};return true;
}
bool Runtime::streamBindingValid(void* context,const RuntimeStreams::AppStreamBinding& b,bool requireActive) {
  auto* r=static_cast<Runtime*>(context);
  if(currentRuntime!=r || !r->port_.owner() || r->retained_ || r->promotionRunning_ ||
      (requireActive && !r->invocation_->active_) || !b.invocation || b.invocation!=r->invocation_->streams_.context() || !b.slot || b.slot>16)return false;
  const auto& g=r->invocation_->appGrants_[b.slot-1];
  return g.live && g.invocation==b.invocation && g.generation==b.generation && g.api==b.api &&
    g.provider.slot==b.graph.slot && g.provider.generation==b.graph.generation;
}
void Runtime::streamRetain(void* context) {
  auto* r=static_cast<Runtime*>(context);
  r->retained_=true;r->fail("stream session retained; restart required");
  (void)r->appExitBarrier();
}
void Runtime::streamYield(void* context) { static_cast<Runtime*>(context)->port_.delay(1); }
bool Runtime::streamClient(risc_stream_client_v1* out) {
  if(!out || out->struct_size<sizeof(*out))return false;
  *out={};
  if(!active() || promotionRunning_ || retained_ || !invocation_->streams_.context())return false;
  *out={RISC_STREAM_CLIENT_API_V1,sizeof(*out),invocation_->streams_.context(),
    [](uint64_t context,const risc_runtime_capability_v1* grant,const void* request,uint32_t size,uint32_t ms,risc_stream_opened_v1* opened)->int32_t {
      if(opened && opened->struct_size>=sizeof(*opened)){*opened={};opened->struct_size=sizeof(*opened);}
      auto* r=currentRuntime;
      if(!r || !r->active() || context!=r->invocation_->streams_.context())return RISC_STREAM_DENIED;
      RuntimeStreams::AppStreamBinding b{};
      if(!r->streamBinding(grant,b))return RISC_STREAM_DENIED;
      return r->invocation_->streams_.open(b,request,size,ms,opened);
    },
    [](uint64_t context,uint64_t session,const void* request,uint32_t size,uint32_t ms,void* reply,uint32_t capacity,uint32_t* count)->int32_t {
      if(count)*count=0;
      auto* r=currentRuntime;
      if(r && r->port_.owner() && r->invocation_->streams_.busy())return RISC_STREAM_BUSY;
      if(!r || !r->active())return RISC_STREAM_CLOSED;
      return r->invocation_->streams_.call(context,session,request,size,ms,reply,capacity,count);
    },
    [](uint64_t context,uint64_t session,uint32_t ms)->int32_t {
      auto* r=currentRuntime;
      if(r && r->port_.owner() && r->invocation_->streams_.busy())return RISC_STREAM_BUSY;
      if(!r || !r->active())return RISC_STREAM_CLOSED;
      return r->invocation_->streams_.close(context,session,ms);
    },
    [](uint64_t context,uint64_t stream,void* data,uint32_t capacity,uint32_t* count)->int32_t {
      if(count)*count=0;
      auto* r=currentRuntime;
      if(r && r->port_.owner() && r->invocation_->streams_.busy())return RISC_STREAM_BUSY;
      if(!r || !r->active())return RISC_STREAM_CLOSED;
      return r->invocation_->streams_.read(context,stream,data,capacity,count);
    },
    [](uint64_t context,uint64_t stream,const void* data,uint32_t size,uint32_t* count)->int32_t {
      if(count)*count=0;
      auto* r=currentRuntime;
      if(r && r->port_.owner() && r->invocation_->streams_.busy())return RISC_STREAM_BUSY;
      if(!r || !r->active())return RISC_STREAM_CLOSED;
      return r->invocation_->streams_.write(context,stream,data,size,count);
    },
    [](uint64_t context,uint64_t stream,risc_stream_client_info_v1* info)->int32_t {
      auto* r=currentRuntime;
      if(r && r->port_.owner() && r->invocation_->streams_.busy())return RISC_STREAM_BUSY;
      if(!r || !r->active())return RISC_STREAM_CLOSED;
      return r->invocation_->streams_.info(context,stream,info);
    }};
  return true;
}
