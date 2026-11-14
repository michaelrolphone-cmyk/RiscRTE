#pragma once
/* Native-owned transport only; product/UI and clock authority stay outside. */
#include "HttpTimeSample.h"
#include <RiscTimeSampleV1.h>
#include <limits>
namespace RiscCpu { namespace NativeTimeSample {
struct Session {
 HttpTimeSample::Sample transport{};
 uint64_t token=0,started=0,lastNow=0;
 uint32_t timeout=0;
 int32_t error=0;
 bool active=false,ready=false;
 risc_time_sample_v1 result{};
};
static Session session;
static uint64_t generation=0,lastClosed=0;
static bool (*owner)()=nullptr;
static bool (*networkReady)()=nullptr;
inline bool idle(){return !session.active;}
inline bool safe(){return !session.transport.retained;}
inline void configure(bool(*o)(),bool(*ready)()){owner=o;networkReady=ready;}
inline int32_t mapped(int32_t rc){
 switch(rc){
  case RISC_HTTP_OK:return RISC_TIME_SAMPLE_OK;
  case RISC_HTTP_AGAIN:return RISC_TIME_SAMPLE_PENDING;
  case RISC_HTTP_TIMEOUT:return RISC_TIME_SAMPLE_TIMEOUT;
  case RISC_HTTP_RETAINED:return RISC_TIME_SAMPLE_RETAINED;
  case RISC_HTTP_CLOCK:return RISC_TIME_SAMPLE_PROTOCOL;
  default:return RISC_TIME_SAMPLE_IO;
 }
}
inline int32_t open(void*,const risc_time_sample_request_v1* r,uint64_t* out){
 if(out)*out=0;
 if(!owner || !owner())return RISC_TIME_SAMPLE_CONTEXT;
 if(!safe())return RISC_TIME_SAMPLE_RETAINED;
 if(!r || r->struct_size!=sizeof(*r) || !out || !r->timeout_ms ||
    r->timeout_ms>RISC_TIME_SAMPLE_TIMEOUT_MAX_MS || !HttpTimeSample::hostname(r->server))return RISC_TIME_SAMPLE_INVALID;
 if(session.active)return RISC_TIME_SAMPLE_BUSY;
 if(!networkReady || !networkReady())return RISC_TIME_SAMPLE_NETWORK;
 if(generation==std::numeric_limits<uint64_t>::max())return RISC_TIME_SAMPLE_CLOSED;
 session=Session{};session.started=HttpTimeSample::now();session.lastNow=session.started;
 session.timeout=r->timeout_ms;session.transport.begin(r->server,session.started);
 session.token=++generation;session.active=true;*out=session.token;return RISC_TIME_SAMPLE_OK;
}
inline int32_t step(void*,uint64_t token,risc_time_sample_v1* out){
 if(!owner || !owner())return RISC_TIME_SAMPLE_CONTEXT;
 if(!safe())return RISC_TIME_SAMPLE_RETAINED;
 if(!session.active || !token || token!=session.token)return RISC_TIME_SAMPLE_CLOSED;
 if(!out || out->struct_size!=sizeof(*out))return RISC_TIME_SAMPLE_INVALID;
 if(session.error)return session.error;
 if(session.ready){*out=session.result;return RISC_TIME_SAMPLE_OK;}
 const uint64_t now=HttpTimeSample::now();
 if(now<session.lastNow || now-session.started>=session.timeout)return session.error=RISC_TIME_SAMPLE_TIMEOUT;
 session.lastNow=now;
 if(!networkReady || !networkReady())return session.error=RISC_TIME_SAMPLE_NETWORK;
 HttpBounds::Budget budget(session.started,session.timeout,48);
 uint64_t utc=0;const int32_t rc=mapped(session.transport.step(budget,utc));
 const uint64_t completed=HttpTimeSample::now();
 if(rc==RISC_TIME_SAMPLE_RETAINED)return session.error=rc;
 if(completed<session.lastNow || completed-session.started>=session.timeout)return session.error=RISC_TIME_SAMPLE_TIMEOUT;
 session.lastNow=completed;
 if(rc<0)return session.error=rc;
 if(rc==RISC_TIME_SAMPLE_PENDING)return rc;
 session.result={sizeof(session.result),RISC_TIME_SAMPLE_UNAUTHENTICATED,utc,completed,1000000000u,0};
 session.ready=true;*out=session.result;return RISC_TIME_SAMPLE_OK;
}
inline int32_t close(void*,uint64_t token){
 if(!owner || !owner())return RISC_TIME_SAMPLE_CONTEXT;
 if(!safe())return RISC_TIME_SAMPLE_RETAINED;
 if(!session.active)return token && token==lastClosed?RISC_TIME_SAMPLE_OK:RISC_TIME_SAMPLE_CLOSED;
 if(!token || token!=session.token)return RISC_TIME_SAMPLE_CLOSED;
 if(!session.transport.close())return RISC_TIME_SAMPLE_RETAINED;
 lastClosed=token;session=Session{};return RISC_TIME_SAMPLE_OK;
}
inline const risc_time_sample_api_v1* api(){
 static const risc_time_sample_api_v1 value{1,sizeof(value),nullptr,open,step,close};return &value;
}
} }
