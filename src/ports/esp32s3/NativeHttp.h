#pragma once
/* Generic nonblocking TLS/HTTP transport for pinned IDF4.4 and IDF6.1.
 * A single native-owned session has bounded PSRAM metadata and no app
 * pointers/tasks/callbacks.
 * Native IDF6 DNS uses a copied worker mailbox; TLS/read/write stay on the
 * owner task. Each caller step yields through the runtime. */
#include "HttpResponse.h"
#include <RiscHttpClientV2.h>
#include "NativeTlsMemory.h"
#include "diagnostics/StageLog.h"
#include "HttpTimeSample.h"
#include <esp_tls.h>
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 6
// Pinned SDK layout is required for checked socket detachment: the public
// setter rejects -1 and mbedtls_net_free discards the underlying close error.
#include <esp_tls_private.h>
#endif
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/asn1.h>
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 6
#include <psa/crypto.h>
#else
#include <mbedtls/bignum.h>
#include <mbedtls/cipher.h>
#include <mbedtls/dhm.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>
#include <mbedtls/pem.h>
#include <mbedtls/pk.h>
#endif
#include <new>
#include <cstdio>
#include <initializer_list>
#include <unistd.h>
namespace RiscCpu { namespace NativeHttp {
inline uint64_t now(){return static_cast<uint64_t>(esp_timer_get_time()/1000);}
using Verify=int(*)(void*,mbedtls_x509_crt*,int,uint32_t*);
enum class Phase { SampleTime,Connect,Send,Receive };
struct Session {
  uint64_t token,started,utc,utcAt;HttpBounds::Budget budget;HttpResponse::Decoder decoder;
  HttpTimeSample::Sample time;
  esp_tls_t* tls=nullptr;esp_tls_cfg_t config{};Verify verify=nullptr;void* verifyContext=nullptr;
  Phase phase=Phase::Connect;int32_t error=0;bool retained=false;unsigned redirects=0;
  char url[RISC_HTTP_URL_MAX+1]{},host[254]{},request[RISC_HTTP_URL_MAX+384]{};
  uint32_t requestBytes=0,sent=0,wire=0;
  char* requestV2=nullptr;uint32_t requestV2Bytes=0;
  uint8_t pending[512]{};uint32_t pendingBytes=0,pendingAt=0;
  int32_t terminal=RISC_HTTP_AGAIN;
#if RISC_NATIVE_ESP_IDF
  char address[INET_ADDRSTRLEN]{};uint64_t closingAt=0;
#endif
  ~Session(){if(requestV2){volatile char* p=requestV2;for(uint32_t i=0;i<requestV2Bytes;++i)p[i]=0;heap_caps_free(requestV2);}}
  Session(uint64_t id,const risc_http_request_v1& r):token(id),started(now()),utc(r.utc_seconds),utcAt(started),
    budget(started,r.timeout_ms,r.max_bytes),decoder(r.max_bytes){std::strcpy(url,r.url);}
};
static Session* session=nullptr;
static uint64_t generation=0,lastClosed=0;
static bool (*owner)()=nullptr;static bool (*networkReady)()=nullptr;
inline bool idle(){return session==nullptr;}
inline bool safe(){return !session || !session->retained;}
inline void configure(bool(*o)(),bool(*ready)()){owner=o;networkReady=ready;}
inline void memoryStage(const char* stage){
  (void)stage;
  RISC_STAGE_LOG("http memory stage=%s tls_psram=%u internal_free=%lu internal_largest=%lu psram_free=%lu psram_largest=%lu dma_largest=%lu",stage,
    unsigned(NativeTlsMemory::ready()),
    static_cast<unsigned long>(heap_caps_get_free_size(NativeTlsMemory::InternalCaps)),
    static_cast<unsigned long>(heap_caps_get_largest_free_block(NativeTlsMemory::InternalCaps)),
    static_cast<unsigned long>(heap_caps_get_free_size(NativeTlsMemory::ExternalCaps)),
    static_cast<unsigned long>(heap_caps_get_largest_free_block(NativeTlsMemory::ExternalCaps)),
    static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)));
}
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
  const uint64_t seconds=s->utc+(now()-s->utcAt)/1000u;
  HttpBounds::Calendar utc{};
  if(!HttpBounds::calendar(seconds,utc))return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
  if(compare(crt->valid_from,utc)>0)*flags|=MBEDTLS_X509_BADCERT_FUTURE;
  if(compare(crt->valid_to,utc)<0)*flags|=MBEDTLS_X509_BADCERT_EXPIRED;
  return result;
}
inline esp_err_t attachBundle(void* raw){
  auto* s=session;auto* conf=static_cast<mbedtls_ssl_config*>(raw);
  if(!s || !conf)return ESP_FAIL;
  const esp_err_t attached=esp_crt_bundle_attach(raw);
  // Pinned IDF ignores the bundle callback's return value. Record its failure
  // ourselves so a failed allocation cannot proceed without our verifier.
  if(attached!=ESP_OK){s->error=attached==ESP_ERR_NO_MEM?RISC_HTTP_MEMORY:RISC_HTTP_TRANSPORT;return attached;}
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 6
  s->verify=conf->MBEDTLS_PRIVATE(f_vrfy);s->verifyContext=conf->MBEDTLS_PRIVATE(p_vrfy);
#else
  s->verify=conf->f_vrfy;s->verifyContext=conf->p_vrfy;
#endif
  if(!s->verify){s->error=RISC_HTTP_TRANSPORT;return ESP_FAIL;}
  mbedtls_ssl_conf_verify(conf,verifyCertificate,s);
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 6
  mbedtls_ssl_conf_min_tls_version(conf,MBEDTLS_SSL_VERSION_TLS1_2);
#else
  mbedtls_ssl_conf_min_version(conf,MBEDTLS_SSL_MAJOR_VERSION_3,MBEDTLS_SSL_MINOR_VERSION_3);
#endif
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
  s.requestBytes=s.requestV2?s.requestV2Bytes:static_cast<uint32_t>(n);s.sent=0;s.phase=Phase::Connect;
  s.config={};s.config.non_block=true;s.config.timeout_ms=1;
  s.config.skip_common_name=false;s.config.crt_bundle_attach=attachBundle;
#if RISC_NATIVE_ESP_IDF
  s.address[0]=0;s.config.common_name=s.host;
#endif
  NativeTlsMemory::DmaHeadroom dma;
  if(!dma)return false;
  s.tls=esp_tls_init();return s.tls!=nullptr;
}
inline bool destroy(Session& s){
  if(!s.time.close())s.retained=true;
  if(!s.tls)return !s.retained;
  auto* tls=s.tls;
  const int fd=tls->sockfd,wrapped=tls->server_fd.fd;
  // IDF initializes both to -1; before TLS setup wrapped is -1, after it
  // must name the same sole owned socket. Do not close ambiguous handles.
  if(fd < -1 || wrapped < -1 || (wrapped>=0 && wrapped!=fd))s.retained=true;
  else if(fd>=0 && ::close(fd)!=0)s.retained=true;
  // SDK normally discards mbedtls_net_free's close result. Detach before
  // SDK destruction so neither a failed close nor a freed fd is retried.
  tls->sockfd=-1;tls->server_fd.fd=-1;
  const int result=esp_tls_conn_destroy(tls);s.tls=nullptr;
  if(result!=0)s.retained=true;
  return !s.retained;
}
inline int32_t close(void*,uint64_t token){
  if(!owner || !owner() || !token)return RISC_HTTP_INVALID;
  if(!session)return token==lastClosed?RISC_HTTP_OK:RISC_HTTP_CLOSED;
  if(token!=session->token)return RISC_HTTP_CLOSED;
  if(session->retained)return RISC_HTTP_RETAINED;
#if RISC_NATIVE_ESP_IDF
  if(NativeDns::pending()){
    if(!session->closingAt)session->closingAt=now();
    if(now()-session->closingAt<30000)return RISC_HTTP_AGAIN;
    session->retained=true;return RISC_HTTP_RETAINED;
  }
  NativeDns::discard();
#endif
  if(!destroy(*session))return RISC_HTTP_RETAINED;
  lastClosed=token;session->~Session();heap_caps_free(session);session=nullptr;return RISC_HTTP_OK;
}
inline int32_t openImpl(const risc_http_request_v1* r,const char* server,uint64_t* out){
  if(out)*out=0;
  if(!out || !owner || !owner() || !r || r->struct_size<sizeof(*r))return RISC_HTTP_INVALID;
  risc_http_request_v1 checked=*r;
  const bool sample=server!=nullptr;
  if(sample){if(r->utc_seconds || !HttpTimeSample::hostname(server))return RISC_HTTP_INVALID;checked.utc_seconds=HttpBounds::FirstUtc;}
  if(!HttpBounds::request(&checked))return RISC_HTTP_INVALID;
  if(session)return RISC_HTTP_BUSY;
  if(!networkReady || !networkReady())return RISC_HTTP_NETWORK;
  if(generation==UINT64_MAX)return RISC_HTTP_CLOSED;
  // Software TLS allocations use PSRAM. prepare() probes the actual separate
  // internal/DMA blocks while creating the SDK handle, rather than demanding
  // 80 KiB free / a 36 KiB internal block for records now allocated externally.
  memoryStage("open");
  if(!NativeTlsMemory::ready())return RISC_HTTP_MEMORY;
  void* memory=heap_caps_malloc(sizeof(Session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!memory){memoryStage("session-allocation-failed");return RISC_HTTP_MEMORY;}
  session=new(memory)Session(++generation,*r);*out=session->token;
  if(sample){session->time.begin(server,session->started);session->phase=Phase::SampleTime;return RISC_HTTP_OK;}
  if(!prepare(*session)){memoryStage("prepare-allocation-failed");const int32_t c=close(nullptr,*out);if(c==RISC_HTTP_OK)*out=0;return c==RISC_HTTP_OK?RISC_HTTP_MEMORY:c;}
  return RISC_HTTP_OK;
}
inline int32_t open(void*,const risc_http_request_v1* r,uint64_t* out){return openImpl(r,nullptr,out);}
inline int32_t openWithTime(void*,const risc_http_request_v1* r,const char* server,uint64_t* out){
  if(!server){if(out)*out=0;return RISC_HTTP_INVALID;}
  return openImpl(r,server,out);
}
inline int32_t info(void*,uint64_t token,risc_http_response_v1* out){
  if(!out || out->struct_size<sizeof(*out) || !owner || !owner())return RISC_HTTP_INVALID;
  const uint32_t size=out->struct_size;*out={};out->struct_size=size;out->content_length=-1;
  if(!session || token!=session->token)return RISC_HTTP_CLOSED;
  out->status_code=session->decoder.status;out->content_length=session->decoder.length;out->received_bytes=session->decoder.received;
  return session->retained?RISC_HTTP_RETAINED:session->error;
}
// IDF stores positive -ret, while read/write may return a negative raw value.
// Decode each selected mbedTLS generation using its own error representation.
inline bool allocationError(int code){
  const uint32_t magnitude=code<0?0u-static_cast<uint32_t>(code):static_cast<uint32_t>(code);
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 6
  // MbedTLS4/TF-PSA returns the PSA allocation status directly, including
  // SSL/X.509 allocation failures. It is not an IDF4 high/low error bitfield.
  return magnitude==uint32_t(-PSA_ERROR_INSUFFICIENT_MEMORY);
#else
  const uint32_t high=magnitude&0x7f80u,low=magnitude&0x7fu;
  return high==uint32_t(-MBEDTLS_ERR_SSL_ALLOC_FAILED) || high==uint32_t(-MBEDTLS_ERR_X509_ALLOC_FAILED) ||
    high==uint32_t(-MBEDTLS_ERR_PK_ALLOC_FAILED) || high==uint32_t(-MBEDTLS_ERR_PEM_ALLOC_FAILED) ||
    high==uint32_t(-MBEDTLS_ERR_MD_ALLOC_FAILED) || high==uint32_t(-MBEDTLS_ERR_CIPHER_ALLOC_FAILED) ||
    high==uint32_t(-MBEDTLS_ERR_ECP_ALLOC_FAILED) || high==uint32_t(-MBEDTLS_ERR_DHM_ALLOC_FAILED) ||
    low==uint32_t(-MBEDTLS_ERR_MPI_ALLOC_FAILED) || low==uint32_t(-MBEDTLS_ERR_ASN1_ALLOC_FAILED);
#endif
}
inline int32_t transportError(Session& s,int direct=0){
  int code=0,flags=0;
  const esp_err_t error=esp_tls_get_and_clear_last_error(s.tls->error_handle,&code,&flags);
  const bool memory=error==ESP_ERR_NO_MEM || allocationError(code) || allocationError(direct);
  RISC_STAGE_LOG("http failure esp=%d mbedtls=%d flags=%d memory=%u",int(error),code,flags,unsigned(memory));
  memoryStage(memory?"tls-allocation-failed":"tls-transport-failed");
  return memory?RISC_HTTP_MEMORY:RISC_HTTP_TRANSPORT;
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
  if(s->phase==Phase::SampleTime){
    const int32_t result=s->time.step(s->budget,s->utc);
    if(result==RISC_HTTP_RETAINED){s->retained=true;return result;}
    if(result<0)return s->error=result;
    if(result==RISC_HTTP_AGAIN)return result;
    s->utcAt=now();
    if(!prepare(*s))return s->error=RISC_HTTP_MEMORY;
    return RISC_HTTP_AGAIN;
  }
  if(s->phase==Phase::Connect){
#if RISC_NATIVE_ESP_IDF
    if(s->requestV2&&!s->address[0]){sockaddr_in resolved{};const int code=NativeDns::resolve(s->host,resolved);if(code==1)return RISC_HTTP_AGAIN;if(code<0||!inet_ntop(AF_INET,&resolved.sin_addr,s->address,sizeof(s->address)))return s->error=RISC_HTTP_NETWORK;}
#endif
    // Both pinned IDF4.4 and IDF6.1 reuse select()'s output fd_sets across asynchronous calls.
    // A timeout clears them. Re-arm the SDK-owned connecting socket before
    // polling again; otherwise ordinary Internet RTTs can stall until timeout.
    if(s->tls->conn_state==ESP_TLS_CONNECTING){
      if(s->tls->sockfd<0)return s->error=RISC_HTTP_TRANSPORT;
      FD_ZERO(&s->tls->rset);FD_SET(s->tls->sockfd,&s->tls->rset);
      s->tls->wset=s->tls->rset;
    }
#if RISC_NATIVE_ESP_IDF
    const int code=esp_tls_conn_new_async(s->requestV2?s->address:s->host,std::strlen(s->requestV2?s->address:s->host),443,&s->config,s->tls);
#else
    const int code=esp_tls_conn_new_async(s->host,std::strlen(s->host),443,&s->config,s->tls);
#endif
    if(!s->budget.alive(now()))return s->error=RISC_HTTP_TIMEOUT;
    if(s->error)return s->error;
    if(code<0)return s->error=transportError(*s);
    if(code==1){s->phase=Phase::Send;s->budget.connected(now());}
    return RISC_HTTP_AGAIN;
  }
  if(s->phase==Phase::Send){
    const uint32_t left=s->requestBytes-s->sent;const uint32_t n=left>512?512:left;
    const ssize_t sent=esp_tls_conn_write(s->tls,(s->requestV2?s->requestV2:s->request)+s->sent,n);
    if(again(sent))return RISC_HTTP_AGAIN;
    if(sent<0)return s->error=transportError(*s,int(sent));
    if(static_cast<uint32_t>(sent)>n)return s->error=RISC_HTTP_TRANSPORT;
    s->sent+=sent;if(s->sent==s->requestBytes)s->phase=Phase::Receive;
    return RISC_HTTP_AGAIN;
  }
  if(s->decoder.complete)return RISC_HTTP_EOF;
  uint8_t bytes[RISC_HTTP_CHUNK_MAX];const ssize_t n=esp_tls_conn_read(s->tls,bytes,capacity);
  if(!s->budget.alive(now()))return s->error=RISC_HTTP_TIMEOUT;
  if(again(n))return RISC_HTTP_AGAIN;
  if(n<0)return s->error=transportError(*s,int(n));
  if(static_cast<uint32_t>(n)>capacity)return s->error=RISC_HTTP_TRANSPORT;
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

inline bool headerName(const char* p){
  if(!p || !*p)return false;
  size_t n=0;for(;p[n]&&n<=64;++n){const unsigned char c=p[n];
    if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;}
  return n<=64;
}
inline bool sameHeader(const char* a,const char* b){
  while(*a&&*b){char x=*a++,y=*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}
  return *a==*b;
}
inline int32_t openV2(void*,const risc_http_request_v2* r,uint64_t* out){
  if(out)*out=0;
  if(!out||!owner||!owner()||!r||r->struct_size<sizeof(*r)||!HttpBounds::url(r->url)||
     (r->method!=RISC_HTTP_GET&&r->method!=RISC_HTTP_POST)||r->redirect_policy||
     r->header_count>8||(r->header_count&&!r->headers)||r->body_bytes>16384||
     (r->body_bytes&&!r->body)||(r->method==RISC_HTTP_GET&&r->body_bytes)||
     !r->max_response_bytes||r->max_response_bytes>32768||!r->timeout_ms||r->timeout_ms>30000||
     (r->time_server?(r->utc_seconds||!HttpTimeSample::hostname(r->time_server)):
        (r->utc_seconds<HttpBounds::FirstUtc||r->utc_seconds>HttpBounds::LastUtc)))return RISC_HTTP_INVALID;
  size_t headerBytes=0;
  for(uint32_t i=0;i<r->header_count;++i){const auto& h=r->headers[i];
    if(!headerName(h.name)||!h.value)return RISC_HTTP_INVALID;
    for(const char* reserved:{"host","content-length","transfer-encoding","connection","accept-encoding","expect"})
      if(sameHeader(h.name,reserved))return RISC_HTTP_INVALID;
    for(uint32_t j=0;j<i;++j)if(sameHeader(h.name,r->headers[j].name))return RISC_HTTP_INVALID;
    size_t len=0;for(;len<=2048&&h.value[len];++len){const unsigned char c=h.value[len];if(c<32||c==127)return RISC_HTTP_INVALID;}
    headerBytes+=std::strlen(h.name)+len+4;if(len>2048||headerBytes>2048)return RISC_HTTP_SIZE;
  }
  if(session)return RISC_HTTP_BUSY;
  if(!networkReady||!networkReady())return RISC_HTTP_NETWORK;
  if(generation==UINT64_MAX)return RISC_HTTP_CLOSED;
  if(!NativeTlsMemory::ready())return RISC_HTTP_MEMORY;
  const size_t cap=RISC_HTTP_URL_MAX+512+headerBytes+r->body_bytes;
  char* request=static_cast<char*>(heap_caps_malloc(cap,NativeTlsMemory::ExternalCaps));
  if(!request)return RISC_HTTP_MEMORY;
  const char* path=r->url+8;while(*path&&*path!='/'&&*path!='?')++path;
  const int hostBytes=static_cast<int>(path-(r->url+8));
  int used=std::snprintf(request,cap,"%s %s%s HTTP/1.1\r\nHost: %.*s\r\nUser-Agent: RiscRTE/2\r\nAccept-Encoding: identity\r\nConnection: close\r\nContent-Length: %lu\r\n",
    r->method==RISC_HTTP_POST?"POST":"GET",*path=='?'?"/":"",*path?path:"/",hostBytes,r->url+8,static_cast<unsigned long>(r->body_bytes));
  for(uint32_t i=0;used>0&&static_cast<size_t>(used)<cap&&i<r->header_count;++i)
    used+=std::snprintf(request+used,cap-used,"%s: %s\r\n",r->headers[i].name,r->headers[i].value);
  if(used<=0||static_cast<size_t>(used)+2+r->body_bytes>=cap){std::memset(request,0,cap);heap_caps_free(request);return RISC_HTTP_SIZE;}
  request[used++]='\r';request[used++]='\n';if(r->body_bytes)std::memcpy(request+used,r->body,r->body_bytes);used+=r->body_bytes;
  void* memory=heap_caps_malloc(sizeof(Session),NativeTlsMemory::ExternalCaps);
  if(!memory){std::memset(request,0,cap);heap_caps_free(request);return RISC_HTTP_MEMORY;}
  risc_http_request_v1 base{sizeof(base),r->url,r->max_response_bytes,r->timeout_ms,r->utc_seconds};
  session=new(memory)Session(++generation,base);*out=session->token;
  session->requestV2=request;session->requestV2Bytes=used;session->decoder.v2=true;
  if(r->time_server){session->time.asyncDns=true;session->time.begin(r->time_server,session->started);session->phase=Phase::SampleTime;return RISC_HTTP_OK;}
  if(!prepare(*session)){const auto c=close(nullptr,*out);if(c==RISC_HTTP_OK)*out=0;return c==RISC_HTTP_OK?RISC_HTTP_MEMORY:c;}
  return RISC_HTTP_OK;
}
inline int32_t advanceV2(void*,uint64_t token){
  if(!owner||!owner())return RISC_HTTP_INVALID;
  auto* s=session;if(!s||token!=s->token||!s->requestV2)return RISC_HTTP_CLOSED;
  if(s->retained)return RISC_HTTP_RETAINED;
  if(s->pendingBytes)return RISC_HTTP_OK;
  if(s->terminal!=RISC_HTTP_AGAIN)return s->terminal;
  uint32_t count=0;const auto result=read(nullptr,token,s->pending,sizeof(s->pending),&count);
  s->pendingBytes=count;s->pendingAt=0;
  if(result<0||result==RISC_HTTP_EOF)s->terminal=result;
  return result;
}
inline int32_t readV2(void*,uint64_t token,void* out,uint32_t capacity,uint32_t* count){
  if(count)*count=0;
  if(!owner||!owner()||!out||!count||!capacity||capacity>512)return RISC_HTTP_INVALID;
  auto* s=session;if(!s||token!=s->token||!s->requestV2)return RISC_HTTP_CLOSED;
  if(s->retained)return RISC_HTTP_RETAINED;
  const auto left=s->pendingBytes-s->pendingAt;const auto n=left<capacity?left:capacity;
  if(n){std::memcpy(out,s->pending+s->pendingAt,n);s->pendingAt+=n;*count=n;
    if(s->pendingAt==s->pendingBytes)s->pendingBytes=s->pendingAt=0;
    return RISC_HTTP_OK;}
  return s->terminal;
}
inline int32_t infoV2(void*,uint64_t token,risc_http_response_v2* out){
  if(!owner||!owner()||!out||out->struct_size<sizeof(*out))return RISC_HTTP_INVALID;
  const auto size=out->struct_size;*out={};out->struct_size=size;out->content_length=-1;
  auto* s=session;if(!s||s->token!=token||!s->requestV2)return RISC_HTTP_CLOSED;
  out->phase=s->terminal!=RISC_HTTP_AGAIN?static_cast<uint32_t>(RISC_HTTP_PHASE_TERMINAL):static_cast<uint32_t>(s->phase)+1;
  out->status_code=s->decoder.status;out->outcome=s->terminal;out->sent_bytes=s->sent;
  out->received_bytes=s->decoder.received;out->content_length=s->decoder.length;out->cleanup_retained=s->retained;
  out->utc_seconds=s->utc?s->utc+(now()-s->utcAt)/1000u:0;
  std::memcpy(out->etag,s->decoder.etag,sizeof(out->etag));std::memcpy(out->last_modified,s->decoder.lastModified,sizeof(out->last_modified));
  std::memcpy(out->content_type,s->decoder.contentType,sizeof(out->content_type));std::memcpy(out->retry_after,s->decoder.retryAfter,sizeof(out->retry_after));
  return s->retained?RISC_HTTP_RETAINED:RISC_HTTP_OK;
}
inline const risc_http_client_v2* apiV2(){
  static const risc_http_client_v2 value={2,sizeof(value),nullptr,openV2,advanceV2,readV2,infoV2,close};return &value;
}
inline const risc_http_client_v1* api(){
  static const risc_http_client_time_v1 value={{1,sizeof(value),nullptr,open,read,info,close},
    RISC_HTTP_TIME_TAG_V1,RISC_HTTP_TIME_VERSION_V1,openWithTime};return &value.base;
}
} }
