#include "AppStreamSessions.h"
#include "ProviderQueueHost.h"
#include <cstring>
#include <cstdlib>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_timer.h>
#else
#include <chrono>
#endif
namespace RuntimeStreams {
namespace {
// Serialized Runtime owner task. Exhaustion never wraps or revives an old ID.
uint64_t nextToken=0;
uint32_t nextConsumer=0;
uint64_t issue() { return nextToken==UINT64_MAX ? 0 : ++nextToken; }
uint64_t nowUs() {
#ifdef ESP_PLATFORM
  return uint64_t(esp_timer_get_time());
#else
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
bool budget(uint32_t ms) { return ms && ms<=RISC_STREAM_CONTROL_MAX_MS_V1; }
bool exceeded(uint64_t began,uint32_t ms) { return nowUs()-began>uint64_t(ms)*1000; }
bool cleanError(int32_t rc) { return rc<0 && rc>=RISC_STREAM_TIMEOUT; }
bool same(const AppStreamBinding& a,const AppStreamBinding& b) {
  return a.invocation==b.invocation && a.slot==b.slot && a.generation==b.generation &&
    a.api==b.api && a.graph.slot==b.graph.slot && a.graph.generation==b.graph.generation;
}
}
AppStreamSessions::~AppStreamSessions() {
  // A retained Runtime cannot destroy custody before its graph abort guard.
  if(sessions_)for(const auto& s:*sessions_)if(s.state!=State::Free)std::abort();
}
bool AppStreamSessions::beginInvocation() {
  if(context_ || busy_ || nextConsumer==UINT32_MAX) return false;
  if(sessions_)for(const auto& s:*sessions_) if(s.state!=State::Free) return false;
  context_=issue(); if(!context_)return false;
  consumer_=++nextConsumer; return true;
}
bool AppStreamSessions::retained() const {
  if(sessions_)for(const auto& s:*sessions_)if(s.state==State::Retained)return true;
  return false;
}
bool AppStreamSessions::valid(const Session& s,bool active) const {
  if(!context_ || s.binding.invocation!=context_ || !host_.valid(host_.context,s.binding,active) ||
     graph_.interfaceFor(s.binding.graph)!=s.binding.api)return false;
  uint64_t context=0;
  return graph_.streamSessionsFor(s.binding.graph,&context)==s.adapter && context==s.providerContext;
}
AppStreamSessions::Session* AppStreamSessions::find(uint64_t context,uint64_t token,bool stream) {
  if(!context || context!=context_ || !token)return nullptr;
  if(sessions_)for(auto& s:*sessions_)if(s.state==State::Open && s.authority &&
      (stream ? s.rx==token || s.tx==token : s.session==token) && valid(s))return &s;
  return nullptr;
}
int32_t AppStreamSessions::fence(Session& s) {
  s.state=State::Retained;s.authority=false;
  revokeAuthority();
  host_.retain(host_.context);
  return RISC_STREAM_RETAINED;
}
void AppStreamSessions::revokeAuthority() {
  if(sessions_)for(auto& s:*sessions_)if(s.state!=State::Free) {
    s.authority=false;
    // Never enter provider code. Failure preserves software custody and is
    // covered by the invocation fence, which rejects every copied client.
    (void)graph_.revokeStreamGrants(s.binding.graph);
  }
}
int32_t AppStreamSessions::open(const AppStreamBinding& binding,const void* request,uint32_t size,uint32_t ms,risc_stream_opened_v1* out) {
  if(!out || out->struct_size<sizeof(*out))return RISC_STREAM_INVALID;
  *out={};out->struct_size=sizeof(*out);
  if((!request && size) || size>RISC_STREAM_CHUNK_V1 || !budget(ms))return RISC_STREAM_INVALID;
  if(busy_ || graph_.lifecycleBusy())return RISC_STREAM_BUSY;
  if(!context_ || binding.invocation!=context_ || !host_.valid(host_.context,binding,true) ||
      !binding.graph.slot || graph_.interfaceFor(binding.graph)!=binding.api)return RISC_STREAM_DENIED;
  uint64_t providerContext=0;
  const auto* adapter=graph_.streamSessionsFor(binding.graph,&providerContext);
  if(!adapter || !providerContext)return RISC_STREAM_UNSUPPORTED;
  if(!sessions_)sessions_.reset(new(std::nothrow) Sessions{});
  if(!sessions_)return RISC_STREAM_LIMIT;
  Session* slot=nullptr;
  if(sessions_)for(auto& s:*sessions_) {
    if(s.state!=State::Free && same(s.binding,binding))return RISC_STREAM_BUSY;
    if(s.state==State::Free && !slot)slot=&s;
  }
  if(!slot || nextToken>UINT64_MAX-3)return RISC_STREAM_LIMIT;
  Session& s=*slot;s.state=State::Reserved;s.binding=binding;s.adapter=adapter;s.providerContext=providerContext;
  s.session=issue();s.rx=issue();s.tx=issue();
  alignas(max_align_t) uint8_t copied[RISC_STREAM_CHUNK_V1]{};if(size)std::memcpy(copied,request,size);
  risc_provider_stream_session_v1 opened{};opened.struct_size=sizeof(opened);
  if(!graph_.beginStreamCallback()){s={};return RISC_STREAM_BUSY;}
  busy_=true;const uint64_t began=nowUs();
  const int32_t rc=adapter->open(copied,size,ms,&opened);
  const bool over=exceeded(began,ms);busy_=false;graph_.endStreamCallback();
  s.providerSession=opened.session;s.rxEndpoint=opened.rx_endpoint;s.txEndpoint=opened.tx_endpoint;
  if(over || !valid(s) || rc==RISC_STREAM_RETAINED)return fence(s);
  if(rc!=RISC_STREAM_OK) {
    if(!cleanError(rc) || opened.session || opened.rx_endpoint || opened.tx_endpoint || opened.reserved ||
        opened.struct_size!=sizeof(opened))return fence(s);
    s={};return rc;
  }
  if(opened.struct_size!=sizeof(opened) || opened.reserved || !opened.session || !opened.rx_endpoint ||
      !opened.tx_endpoint || opened.rx_endpoint==opened.tx_endpoint)return fence(s);
  for(const auto& other:*sessions_)if(&other!=&s && other.state!=State::Free &&
      other.providerContext==s.providerContext && other.providerSession==s.providerSession)return fence(s);
  const int32_t reserved=reserveEndpointPair(s.providerContext,lease(s),consumer_,s.session,s.rxEndpoint,s.txEndpoint);
  // Unvalidated output can point into somebody else's session. Never close it.
  if(reserved!=RISC_STREAM_OK)return fence(s);
  if(!graph_.grantStream(binding.graph,consumer_,s.rxEndpoint,RISC_STREAM_READ) ||
      !graph_.grantStream(binding.graph,consumer_,s.txEndpoint,RISC_STREAM_WRITE)) {
    // The exact NEW token and pair are authenticated. Revoke any first grant
    // before the one checked rollback; outputs are still entirely unpublished.
    if(close(s,ms,true)!=RISC_STREAM_OK)return RISC_STREAM_RETAINED;
    return RISC_STREAM_LIMIT;
  }
  if(!valid(s))return fence(s);
  s.state=State::Open;s.authority=true;
  out->session=s.session;out->rx=s.rx;out->tx=s.tx;
  return RISC_STREAM_OK;
}
int32_t AppStreamSessions::close(Session& s,uint32_t ms,bool active) {
  if(busy_ || graph_.lifecycleBusy())return RISC_STREAM_BUSY;
  if(s.state==State::Retained)return RISC_STREAM_RETAINED;
  if(!valid(s,active))return fence(s);
  s.state=State::Closing;s.authority=false;
  if(!graph_.revokeStreamGrants(s.binding.graph) || !graph_.beginStreamCallback())return fence(s);
  busy_=true;const uint64_t began=nowUs();
  const int32_t rc=s.adapter->close(s.providerSession,ms);
  const bool over=exceeded(began,ms);busy_=false;graph_.endStreamCallback();
  if(rc!=RISC_STREAM_OK || over || !valid(s,active))return fence(s);
  if(releaseEndpointPair(s.providerContext,lease(s),consumer_,s.session,s.rxEndpoint,s.txEndpoint)!=RISC_STREAM_OK)return fence(s);
  s={};return RISC_STREAM_OK;
}
int32_t AppStreamSessions::close(uint64_t context,uint64_t token,uint32_t ms) {
  if(!budget(ms))return RISC_STREAM_INVALID;
  if(busy_ || graph_.lifecycleBusy())return RISC_STREAM_BUSY;
  Session* s=find(context,token);return s?close(*s,ms,true):RISC_STREAM_CLOSED;
}
bool AppStreamSessions::closeGrant(const AppStreamBinding& binding) {
  if(sessions_)for(auto& s:*sessions_)if(s.state!=State::Free && same(s.binding,binding))return close(s,RISC_STREAM_CONTROL_MAX_MS_V1,false)==RISC_STREAM_OK;
  return true;
}
bool AppStreamSessions::closeAll() {
  revokeAuthority();
  if(sessions_)for(auto& s:*sessions_)if(s.state!=State::Free) {
    if(close(s,RISC_STREAM_CONTROL_MAX_MS_V1,false)!=RISC_STREAM_OK)return false;
    host_.yield(host_.context);
  }
  return true;
}
int32_t AppStreamSessions::call(uint64_t context,uint64_t token,const void* request,uint32_t size,uint32_t ms,void* reply,uint32_t capacity,uint32_t* actual) {
  if(actual)*actual=0;
  if(!actual || (!request && size) || size>RISC_STREAM_CHUNK_V1 || (!reply && capacity) || capacity>RISC_STREAM_CHUNK_V1 || !budget(ms))return RISC_STREAM_INVALID;
  if(busy_ || graph_.lifecycleBusy())return RISC_STREAM_BUSY;
  Session* s=find(context,token);if(!s)return RISC_STREAM_CLOSED;
  alignas(max_align_t) uint8_t copied[RISC_STREAM_CHUNK_V1]{},response[RISC_STREAM_CHUNK_V1]{};
  if(size)std::memcpy(copied,request,size);
  uint32_t count=0;
  if(!graph_.beginStreamCallback())return RISC_STREAM_BUSY;
  busy_=true;const uint64_t began=nowUs();
  const int32_t rc=s->adapter->call(s->providerSession,copied,size,ms,response,capacity,&count);
  const bool over=exceeded(began,ms);busy_=false;graph_.endStreamCallback();
  if(over || !valid(*s) || rc==RISC_STREAM_RETAINED || count>capacity ||
      (rc!=RISC_STREAM_OK && (!cleanError(rc) || count)))return fence(*s);
  if(rc==RISC_STREAM_OK){if(count)std::memcpy(reply,response,count);*actual=count;}
  return rc;
}
int32_t AppStreamSessions::read(uint64_t context,uint64_t token,void* data,uint32_t size,uint32_t* count) {
  if(count)*count=0;
  if(!count || (!data && size))return RISC_STREAM_INVALID;
  if(busy_ || graph_.lifecycleBusy())return RISC_STREAM_BUSY;
  Session* s=find(context,token,true);if(!s)return RISC_STREAM_CLOSED;
  if(s->rx!=token)return RISC_STREAM_DENIED;
  const int32_t rc=providerStreamRead(s->providerContext,lease(*s),consumer_,s->rxEndpoint,data,size,count);
  return rc==RISC_STREAM_RETAINED ? fence(*s) : rc;
}
int32_t AppStreamSessions::write(uint64_t context,uint64_t token,const void* data,uint32_t size,uint32_t* count) {
  if(count)*count=0;
  if(!count || (!data && size))return RISC_STREAM_INVALID;
  if(busy_ || graph_.lifecycleBusy())return RISC_STREAM_BUSY;
  Session* s=find(context,token,true);if(!s)return RISC_STREAM_CLOSED;
  if(s->tx!=token)return RISC_STREAM_DENIED;
  const int32_t rc=providerStreamWrite(s->providerContext,lease(*s),consumer_,s->txEndpoint,data,size,count);
  return rc==RISC_STREAM_RETAINED ? fence(*s) : rc;
}
int32_t AppStreamSessions::info(uint64_t context,uint64_t token,risc_stream_client_info_v1* out) {
  if(!out || out->struct_size<sizeof(*out))return RISC_STREAM_INVALID;
  if(busy_ || graph_.lifecycleBusy())return RISC_STREAM_BUSY;
  Session* s=find(context,token,true);if(!s)return RISC_STREAM_CLOSED;
  const int32_t rc=providerStreamInfo(s->providerContext,lease(*s),consumer_,token==s->rx?s->rxEndpoint:s->txEndpoint,out);
  return rc==RISC_STREAM_RETAINED || (rc==RISC_STREAM_OK && out->terminal==RISC_STREAM_RETAINED) ? fence(*s) : rc;
}
}
