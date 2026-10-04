#include "ports/esp32s3/NativeHttp.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <vector>
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
static int chain(void*,mbedtls_x509_crt*,int,uint32_t* flags){*flags|=chainFlags;return 0;}
int64_t esp_timer_get_time(){return clockMs*1000;}
size_t heap_caps_get_free_size(unsigned){return lowHeap?79*1024:128*1024;}
size_t heap_caps_get_largest_free_block(unsigned){return 48*1024;}
void* heap_caps_malloc(size_t n,unsigned caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(oom)return nullptr;++allocations;return std::malloc(n);}
void heap_caps_free(void* p){--allocations;std::free(p);}
esp_tls_t* esp_tls_init(){if(tlsFail)return nullptr;++live;return new esp_tls_t{static_cast<unsigned>(opens++)};}
extern "C" int __wrap_close(int fd){assert(fd==7);++socketCloses;return socketCloseFail?-1:0;}
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
 assert(NativeHttp::idle()&&live==0&&allocations==0);
 clockMs=0;owned=online=true;oom=lowHeap=closeFail=tlsFail=slowConnect=readWait=false;chainFlags=0;
 pendingConnect=connectPolls=socketCloses=0;invalidSocket=socketCloseFail=false;
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
int main(){
 unsigned cases=0;
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
