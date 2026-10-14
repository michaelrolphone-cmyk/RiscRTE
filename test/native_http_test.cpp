#include "ports/esp32s3/NativeHttp.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <cstdarg>
using namespace RiscCpu;
static uint64_t clockMs=0;
static bool owned=true,online=true,oom=false,lowHeap=false,closeFail=false,tlsFail=false,slowConnect=false,readWait=false;
static unsigned pendingConnect=0,connectPolls=0,socketCloses=0;static bool invalidSocket=false,socketCloseFail=false;
static int live=0,opens=0,closes=0,allocations=0;
static size_t chunk=512;
static std::vector<std::string> responses;
static size_t inputAt=0;
static std::string sent;
static mbedtls_ssl_config config;
static uint32_t chainFlags=0;
struct Datagram { std::vector<uint8_t> bytes; sockaddr_in source{}; socklen_t length=sizeof(sockaddr_in); };
static std::deque<Datagram> datagrams;
static std::array<uint8_t,48> query{};
static sockaddr_in timePeer{};
static uint32_t dnsDelay=0,udpDelay=0;
static int dnsResult=0,sendError=0,receiveError=0;
static bool socketFail=false,fcntlFail=false,udpCloseFail=false,shortSend=false;
static unsigned dnsCalls=0,dnsFrees=0,udpOpens=0,udpCloses=0,udpSends=0,udpReceives=0,randomCalls=0;
static bool udpLive=false;
static std::string resolvedName;
void esp_fill_random(void* out,size_t n){assert(online && n==8);++randomCalls;for(size_t i=0;i<n;++i)static_cast<uint8_t*>(out)[i]=uint8_t(randomCalls+i+1);}
extern "C" int __wrap_getaddrinfo(const char* name,const char* service,const addrinfo* hints,addrinfo** out){
 assert(name && std::string(service)=="123" && hints->ai_family==AF_INET && hints->ai_socktype==SOCK_DGRAM && hints->ai_protocol==IPPROTO_UDP);
 resolvedName=name;++dnsCalls;clockMs+=dnsDelay;
 static addrinfo result;result={};timePeer={};timePeer.sin_family=AF_INET;timePeer.sin_port=htons(123);timePeer.sin_addr.s_addr=htonl(0xc0000201);
 result.ai_family=AF_INET;result.ai_addr=reinterpret_cast<sockaddr*>(&timePeer);result.ai_addrlen=sizeof(timePeer);
 *out=dnsResult?nullptr:&result;return dnsResult;
}
extern "C" void __wrap_freeaddrinfo(addrinfo* p){assert(p);++dnsFrees;}
extern "C" int __wrap_socket(int family,int type,int protocol){
 assert(family==AF_INET && type==SOCK_DGRAM && protocol==IPPROTO_UDP && !udpLive);++udpOpens;
 if(socketFail){errno=EMFILE;return -1;}udpLive=true;return 9;
}
extern "C" int __wrap_fcntl(int fd,int command,...){
 va_list args;va_start(args,command);const int value=va_arg(args,int);va_end(args);
 assert(fd==9 && udpLive && command==F_SETFL && value==O_NONBLOCK);return fcntlFail?-1:0;
}
extern "C" ssize_t __wrap_sendto(int fd,const void* bytes,size_t size,int flags,const sockaddr* to,socklen_t length){
 assert(fd==9 && udpLive && size==48 && !flags && length==sizeof(timePeer) && !std::memcmp(to,&timePeer,length));
 ++udpSends;clockMs+=udpDelay;
 if(sendError){errno=sendError;return -1;}std::memcpy(query.data(),bytes,size);assert(query[0]==0x23);return shortSend?47:48;
}
extern "C" ssize_t __wrap_recvfrom(int fd,void* bytes,size_t capacity,int flags,sockaddr* source,socklen_t* length){
 assert(fd==9 && udpLive && capacity==49 && !flags);++udpReceives;clockMs+=udpDelay;
 if(receiveError){errno=receiveError;return -1;}
 if(datagrams.empty()){errno=EAGAIN;return -1;}
 const auto packet=datagrams.front();datagrams.pop_front();
 std::memcpy(source,&packet.source,std::min<size_t>(*length,sizeof(packet.source)));*length=packet.length;
 const size_t n=std::min(capacity,packet.bytes.size());std::memcpy(bytes,packet.bytes.data(),n);return n;
}
static int chain(void*,mbedtls_x509_crt*,int,uint32_t* flags){*flags|=chainFlags;return 0;}
int64_t esp_timer_get_time(){return clockMs*1000;}
size_t heap_caps_get_free_size(unsigned){return lowHeap?79*1024:128*1024;}
size_t heap_caps_get_largest_free_block(unsigned){return 48*1024;}
void* heap_caps_malloc(size_t n,unsigned caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(oom)return nullptr;++allocations;return std::malloc(n);}
void heap_caps_free(void* p){--allocations;std::free(p);}
esp_tls_t* esp_tls_init(){assert(!udpLive);if(tlsFail)return nullptr;++live;return new esp_tls_t{static_cast<unsigned>(opens++)};}
extern "C" int __wrap_close(int fd){
 if(fd==9){assert(udpLive);++udpCloses;if(udpCloseFail)return -1;udpLive=false;return 0;}
 assert(fd==7);++socketCloses;return socketCloseFail?-1:0;
}
int esp_tls_conn_destroy(esp_tls_t* p){assert(p->sockfd==-1&&p->server_fd.fd==-1);--live;++closes;delete p;return closeFail?-1:0;}
esp_err_t esp_crt_bundle_attach(void* p){static_cast<mbedtls_ssl_config*>(p)->f_vrfy=chain;return ESP_OK;}
int esp_tls_conn_new_async(const char* host,int n,int port,const esp_tls_cfg_t* cfg,esp_tls_t* tls){
 assert(n>0 && host[n]==0 && port==443 && cfg->non_block && !cfg->skip_common_name && cfg->timeout_ms>0);
 if(tls->conn_state==ESP_TLS_INIT)tls->sockfd=7;
 if(slowConnect)return 0;
 if(pendingConnect){
  ++connectPolls;
  if(tls->conn_state==ESP_TLS_CONNECTING){
   assert(FD_ISSET(tls->sockfd,&tls->rset)&&FD_ISSET(tls->sockfd,&tls->wset));
  }
  --pendingConnect;tls->conn_state=ESP_TLS_CONNECTING;
  FD_ZERO(&tls->rset);FD_ZERO(&tls->wset);if(invalidSocket)tls->sockfd=-1;
  return 0;
 }
 if(tls->conn_state==ESP_TLS_CONNECTING){
  assert(FD_ISSET(tls->sockfd,&tls->rset)&&FD_ISSET(tls->sockfd,&tls->wset));
 }
 tls->conn_state=ESP_TLS_HANDSHAKE;tls->server_fd.fd=tls->sockfd;
 config={};assert(cfg->crt_bundle_attach(&config)==0);assert(config.major==3&&config.minor==3);return 1;
}
ssize_t esp_tls_conn_write(esp_tls_t*,const void* p,size_t n){size_t k=std::min(n,chunk);sent.append(static_cast<const char*>(p),k);return k;}
ssize_t esp_tls_conn_read(esp_tls_t* tls,void* out,size_t n){
 if(readWait)return ESP_TLS_ERR_SSL_WANT_READ;
 assert(tls->index<responses.size());const auto& s=responses[tls->index];
 size_t k=std::min({n,chunk,s.size()-inputAt});std::memcpy(out,s.data()+inputAt,k);inputAt+=k;return k;
}
static bool owner(){return owned;}static bool ready(){return online;}
static void reset(std::vector<std::string> response={"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"}){
 assert(NativeHttp::idle()&&live==0&&allocations==0&&!udpLive);
 clockMs=0;owned=online=true;oom=lowHeap=closeFail=tlsFail=slowConnect=readWait=false;chainFlags=0;
 pendingConnect=connectPolls=socketCloses=0;invalidSocket=socketCloseFail=false;
 dnsDelay=udpDelay=0;dnsResult=sendError=receiveError=0;
 socketFail=fcntlFail=udpCloseFail=shortSend=false;
 dnsCalls=dnsFrees=udpOpens=udpCloses=udpSends=udpReceives=randomCalls=0;resolvedName.clear();datagrams.clear();query={};
 responses=std::move(response);opens=closes=0;inputAt=0;sent.clear();chunk=512;NativeHttp::configure(owner,ready);
}
static uint64_t open(uint32_t maximum=1024,uint32_t ms=30000){
 const risc_http_request_v1 r={sizeof(r),"https://example.test/path?q=1",maximum,ms,1780000000};uint64_t token=0;
 assert(NativeHttp::api()->open(nullptr,&r,&token)==RISC_HTTP_OK&&token);return token;
}
static int run(uint64_t token,std::string& body){
 char bytes[512];uint32_t n=0;int result=0;
 for(unsigned i=0;i<100000;++i){
  const int before=opens;result=NativeHttp::api()->read(nullptr,token,bytes,sizeof(bytes),&n);
  if(opens!=before)inputAt=0;
  if(result==RISC_HTTP_OK){assert(n);body.append(bytes,n);}else assert(n==0);
  if(result!=RISC_HTTP_OK&&result!=RISC_HTTP_AGAIN)return result;
  clockMs+=1;
 }
 assert(false);return result;
}
static void close(uint64_t t){assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_OK);assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_OK);assert(NativeHttp::idle());}

