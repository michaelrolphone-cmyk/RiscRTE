#pragma once
#include <cstddef>
#include <cstdint>
#if RISC_NATIVE_ESP_IDF
#include <psa/crypto.h>
#else
#include <mbedtls/sha256.h>
#endif
namespace RiscCrypto {
// One owner, one streaming hash. Failed/abandoned operations are aborted before
// reuse; an error can never be mistaken for a completed 32-byte digest.
class Sha256 {
#if RISC_NATIVE_ESP_IDF
  psa_hash_operation_t operation_=PSA_HASH_OPERATION_INIT;
  bool active_=false;
#else
  mbedtls_sha256_context context_{};
#endif
public:
  Sha256()=default;
  ~Sha256(){
#if RISC_NATIVE_ESP_IDF
    if(release()){volatile uint8_t* p=reinterpret_cast<volatile uint8_t*>(&operation_);for(size_t i=0;i<sizeof(operation_);++i)p[i]=0;}
#else
    (void)release(); // mbedTLS frees and zeroizes its context.
#endif
  }
  bool release(){
#if RISC_NATIVE_ESP_IDF
    return reset();
#else
    mbedtls_sha256_free(&context_);return true;
#endif
  }
  static bool digest(const uint8_t* bytes,size_t count,uint8_t* out){
    if(!out || (!bytes && count))return false;
#if RISC_NATIVE_ESP_IDF
    size_t size=0;
    return psa_crypto_init()==PSA_SUCCESS &&
      psa_hash_compute(PSA_ALG_SHA_256,bytes,count,out,32,&size)==PSA_SUCCESS && size==32;
#else
    Sha256 hash;hash.initialize();return hash.begin() && hash.add(bytes,count) && hash.finish(out);
#endif
  }
  Sha256(const Sha256&)=delete;
  Sha256& operator=(const Sha256&)=delete;
  void initialize(){
#if !RISC_NATIVE_ESP_IDF
    mbedtls_sha256_init(&context_);
#endif
  }
  bool reset(){
#if RISC_NATIVE_ESP_IDF
    active_=false;return psa_hash_abort(&operation_)==PSA_SUCCESS;
#else
    return true;
#endif
  }
  bool begin(){
#if RISC_NATIVE_ESP_IDF
    if(!reset() || psa_crypto_init()!=PSA_SUCCESS)return false;
    active_=psa_hash_setup(&operation_,PSA_ALG_SHA_256)==PSA_SUCCESS;
    if(!active_)(void)psa_hash_abort(&operation_);
    return active_;
#else
    return mbedtls_sha256_starts_ret(&context_,0)==0;
#endif
  }
  bool add(const uint8_t* bytes,size_t size){
#if RISC_NATIVE_ESP_IDF
    if(!active_)return false;
    if(psa_hash_update(&operation_,bytes,size)==PSA_SUCCESS)return true;
    active_=false;(void)psa_hash_abort(&operation_);return false;
#else
    return mbedtls_sha256_update_ret(&context_,bytes,size)==0;
#endif
  }
  bool finish(uint8_t* digest){
#if RISC_NATIVE_ESP_IDF
    if(!active_ || !digest)return false;
    size_t size=0;active_=false;
    const auto result=psa_hash_finish(&operation_,digest,32,&size);
    const auto cleanup=psa_hash_abort(&operation_);
    return result==PSA_SUCCESS && size==32 && cleanup==PSA_SUCCESS;
#else
    return mbedtls_sha256_finish_ret(&context_,digest)==0;
#endif
  }
};
}
