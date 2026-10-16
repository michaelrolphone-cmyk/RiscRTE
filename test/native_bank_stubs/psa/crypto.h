#pragma once
#include <mbedtls/sha256.h>
#include <new>
#include <type_traits>
typedef int psa_status_t;
// The real PSA operation is C-shaped and may be zeroized after successful abort.
// Keep C++ allocation ownership behind a trivial handle, never inside that ABI.
struct psa_hash_operation_t {mbedtls_sha256_context* context;};
static_assert(std::is_trivially_copyable<psa_hash_operation_t>::value);
static_assert(std::is_trivially_destructible<psa_hash_operation_t>::value);
inline size_t nativePsaHashLive=0;
#define PSA_HASH_OPERATION_INIT {}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 0x02000009
inline int psa_crypto_init(){return 0;}
inline int psa_hash_abort(psa_hash_operation_t* op){
 if(op->context){mbedtls_sha256_free(op->context);delete op->context;op->context=nullptr;--nativePsaHashLive;}
 return 0;
}
inline int psa_hash_setup(psa_hash_operation_t* op,unsigned alg){
 if(alg!=PSA_ALG_SHA_256 || op->context)return -1;
 op->context=new(std::nothrow) mbedtls_sha256_context{};if(!op->context)return -1;
 ++nativePsaHashLive;return mbedtls_sha256_starts_ret(op->context,0);
}
inline int psa_hash_update(psa_hash_operation_t* op,const unsigned char* bytes,size_t size){
 return op->context?mbedtls_sha256_update_ret(op->context,bytes,size):-1;
}
inline int psa_hash_finish(psa_hash_operation_t* op,unsigned char* out,size_t capacity,size_t* used){
 if(!op->context || !out || !used || capacity<32)return -1;
 const int result=mbedtls_sha256_finish_ret(op->context,out);if(result)return result;
 *used=32;return psa_hash_abort(op);
}
inline int psa_hash_compute(unsigned alg,const unsigned char* bytes,size_t size,unsigned char* out,size_t capacity,size_t* used){
 psa_hash_operation_t op{};
 const int result=psa_hash_setup(&op,alg) || psa_hash_update(&op,bytes,size) || psa_hash_finish(&op,out,capacity,used)?-1:0;
 psa_hash_abort(&op);return result;
}