static int step(uint64_t token){char bytes[512];uint32_t n=99;const int result=NativeHttp::api()->read(nullptr,token,bytes,sizeof(bytes),&n);assert(n==0);return result;}
static uint64_t sample(uint32_t timeout=30000,const char* server="time.cloudflare.com"){
 const risc_http_request_v1 r{sizeof(r),"https://example.test/path",1024,timeout,0};uint64_t token=77;
 const auto* api=risc_http_client_time_from_v1(NativeHttp::api());assert(api);
 assert(api->open_with_time(nullptr,&r,server,&token)==RISC_HTTP_OK && token);
 assert(opens==0 && dnsCalls==0 && !NativeHttp::idle() && NativeHttp::safe());
 risc_http_response_v1 info{sizeof(info)};assert(api->base.info(nullptr,token,&info)==0 && !info.status_code && info.content_length==-1 && !info.received_bytes);
 return token;
}
static void sentSample(uint64_t token){assert(step(token)==RISC_HTTP_AGAIN && dnsCalls==1 && dnsFrees==1 && udpLive);assert(step(token)==RISC_HTTP_AGAIN && udpSends==1 && opens==0);}
static void stamp(std::vector<uint8_t>& packet,size_t offset,uint64_t utc,uint32_t fraction=0x40000000){
 uint64_t ntp=utc+HttpTimeSample::NtpOffset;for(unsigned i=0;i<4;++i){packet[offset+3-i]=uint8_t(ntp>>(i*8));packet[offset+7-i]=uint8_t(fraction>>(i*8));}
}
static Datagram response(uint64_t utc=1780000000){
 Datagram reply;reply.bytes.resize(48);reply.source=timePeer;reply.bytes[0]=0x24;reply.bytes[1]=2;
 std::memcpy(reply.bytes.data()+24,query.data()+40,8);stamp(reply.bytes,32,utc);stamp(reply.bytes,40,utc);return reply;
}
static void discardRetained(){
 assert(!NativeHttp::idle() && !NativeHttp::safe());
 NativeHttp::session->~Session();heap_caps_free(NativeHttp::session);NativeHttp::session=nullptr;
 // Test-process-only release of a modeled descriptor; production stays fenced.
 udpLive=false;
}
static unsigned timeTests(){
 unsigned cases=0;
 for(uint64_t utc:{UINT64_C(0),HttpBounds::FirstUtc-1,HttpBounds::LastUtc+1,UINT64_MAX}){
  reset();risc_http_request_v1 r{sizeof(r),"https://example.test/",1024,30000,utc};uint64_t t=88;
  assert(NativeHttp::api()->open(nullptr,&r,&t)==RISC_HTTP_INVALID && !t && NativeHttp::idle() && !dnsCalls);++cases;
 }
 for(uint64_t utc:{HttpBounds::FirstUtc,HttpBounds::LastUtc}){
  reset();risc_http_request_v1 r{sizeof(r),"https://example.test/",1024,30000,utc};uint64_t t=88;
  assert(NativeHttp::api()->open(nullptr,&r,&t)==0 && t && opens==1 && !dnsCalls);close(t);++cases;
 }
 const char* bad[]={nullptr,"","https://time.cloudflare.com","server:123","a..b",".a","a.","-a","a-","a.-b","a-.b","a/b","a b","a\\b","a\nb","a@b","[::1]"};
 for(auto name:bad){
  reset();risc_http_request_v1 r{sizeof(r),"https://example.test/",1024,30000,0};uint64_t t=88;
  assert(risc_http_client_time_from_v1(NativeHttp::api())->open_with_time(nullptr,&r,name,&t)==RISC_HTTP_INVALID && !t && NativeHttp::idle() && !dnsCalls);++cases;
 }
 for(const std::string& name:{std::string(64,'a'),std::string(63,'a')+"."+std::string(63,'b')+"."+std::string(63,'c')+"."+std::string(62,'d')}){
  reset();risc_http_request_v1 r{sizeof(r),"https://example.test/",1024,30000,0};uint64_t t=88;
  assert(risc_http_client_time_from_v1(NativeHttp::api())->open_with_time(nullptr,&r,name.c_str(),&t)==RISC_HTTP_INVALID && !t);++cases;
 }
 reset();{
  std::string maxHost=std::string(63,'a')+"."+std::string(63,'b')+"."+std::string(63,'c')+"."+std::string(61,'d');assert(maxHost.size()==253);
  auto t=sample(30000,maxHost.c_str());assert(step(t)==RISC_HTTP_AGAIN && resolvedName==maxHost);close(t);++cases;
 }
 reset();{
  risc_http_request_v1 r{sizeof(r),"https://example.test/",1024,30000,HttpBounds::FirstUtc};uint64_t t=88;
  assert(risc_http_client_time_from_v1(NativeHttp::api())->open_with_time(nullptr,&r,"time.cloudflare.com",&t)==RISC_HTTP_INVALID && !t && NativeHttp::idle());++cases;
 }
 reset();{
  char server[]="time.cloudflare.com",url[]="https://example.test/path";
  risc_http_request_v1 r{sizeof(r),url,1024,30000,0};uint64_t t=0;
  assert(risc_http_client_time_from_v1(NativeHttp::api())->open_with_time(nullptr,&r,server,&t)==0);
  server[0]='x';url[8]='x';r={};sentSample(t);
  assert(resolvedName=="time.cloudflare.com" && std::string(NativeHttp::session->url)=="https://example.test/path");
  datagrams.push_back(response());clockMs=4000;assert(step(t)==RISC_HTTP_AGAIN && opens==1 && udpCloses==1 && !udpLive);
  assert(NativeHttp::session->utc==1780000000 && NativeHttp::session->utcAt==4000 && NativeHttp::session->started==0);
  std::string body;assert(run(t,body)==RISC_HTTP_EOF && body=="hello");
  mbedtls_x509_crt cert{{2025,1,1,0,0,0},{2027,1,1,0,0,0}};uint32_t flags=0;
  assert(config.f_vrfy(config.p_vrfy,&cert,0,&flags)==0 && !flags);
  cert.valid_from.year=2027;chainFlags=8;assert(config.f_vrfy(config.p_vrfy,&cert,0,&flags)==0 && (flags&8) && (flags&MBEDTLS_X509_BADCERT_FUTURE));
  cert.valid_from.year=2024;cert.valid_to.year=2025;flags=0;assert(config.f_vrfy(config.p_vrfy,&cert,0,&flags)==0 && (flags&MBEDTLS_X509_BADCERT_EXPIRED));
  close(t);++cases;
 }
 for(uint64_t utc:{HttpBounds::FirstUtc,UINT64_C(2085978495),UINT64_C(2085978496),UINT64_C(2085978497),HttpBounds::LastUtc}){
  reset();auto t=sample();sentSample(t);auto reply=response(utc);if(utc==2085978496)stamp(reply.bytes,32,utc-1);
  datagrams.push_back(reply);assert(step(t)==RISC_HTTP_AGAIN && NativeHttp::session->utc==utc && !udpLive && opens==1);close(t);++cases;
 }
 for(unsigned phase=0;phase<3;++phase){
  reset();auto t=sample();for(unsigned i=0;i<phase;++i)assert(step(t)==RISC_HTTP_AGAIN);
  risc_http_response_v1 info{sizeof(info)};assert(NativeHttp::api()->info(nullptr,t,&info)==0);
  close(t);assert(opens==0 && udpCloses==(phase?1u:0u));++cases;
 }
 // Sampling failures never start TLS. Every ordinary error remains closeable.
 for(unsigned which=0;which<10;++which){
  reset();auto t=sample(which==0?5:30000);int expected=RISC_HTTP_TRANSPORT;
  if(which==0){dnsDelay=10;expected=RISC_HTTP_TIMEOUT;}
  if(which==1){dnsResult=EAI_NONAME;expected=RISC_HTTP_CLOCK;}
  if(which==2)socketFail=true;
  if(which==3)fcntlFail=true;
  if(which<4)assert(step(t)==expected);
  else{
   assert(step(t)==RISC_HTTP_AGAIN);
   if(which==4)sendError=EIO;
   if(which==5)shortSend=true;
   if(which==6){clockMs=5000;expected=RISC_HTTP_TIMEOUT;}
   if(which<7)assert(step(t)==expected);
   else{
    assert(step(t)==RISC_HTTP_AGAIN);
    if(which==7)receiveError=EIO;
    if(which==8){clockMs=5000;expected=RISC_HTTP_TIMEOUT;}
    if(which==9){udpDelay=5000;expected=RISC_HTTP_TIMEOUT;datagrams.push_back(response());}
    assert(step(t)==expected);
   }
  }
  assert(opens==0);risc_http_response_v1 info{sizeof(info)};assert(NativeHttp::api()->info(nullptr,t,&info)==expected);close(t);++cases;
 }
 reset();{auto t=sample(30);sentSample(t);clockMs=25;datagrams.push_back(response());assert(step(t)==RISC_HTTP_AGAIN);slowConnect=true;clockMs=30;assert(step(t)==RISC_HTTP_TIMEOUT);close(t);++cases;}
 reset();{auto t=sample(300000);sentSample(t);clockMs=5000;assert(step(t)==RISC_HTTP_TIMEOUT);close(t);++cases;}
 for(int error:{EAGAIN,EWOULDBLOCK,EINTR}){
  reset();auto t=sample();assert(step(t)==RISC_HTTP_AGAIN);sendError=error;assert(step(t)==RISC_HTTP_AGAIN);sendError=0;assert(step(t)==RISC_HTTP_AGAIN);
  receiveError=error;assert(step(t)==RISC_HTTP_AGAIN);receiveError=0;datagrams.push_back(response());assert(step(t)==RISC_HTTP_AGAIN && opens==1);close(t);++cases;
 }
 for(unsigned which=0;which<17;++which){
  reset();auto t=sample();sentSample(t);auto reply=response();
  if(which==0)reply.bytes.resize(47);
  if(which==1)reply.bytes.resize(49);
  if(which==2)reply.bytes[0]=0x23; // reflected client packet
  if(which==3)reply.bytes[0]=0x14; // old NTP version
  if(which==4)reply.bytes[0]=0x2c; // unknown NTP version
  if(which==5)reply.bytes[0]=0xe4; // unsynchronized leap state
  if(which==6)reply.bytes[1]=0; // kiss of death
  if(which==7)reply.bytes[1]=16;
  if(which==8)stamp(reply.bytes,40,HttpBounds::FirstUtc-1);
  if(which==9)stamp(reply.bytes,40,HttpBounds::LastUtc+1);
  if(which==10)stamp(reply.bytes,32,HttpBounds::FirstUtc-1);
  if(which==11)std::fill(reply.bytes.begin()+40,reply.bytes.end(),0);
  if(which==12)std::fill(reply.bytes.begin()+32,reply.bytes.begin()+40,0);
  if(which==13)stamp(reply.bytes,32,1780000001);
  if(which==14)stamp(reply.bytes,40,1780000006);
  if(which==15)stamp(reply.bytes,40,1780000000,1);
  if(which==16)reply.bytes[0]=0x25; // broadcast
  datagrams.push_back(reply);assert(step(t)==RISC_HTTP_CLOCK && !opens);close(t);++cases;
 }
 // Source and transaction checks reject stale/unrelated packets, with a fixed
 // datagram count and deadline. A later matching answer can still complete.
 for(unsigned which=0;which<6;++which){
  reset();auto t=sample();sentSample(t);auto bad=response();
  if(which==0)bad.source.sin_addr.s_addr^=1;
  if(which==1)bad.source.sin_port=htons(124);
  if(which==2)bad.source.sin_family=AF_INET6;
  if(which==3)bad.length=sizeof(sockaddr_in)-1;
  if(which==4)bad.bytes[24]^=1;
  if(which==5)std::fill(bad.bytes.begin()+24,bad.bytes.begin()+32,0);
  datagrams.push_back(bad);datagrams.push_back(response());assert(step(t)==RISC_HTTP_AGAIN && !opens);assert(step(t)==RISC_HTTP_AGAIN && opens==1);close(t);++cases;
 }
 reset();{auto t=sample();sentSample(t);auto stale=response();stale.bytes[24]^=1;
  for(unsigned i=0;i<HttpTimeSample::MaximumRejected;++i)datagrams.push_back(stale);
  for(unsigned i=1;i<HttpTimeSample::MaximumRejected;++i)assert(step(t)==RISC_HTTP_AGAIN);
  assert(step(t)==RISC_HTTP_CLOCK && !opens);close(t);++cases;
 }
 for(uint8_t header:{uint8_t(0x1c),uint8_t(0x64),uint8_t(0xa4)}){
  reset();auto t=sample();sentSample(t);auto reply=response();reply.bytes[0]=header;datagrams.push_back(reply);assert(step(t)==RISC_HTTP_AGAIN && opens==1);close(t);++cases;
 }
 reset();{auto t=sample();sentSample(t);datagrams.push_back(response());tlsFail=true;assert(step(t)==RISC_HTTP_MEMORY && !udpLive && udpCloses==1);close(t);++cases;}
 reset();{auto t=sample();sentSample(t);online=false;assert(step(t)==RISC_HTTP_NETWORK && !opens);online=true;close(t);++cases;}
 reset();{auto t=sample();sentSample(t);owned=false;const unsigned old=udpReceives;assert(step(t)==RISC_HTTP_INVALID && udpReceives==old);assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_INVALID);owned=true;close(t);++cases;}
 for(unsigned which=0;which<3;++which){
  reset();auto t=sample();sentSample(t);udpCloseFail=true;
  if(which==0){datagrams.push_back(response());assert(step(t)==RISC_HTTP_RETAINED);}
  if(which==1)assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_RETAINED);
  if(which==2){NativeHttp::session->time.fd=-2;assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_RETAINED);}
  const unsigned closesBefore=udpCloses,readsBefore=udpReceives;
  assert(step(t)==RISC_HTTP_RETAINED && !opens && udpReceives==readsBefore);
  risc_http_response_v1 info{sizeof(info)};assert(NativeHttp::api()->info(nullptr,t,&info)==RISC_HTTP_RETAINED);
  assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_RETAINED && udpCloses==closesBefore && closesBefore==(which==2?0u:1u));
  discardRetained();++cases;
 }
 return cases;
}
int main(){
 unsigned cases=timeTests();
 const char* bad[]={"http://example.test/x","https://","https://a@b/x","https://host:80/x","https://host:443@x/a","https://host/x#y","https://host/a\\b","https://host/\r\nx","https://.host/","https://host./"};
 for(auto url:bad){assert(!HttpBounds::url(url));++cases;}
 assert(HttpBounds::url("https://host:443/a?x=y"));++cases;
 for(size_t block:{size_t(1),size_t(2),size_t(7),size_t(64),size_t(512)}){
  reset();chunk=block;auto t=open();std::string body;assert(run(t,body)==RISC_HTTP_EOF&&body=="hello");
  risc_http_response_v1 info{sizeof(info)};assert(NativeHttp::api()->info(nullptr,t,&info)==0&&info.received_bytes==5&&info.status_code==200);
  assert(sent.find("Accept-Encoding: identity")!=std::string::npos);close(t);++cases;
 }
 const std::vector<std::pair<std::string,int>> samples={
  {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n",RISC_HTTP_EOF},
  {"HTTP/1.0 200 OK\r\n\r\nhello",RISC_HTTP_EOF},
  {"HTTP/1.1 404 Missing\r\nContent-Length: 0\r\n\r\n",RISC_HTTP_STATUS},
  {"HTTP/1.1 200 OK\r\nContent-Length: 9999\r\n\r\n",RISC_HTTP_SIZE},
  {"HTTP/1.1 200 OK\r\nContent-Length: 99\r\n\r\nhello",RISC_HTTP_TRANSPORT},
  {"HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Encoding: gzip\r\n\r\nhello",RISC_HTTP_INVALID},
  {"HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello",RISC_HTTP_TRANSPORT},
  {"HTTP/1.1 302 Found\r\nLocation: http://evil.test/\r\n\r\n",RISC_HTTP_INVALID},
  {"HTTP/1.1 302 Found\r\n\r\n",RISC_HTTP_INVALID},
  {"HTTP/1.1 200 OK\r\nX: "+std::string(2050,'x')+"\r\n\r\n",RISC_HTTP_SIZE},
  {"HTTP/1.1 200 OK\r\n"+std::string(70,'x')+": y\r\n\r\n",RISC_HTTP_SIZE},
 };
 for(const auto& test:samples){reset({test.first});auto t=open();std::string body;assert(run(t,body)==test.second);close(t);++cases;}
 reset({"HTTP/1.1 302 Found\r\nLocation: https://cdn.test/x\r\nContent-Length: 0\r\n\r\n","HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"});
 {auto t=open();std::string body;assert(run(t,body)==RISC_HTTP_EOF&&body=="hello"&&opens==2);close(t);++cases;}
 reset(std::vector<std::string>(5,"HTTP/1.1 302 Found\r\nLocation: https://cdn.test/x\r\nContent-Length: 0\r\n\r\n"));
 {auto t=open();std::string body;assert(run(t,body)==RISC_HTTP_STATUS&&opens==4);close(t);++cases;}
 reset({"HTTP/1.1 200 OK\r\n"+std::string(8, ' ') + "X: "+std::string(1600,'x')+"\r\n\r\n"});
 // Header completion in the same chunk that crosses the total cap must not
 // evade the check merely by setting headersDone before feed returns.
 {std::string headers="HTTP/1.1 200 OK\r\n";for(unsigned i=0;i<12;++i)headers+="X-Header: "+std::string(1400,'x')+"\r\n";headers+="Content-Length: 0\r\n\r\n";
  reset({headers});auto t=open();std::string body;assert(run(t,body)==RISC_HTTP_SIZE);close(t);++cases;}
 for(unsigned waits:{1u,5u,100u}){reset();pendingConnect=waits;auto t=open();std::string body;
  assert(run(t,body)==RISC_HTTP_EOF&&body=="hello"&&connectPolls==waits);close(t);++cases;}
 reset();pendingConnect=1;invalidSocket=true;{auto t=open();std::string body;
  assert(run(t,body)==RISC_HTTP_TRANSPORT&&connectPolls==1);close(t);++cases;}
 reset();slowConnect=true;{auto t=open();std::string body;assert(run(t,body)==RISC_HTTP_TIMEOUT);close(t);++cases;}
 reset();readWait=true;{auto t=open();std::string body;assert(run(t,body)==RISC_HTTP_TIMEOUT);close(t);++cases;}
 reset();{auto t=open();online=false;std::string body;assert(run(t,body)==RISC_HTTP_NETWORK);online=true;close(t);++cases;}
 for(unsigned fail=0;fail<4;++fail){reset();oom=fail==0;lowHeap=fail==1;tlsFail=fail==2;online=fail!=3;
  risc_http_request_v1 r{sizeof(r),"https://example.test/",1024,30000,1780000000};uint64_t t=77;
  assert(NativeHttp::api()->open(nullptr,&r,&t)==(fail==3?RISC_HTTP_NETWORK:RISC_HTTP_MEMORY)&&t==0&&NativeHttp::idle());++cases;
 }
 reset();{auto t=open();uint64_t other=99;risc_http_request_v1 r{sizeof(r),"https://example.test/",1024,30000,1780000000};assert(NativeHttp::api()->open(nullptr,&r,&other)==RISC_HTTP_BUSY&&other==0);close(t);++cases;}
 reset();{auto t=open();std::string body;assert(run(t,body)==RISC_HTTP_EOF);
  mbedtls_x509_crt cert{{2025,1,1,0,0,0},{2027,1,1,0,0,0}};uint32_t flags=0;
  assert(config.f_vrfy(config.p_vrfy,&cert,0,&flags)==0&&flags==0);
  cert.valid_to.year=2025;assert(config.f_vrfy(config.p_vrfy,&cert,0,&flags)==0&&(flags&MBEDTLS_X509_BADCERT_EXPIRED));
  flags=0;cert.valid_from.year=2027;assert(config.f_vrfy(config.p_vrfy,&cert,0,&flags)==0&&(flags&MBEDTLS_X509_BADCERT_FUTURE));
  flags=0;chainFlags=8;assert(config.f_vrfy(config.p_vrfy,&cert,0,&flags)==0&&(flags&8));close(t);cases+=4;
 }
 // A direct checked socket close observes failures that the SDK normally
 // discards. Ambiguous ownership never closes either descriptor.
 for(unsigned which=0;which<4;++which){
  reset();auto t=open();std::string body;assert(run(t,body)==RISC_HTTP_EOF);
  if(which==0)socketCloseFail=true;
  if(which==1)NativeHttp::session->tls->server_fd.fd=8;
  if(which==2)NativeHttp::session->tls->sockfd=-1;
  if(which==3)NativeHttp::session->tls->sockfd=-2;
  assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_RETAINED&&live==0&&!NativeHttp::safe());
  assert(socketCloses==(which==0?1u:0u));
  assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_RETAINED&&closes==1);
  NativeHttp::session->~Session();heap_caps_free(NativeHttp::session);NativeHttp::session=nullptr;++cases;
 }
 // Last case intentionally retains native poison after SDK freed a socket
 // object but could not prove closure; no new requests or unsafe unload.
 reset();{auto t=open();closeFail=true;assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_RETAINED&&!NativeHttp::idle()&&!NativeHttp::safe()&&live==0);assert(NativeHttp::api()->close(nullptr,t)==RISC_HTTP_RETAINED&&closes==1);++cases;
  // Test-process reset only; production correctly requires restart.
  NativeHttp::session->~Session();heap_caps_free(NativeHttp::session);NativeHttp::session=nullptr;
 }
 std::cout<<"Native HTTPS: "<<cases<<" bounded transport/parser/lifecycle cases passed\n";
}
