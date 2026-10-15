#pragma once
#include <mbedtls/sha256.h>
typedef int psa_status_t;
typedef struct { const unsigned char* bytes; size_t size; } psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {NULL,0}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 0x02000009
static inline int psa_crypto_init(void) { return 0; }
static inline int psa_hash_abort(psa_hash_operation_t* op) { op->bytes=NULL;op->size=0;return 0; }
static inline int psa_hash_setup(psa_hash_operation_t* op,unsigned alg) { psa_hash_abort(op);return alg==PSA_ALG_SHA_256?0:-1; }
static inline int psa_hash_update(psa_hash_operation_t* op,const unsigned char* bytes,size_t n) { if(op->bytes)return -1;op->bytes=bytes;op->size=n;return 0; }
static inline int psa_hash_compute(unsigned alg,const unsigned char* bytes,size_t n,unsigned char* out,size_t capacity,size_t* used) {
 if(alg!=PSA_ALG_SHA_256 || capacity<32)return -1;
 *used=32;return mbedtls_sha256_ret(bytes,n,out,0);
}
static inline int psa_hash_finish(psa_hash_operation_t* op,unsigned char* out,size_t capacity,size_t* used) { return psa_hash_compute(PSA_ALG_SHA_256,op->bytes,op->size,out,capacity,used); }
