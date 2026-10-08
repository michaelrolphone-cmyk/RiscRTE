#pragma once
#include <openssl/sha.h>
#include <vector>
struct mbedtls_sha256_context {std::vector<unsigned char> bytes;};
// Count only SHA calls made through the production adapter, not fixture setup.
inline size_t nativeHashStarts=0,nativeHashUpdates=0,nativeHashBytes=0,nativeHashFinishes=0;
inline void mbedtls_sha256_init(mbedtls_sha256_context* c){c->bytes.clear();}
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context* c,int){++nativeHashStarts;c->bytes.clear();return 0;}
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context* c,const unsigned char* p,size_t n){++nativeHashUpdates;nativeHashBytes+=n;c->bytes.insert(c->bytes.end(),p,p+n);return 0;}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context* c,unsigned char* out){++nativeHashFinishes;SHA256(c->bytes.data(),c->bytes.size(),out);return 0;}

inline void mbedtls_sha256_free(mbedtls_sha256_context* c){c->bytes.clear();}
