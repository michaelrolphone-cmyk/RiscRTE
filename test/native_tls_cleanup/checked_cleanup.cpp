/* Synthetic fd operations only: no actual socket, RF, or network operations.
 * SDK cleanup functions are copied verbatim from IDF 38eeba213a.
 */
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <initializer_list>
struct mbedtls_net_context{int fd;};
struct esp_tls_t{int sockfd;mbedtls_net_context server_fd;bool is_tls;int error_handle;};
static bool failClose=false;static int closeCalls=0,shutdownCalls=0,frees=0,cleanups=0,lastFd=-1;
static int fake_close(int fd){++closeCalls;lastFd=fd;return failClose?-1:0;}
static int fake_shutdown(int,int){++shutdownCalls;return 0;}
static void fake_free(void*p){++frees;std::free(p);}
static void esp_mbedtls_cleanup(esp_tls_t*){++cleanups;}
static void esp_tls_internal_event_tracker_destroy(int){}
#define close fake_close
#define shutdown fake_shutdown
#define free fake_free
/* Exact MbedTLS 2b8e772f net_sockets.c mbedtls_net_free body. */
static void mbedtls_net_free(mbedtls_net_context *ctx){
    if (ctx->fd == -1) {
        return;
    }
    shutdown(ctx->fd, 2);
    close(ctx->fd);
    ctx->fd = -1;
}
#define _esp_tls_conn_delete esp_mbedtls_conn_delete
#include "sdk_cleanup.inc"
struct Session{esp_tls_t*tls;bool retained=false;};
#include "checked_destroy.inc"
#undef close
#undef shutdown
#undef free
static Session fixture(int fd,int wrapped,bool tls=true){
 failClose=false;closeCalls=shutdownCalls=frees=cleanups=0;lastFd=-1;
 auto*p=static_cast<esp_tls_t*>(std::malloc(sizeof(esp_tls_t)));assert(p);
 *p={fd,{wrapped},tls,0};return {p,false};
}
int main(){
 unsigned cases=0;
 {auto s=fixture(7,7);failClose=true;assert(esp_tls_conn_destroy(s.tls)==0&&closeCalls==1&&shutdownCalls==1&&frees==1);++cases;}
 puts("SDK control: established-TLS close failure is discarded (destroy returns 0).");
 for(bool tls:{false,true}){
  auto s=fixture(7,tls?7:-1,tls);assert(checkedDestroy(s)&&closeCalls==1&&lastFd==7&&shutdownCalls==0&&frees==1&&cleanups==1);
  assert(checkedDestroy(s)&&closeCalls==1&&frees==1);++cases;
 }
 {auto s=fixture(-1,-1,false);assert(checkedDestroy(s)&&closeCalls==0&&frees==1&&shutdownCalls==0);++cases;}
 for(bool tls:{false,true}){
  auto s=fixture(7,tls?7:-1,tls);failClose=true;
  assert(!checkedDestroy(s)&&s.retained&&s.tls==nullptr&&closeCalls==1&&frees==1&&shutdownCalls==0);
  assert(!checkedDestroy(s)&&closeCalls==1&&frees==1);++cases;
 }
 for(auto pair:{std::pair<int,int>{7,8},{-1,7},{-2,-1},{7,-2}}){
  auto s=fixture(pair.first,pair.second);assert(!checkedDestroy(s)&&s.retained&&closeCalls==0&&frees==1&&shutdownCalls==0);++cases;
 }
 printf("%u checked-close SDK ownership, failure, no-retry, and no-double-close cases passed.\n",cases);
}
