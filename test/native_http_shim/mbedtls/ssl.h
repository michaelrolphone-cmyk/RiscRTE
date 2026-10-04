#pragma once
#include "x509_crt.h"
#define MBEDTLS_SSL_MAJOR_VERSION_3 3
#define MBEDTLS_SSL_MINOR_VERSION_3 3
struct mbedtls_ssl_config {int(*f_vrfy)(void*,mbedtls_x509_crt*,int,uint32_t*)=nullptr;void* p_vrfy=nullptr;int major=0,minor=0;};
inline void mbedtls_ssl_conf_verify(mbedtls_ssl_config* c,int(*f)(void*,mbedtls_x509_crt*,int,uint32_t*),void* p){c->f_vrfy=f;c->p_vrfy=p;}
inline void mbedtls_ssl_conf_min_version(mbedtls_ssl_config* c,int a,int b){c->major=a;c->minor=b;}
