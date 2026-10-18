#pragma once
#include <stddef.h>
// Compile-only substitute for the host harness OpenSSL-backed shim.
// This type is not evidence of the SDK SHA operation's layout or stack.
struct mbedtls_sha256_context {void* context;};
extern "C" {
void mbedtls_sha256_init(mbedtls_sha256_context*);
void mbedtls_sha256_free(mbedtls_sha256_context*);
int mbedtls_sha256_starts_ret(mbedtls_sha256_context*,int);
int mbedtls_sha256_update_ret(mbedtls_sha256_context*,const unsigned char*,size_t);
int mbedtls_sha256_finish_ret(mbedtls_sha256_context*,unsigned char*);
int mbedtls_sha256_ret(const unsigned char*,size_t,unsigned char*,int);
}
