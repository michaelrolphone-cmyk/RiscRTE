/* Exact esp_tls_low_level_conn from IDF 38eeba213a. Only socket establishment
 * and TLS crypto are replaced; select() is the real host POSIX implementation.
 * A pipe read end simulates a TCP descriptor becoming ready after the first poll.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int esp_err_t;
typedef struct { bool is_plain_tcp, non_block; int timeout_ms; } esp_tls_cfg_t;
enum { ESP_TLS_INIT, ESP_TLS_CONNECTING, ESP_TLS_HANDSHAKE, ESP_TLS_FAIL, ESP_TLS_DONE };
typedef struct esp_tls { int sockfd; bool is_tls; int conn_state,error_handle; fd_set rset,wset; void *read,*write; } esp_tls_t;
#define ESP_OK 0
#define ESP_ERR_ESP_TLS_SOCKET_SETOPT_FAILED -1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_INT_EVENT_TRACKER_CAPTURE(...) ((void)0)
#define _esp_tls_net_init(...) ((void)0)
#define tcp_read NULL
#define tcp_write NULL
#define _esp_tls_read NULL
#define _esp_tls_write NULL
static int pending_fd, tls_creations;
static int tcp_connect(const char*h,int l,int p,const esp_tls_cfg_t*c,int e,int*fd){(void)h;(void)l;(void)p;(void)c;(void)e;*fd=pending_fd;return ESP_OK;}
static void ms_to_timeval(int n,struct timeval*t){t->tv_sec=n/1000;t->tv_usec=n%1000*1000;}
static int create_ssl_handle(const char*h,int l,const void*c,esp_tls_t*t){(void)h;(void)l;(void)c;(void)t;++tls_creations;return 0;}
static int esp_tls_handshake(esp_tls_t*t,const esp_tls_cfg_t*c){(void)c;t->conn_state=ESP_TLS_DONE;return 1;}
/* Once the pipe becomes readable, model successful TCP SO_ERROR. */
static int model_getsockopt(int f,int l,int o,void*v,socklen_t*n){(void)f;(void)l;(void)o;(void)n;*(int*)v=0;return 0;}
#define getsockopt model_getsockopt
#include "low_level_conn.inc"
#undef getsockopt
int main(void){
 int pipefd[2];assert(pipe(pipefd)==0);pending_fd=pipefd[0];
 esp_tls_t tls={0};esp_tls_cfg_t cfg={.is_plain_tcp=false,.non_block=true,.timeout_ms=1};
 int r=esp_tls_low_level_conn("fixture.test",12,443,&cfg,&tls);
 assert(r==0 && tls.conn_state==ESP_TLS_CONNECTING);
 printf("First timeout: result=%d read_set=%d write_set=%d\n",r,FD_ISSET(pending_fd,&tls.rset),FD_ISSET(pending_fd,&tls.wset));
 assert(write(pipefd[1],"x",1)==1);
 for(int i=0;i<100;i++)assert(esp_tls_low_level_conn("fixture.test",12,443,&cfg,&tls)==0);
 assert(tls_creations==0);
 printf("Ready descriptor after timeout: 100 later polls still pending; TLS setups=%d\n",tls_creations);
 /* Narrow adapter workaround: re-arm both sets before each CONNECTING call. */
 FD_ZERO(&tls.rset);FD_SET(tls.sockfd,&tls.rset);tls.wset=tls.rset;
 assert(esp_tls_low_level_conn("fixture.test",12,443,&cfg,&tls)==1);
 printf("After re-arming fd sets: connected; TLS setups=%d\n",tls_creations);
 close(pipefd[0]);close(pipefd[1]);
}
