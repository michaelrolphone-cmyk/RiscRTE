#pragma once
#include <mbedtls/sha256.h>
typedef int psa_status_t;
typedef mbedtls_sha256_context psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 0x02000009
inline int psa_crypto_init(){return 0;}
inline int psa_hash_abort(psa_hash_operation_t* op){mbedtls_sha256_free(op);return 0;}
inline int psa_hash_setup(psa_hash_operation_t* op,unsigned alg){return alg==PSA_ALG_SHA_256?mbedtls_sha256_starts_ret(op,0):-1;}
inline int psa_hash_update(psa_hash_operation_t* op,const unsigned char* bytes,size_t size){return mbedtls_sha256_update_ret(op,bytes,size);}
inline int psa_hash_finish(psa_hash_operation_t* op,unsigned char* out,size_t capacity,size_t* used){
 if(capacity<32)return -1;
 *used=32;return mbedtls_sha256_finish_ret(op,out);
}
inline int psa_hash_compute(unsigned alg,const unsigned char* bytes,size_t size,unsigned char* out,size_t capacity,size_t* used){
 psa_hash_operation_t op{};return psa_hash_setup(&op,alg) || psa_hash_update(&op,bytes,size) || psa_hash_finish(&op,out,capacity,used)?-1:0;
}
