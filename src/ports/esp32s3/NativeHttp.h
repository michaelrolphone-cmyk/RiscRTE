#pragma once
/* Generic nonblocking TLS/HTTP transport for IDF4.4. A single native-owned
 * session has bounded PSRAM metadata and no app pointers/tasks/callbacks.
 * SDK DNS may take its configured resolver timeout; all later TLS/read/write
 * calls are nonblocking. Each caller step yields through the runtime. */
#include "HttpResponse.h"
#include <esp_tls.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <new>
#include <cstdio>
namespace RiscCpu { namespace NativeHttp {
inline uint64_t now(){return static_cast<uint64_t>(esp_timer_get_time()/1000);}
using Verify=int(*)(void*,mbedtls_x509_crt*,int,uint32_t*);
enum class Phase { Connect,Send,Receive };
struct Session {
  uint64_t token,started,utc;HttpBounds::Budget budget;HttpResponse::Decoder decoder;
  esp_tls_t* tls=nullptr;esp_tls_cfg_t config{};Verify verify=nullptr;void* verifyContext=nullptr;
  Phase phase=Phase::Connect;int32_t error=0;bool retained=false;unsigned redirects=0;
  char url[RISC_HTTP_URL_MAX+1]{},host[254]{},request[RISC_HTTP_URL_MAX+384]{};
  uint32_t requestBytes=0,sent=0,wire=0;
  Session(uint64_t id,const risc_http_request_v1& r):token(id),started(now()),utc(r.utc_seconds),
    budget(started,r.timeout_ms,r.max_bytes),decoder(r.max_bytes){std::strcpy(url,r.url);}
};
static Session* session=nullptr;
static uint64_t generation=0,lastClosed=0;
static bool (*owner)()=nullptr;static bool (*networkReady)()=nullptr;
inline bool idle(){return session==nullptr;}
inline bool safe(){return !session || !session->retained;}
inline void configure(bool(*o)(),bool(*ready)()){owner=o;networkReady=ready;}
inline int compare(const mbedtls_x509_time& a,const HttpBounds::Calendar& b){
  const int left[]={a.year,a.mon,a.day,a.hour,a.min,a.sec};
  const int right[]={b.year,b.month,b.day,b.hour,b.minute,b.second};
  for(unsigned i=0;i<6;++i){if(left[i]<right[i])return -1;if(left[i]>right[i])return 1;}return 0;
}
inline int verifyCertificate(void* context,mbedtls_x509_crt* crt,int depth,uint32_t* flags){
  auto* s=static_cast<Session*>(context);
  if(!s || s!=session || !s->verify || !crt || !flags)return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
  const int result=s->verify(s->verifyContext,crt,depth,flags);
  // The pinned SDK omits HAVE_TIME_DATE. Validate every presented certificate
  // explicitly; do not alter global system time or disable chain/hostname checks.
  const uint64_t seconds=s->utc+(now()-s->started)/1000u;
  HttpBounds::Calendar utc{};
  if(!HttpBounds::calendar(seconds,utc))return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
  if(compare(crt->valid_from,utc)>0)*flags|=MBEDTLS_X509_BADCERT_FUTURE;
  if(compare(crt->valid_to,utc)<0)*flags|=MBEDTLS_X509_BADCERT_EXPIRED;
  return result;
}
inline esp_err_t attachBundle(void* raw){
  auto* s=session;auto* conf=static_cast<mbedtls_ssl_config*>(raw);
  if(!s || !conf || esp_crt_bundle_attach(raw)!=ESP_OK)return ESP_FAIL;
  s->verify=conf->f_vrfy;s->verifyContext=conf->p_vrfy;
  if(!s->verify)return ESP_FAIL;
  mbedtls_ssl_conf_verify(conf,verifyCertificate,s);
  mbedtls_ssl_conf_min_version(conf,MBEDTLS_SSL_MAJOR_VERSION_3,MBEDTLS_SSL_MINOR_VERSION_3);
  return ESP_OK;
}
inline bool prepare(Session& s){
  if(!HttpBounds::url(s.url))return false;
  const char* first=s.url+8;const char* path=first;
  while(*path && *path!='/' && *path!='?')++path;
  const char* end=path;const char* colon=static_cast<const char*>(std::memchr(first,':',end-first));
  if(colon)end=colon;
  if(end==first || static_cast<size_t>(end-first)>=sizeof(s.host))return false;
  std::memcpy(s.host,first,end-first);s.host[end-first]=0;
  const char* prefix=*path=='?'?"/":"";if(!*path)path="/";
  const int n=std::snprintf(s.request,sizeof(s.request),
    "GET %s%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: RiscRTE/1\r\nAccept: */*\r\nAccept-Encoding: identity\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",prefix,path,s.host);
  if(n<=0 || static_cast<size_t>(n)>=sizeof(s.request))return false;
  s.requestBytes=n;s.sent=0;s.phase=Phase::Connect;
  s.config={};s.config.non_block=true;s.config.timeout_ms=1;
  s.config.skip_common_name=false;s.config.crt_bundle_attach=attachBundle;
  s.tls=esp_tls_init();return s.tls!=nullptr;
}
inline bool destroy(Session& s){
  if(!s.tls)return !s.retained;
  // IDF destroy frees its object even on socket-close failure. Never retry the
  // freed pointer; retain a poison marker and reject sleep/reuse until reboot.
  const int result=esp_tls_conn_destroy(s.tls);s.tls=nullptr;
  if(result!=0)s.retained=true;
  return !s.retained;
}
inline int32_t close(void*,uint64_t token){
  if(!owner || !owner() || !token)return RISC_HTTP_INVALID;
  if(!session)return token==lastClosed?RISC_HTTP_OK:RISC_HTTP_CLOSED;
  if(token!=session->token)return RISC_HTTP_CLOSED;
  if(!destroy(*session))return RISC_HTTP_RETAINED;
  lastClosed=token;session->~Session();heap_caps_free(session);session=nullptr;return RISC_HTTP_OK;
}
inline int32_t open(void*,const risc_http_request_v1* r,uint64_t* out){
  if(out)*out=0;
  if(!out || !owner || !owner() || !HttpBounds::request(r))return RISC_HTTP_INVALID;
  if(session)return RISC_HTTP_BUSY;
  if(!networkReady || !networkReady())return RISC_HTTP_NETWORK;
  if(generation==UINT64_MAX)return RISC_HTTP_CLOSED;
  // TLS buffers remain internal in the pinned SDK. Reserve a conservative
  // minimum before starting; all bulk metadata is PSRAM-only, no fallback.
  if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<80u*1024u ||
     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<36u*1024u)return RISC_HTTP_MEMORY;
  void* memory=heap_caps_malloc(sizeof(Session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!memory)return RISC_HTTP_MEMORY;
  session=new(memory)Session(++generation,*r);*out=session->token;
  if(!prepare(*session)){const int32_t c=close(nullptr,*out);if(c==RISC_HTTP_OK)*out=0;return c==RISC_HTTP_OK?RISC_HTTP_MEMORY:c;}
  return RISC_HTTP_OK;
}
inline int32_t info(void*,uint64_t token,risc_http_response_v1* out){
  if(!out || out->struct_size<sizeof(*out) || !owner || !owner())return RISC_HTTP_INVALID;
  const uint32_t size=out->struct_size;*out={};out->struct_size=size;out->content_length=-1;
  if(!session || token!=session->token)return RISC_HTTP_CLOSED;
  out->status_code=session->decoder.status;out->content_length=session->decoder.length;out->received_bytes=session->decoder.received;
  return session->retained?RISC_HTTP_RETAINED:session->error;
}
inline bool again(ssize_t code){return code==ESP_TLS_ERR_SSL_WANT_READ || code==ESP_TLS_ERR_SSL_WANT_WRITE;}
inline int32_t read(void*,uint64_t token,void* out,uint32_t capacity,uint32_t* count){
  if(count)*count=0;
  if(!count || !out || !capacity || capacity>RISC_HTTP_CHUNK_MAX || !owner || !owner())return RISC_HTTP_INVALID;
  auto* s=session;if(!s || token!=s->token)return RISC_HTTP_CLOSED;
  if(s->retained)return RISC_HTTP_RETAINED;
  if(s->error)return s->error;
  if(!s->budget.alive(now()))return s->error=RISC_HTTP_TIMEOUT;
  if(!networkReady || !networkReady())return s->error=RISC_HTTP_NETWORK;
  if(s->phase==Phase::Connect){
    const int code=esp_tls_conn_new_async(s->host,std::strlen(s->host),443,&s->config,s->tls);
    if(!s->budget.alive(now()))return s->error=RISC_HTTP_TIMEOUT;
    if(code<0)return s->error=RISC_HTTP_TRANSPORT;
    if(code==1){s->phase=Phase::Send;s->budget.connected(now());}
    return RISC_HTTP_AGAIN;
  }
  if(s->phase==Phase::Send){
    const uint32_t left=s->requestBytes-s->sent;const uint32_t n=left>512?512:left;
    const ssize_t sent=esp_tls_conn_write(s->tls,s->request+s->sent,n);
    if(again(sent))return RISC_HTTP_AGAIN;
    if(sent<0 || static_cast<uint32_t>(sent)>n)return s->error=RISC_HTTP_TRANSPORT;
    s->sent+=sent;if(s->sent==s->requestBytes)s->phase=Phase::Receive;
    return RISC_HTTP_AGAIN;
  }
  if(s->decoder.complete)return RISC_HTTP_EOF;
  uint8_t bytes[RISC_HTTP_CHUNK_MAX];const ssize_t n=esp_tls_conn_read(s->tls,bytes,capacity);
  if(!s->budget.alive(now()))return s->error=RISC_HTTP_TIMEOUT;
  if(again(n))return RISC_HTTP_AGAIN;
  if(n<0 || n>capacity)return s->error=RISC_HTTP_TRANSPORT;
  if(s->wire>s->decoder.maximum+65536u || static_cast<uint32_t>(n)>s->decoder.maximum+65536u-s->wire)return s->error=RISC_HTTP_SIZE;
  s->wire+=n;
  if(!s->decoder.feed(bytes,n,out,capacity))return s->error=s->decoder.error;
  if(s->decoder.redirect){
    if(++s->redirects>3)return s->error=RISC_HTTP_STATUS;
    std::strcpy(s->url,s->decoder.location);
    if(!destroy(*s))return RISC_HTTP_RETAINED;
    const uint32_t maximum=s->decoder.maximum;s->decoder=HttpResponse::Decoder(maximum);s->decoder.parser.data=&s->decoder;
    if(!prepare(*s))return s->error=RISC_HTTP_MEMORY;
    return RISC_HTTP_AGAIN;
  }
  *count=s->decoder.copied;
  if(!s->budget.accept(now(),*count)){*count=0;return s->error=RISC_HTTP_SIZE;}
  if(*count)return RISC_HTTP_OK;
  if(s->decoder.complete)return RISC_HTTP_EOF;
  if(n==0)return s->error=RISC_HTTP_TRANSPORT;
  return RISC_HTTP_AGAIN;
}
inline const risc_http_client_v1* api(){static const risc_http_client_v1 value={1,sizeof(value),nullptr,open,read,info,close};return &value;}
} }
