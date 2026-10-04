#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <sys/select.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_TLS_ERR_SSL_WANT_READ -0x6900
#define ESP_TLS_ERR_SSL_WANT_WRITE -0x6880
using esp_err_t=int;
enum esp_tls_conn_state_t { ESP_TLS_INIT,ESP_TLS_CONNECTING,ESP_TLS_HANDSHAKE };
struct esp_tls_t { unsigned index=0;int sockfd=-1;struct {int fd=-1;} server_fd;esp_tls_conn_state_t conn_state=ESP_TLS_INIT;fd_set rset{},wset{}; };
struct esp_tls_cfg_t { bool non_block=false;int timeout_ms=0;bool skip_common_name=false;esp_err_t(*crt_bundle_attach)(void*)=nullptr; };
esp_tls_t* esp_tls_init();
int esp_tls_conn_new_async(const char*,int,int,const esp_tls_cfg_t*,esp_tls_t*);
ssize_t esp_tls_conn_write(esp_tls_t*,const void*,size_t);
ssize_t esp_tls_conn_read(esp_tls_t*,void*,size_t);
int esp_tls_conn_destroy(esp_tls_t*);
