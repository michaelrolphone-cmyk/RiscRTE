#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int mbedtls_sha256_ret(const unsigned char*, size_t, unsigned char[32], int);
#ifdef __cplusplus
}
#endif
/* Production SHA adapter now uses the streaming SDK4 API. This harness hashes
 * one immutable span and retains the existing digest-observation hook. */
typedef struct { const unsigned char* bytes; size_t size; } mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context* c) { c->bytes=NULL;c->size=0; }
static inline void mbedtls_sha256_free(mbedtls_sha256_context* c) { c->bytes=NULL;c->size=0; }
static inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context* c,int mode) { mbedtls_sha256_init(c);return mode?-1:0; }
static inline int mbedtls_sha256_update_ret(mbedtls_sha256_context* c,const unsigned char* p,size_t n) { if(c->bytes)return -1;c->bytes=p;c->size=n;return 0; }
static inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context* c,unsigned char* out) { return mbedtls_sha256_ret(c->bytes,c->size,out,0); }
