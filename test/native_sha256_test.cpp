#include "runtime/crypto/Sha256.h"
#include <openssl/evp.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <type_traits>
namespace {
std::map<psa_hash_operation_t*,EVP_MD_CTX*> live;
bool initFail=false,setupFail=false,updateFail=false,finishFail=false,abortFail=false,shortDigest=false;
unsigned setups=0,aborts=0;
void release(psa_hash_operation_t* p){auto i=live.find(p);if(i!=live.end()){EVP_MD_CTX_free(i->second);live.erase(i);}}
}
extern "C" psa_status_t psa_crypto_init(){return initFail?PSA_ERROR_GENERIC_ERROR:PSA_SUCCESS;}
extern "C" psa_status_t psa_hash_compute(psa_algorithm_t algorithm,const uint8_t* bytes,size_t count,uint8_t* out,size_t capacity,size_t* length){
  assert(algorithm==PSA_ALG_SHA_256 && capacity==32);if(finishFail)return PSA_ERROR_GENERIC_ERROR;
  unsigned size=0;assert(EVP_Digest(bytes,count,out,&size,EVP_sha256(),nullptr)==1);*length=shortDigest?31:size;return PSA_SUCCESS;
}
extern "C" psa_status_t psa_hash_abort(psa_hash_operation_t* p){++aborts;if(abortFail)return PSA_ERROR_GENERIC_ERROR;release(p);return PSA_SUCCESS;}
extern "C" psa_status_t psa_hash_setup(psa_hash_operation_t* p,psa_algorithm_t algorithm){
  assert(algorithm==PSA_ALG_SHA_256 && !live.count(p));++setups;
  // Even a failed setup may have partial native custody.
  auto* context=EVP_MD_CTX_new();assert(context);live[p]=context;
  assert(EVP_DigestInit_ex(context,EVP_sha256(),nullptr)==1);
  return setupFail?PSA_ERROR_GENERIC_ERROR:PSA_SUCCESS;
}
extern "C" psa_status_t psa_hash_update(psa_hash_operation_t* p,const uint8_t* bytes,size_t size){
  assert(live.count(p));if(updateFail)return PSA_ERROR_GENERIC_ERROR;
  assert(EVP_DigestUpdate(live[p],bytes,size)==1);return PSA_SUCCESS;
}
extern "C" psa_status_t psa_hash_finish(psa_hash_operation_t* p,uint8_t* digest,size_t capacity,size_t* size){
  assert(live.count(p) && capacity==32);if(finishFail)return PSA_ERROR_GENERIC_ERROR;
  unsigned actual=0;assert(EVP_DigestFinal_ex(live[p],digest,&actual)==1);release(p);
  *size=shortDigest?31:actual;return PSA_SUCCESS;
}
int main(){
  static_assert(!std::is_copy_constructible<RiscCrypto::Sha256>::value);
  static_assert(!std::is_move_constructible<RiscCrypto::Sha256>::value);
  RiscCrypto::Sha256 h;h.initialize();std::array<uint8_t,32> digest{};
  assert(!h.add(nullptr,0) && !h.finish(digest.data()));
  assert(h.begin());assert(h.add(reinterpret_cast<const uint8_t*>("a"),1));
  assert(h.add(reinterpret_cast<const uint8_t*>("bc"),2));assert(h.finish(digest.data()));
  const uint8_t abc[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
  assert(std::memcmp(digest.data(),abc,32)==0 && live.empty());
  assert(!h.finish(digest.data()));
  assert(RiscCrypto::Sha256::digest(reinterpret_cast<const uint8_t*>("abc"),3,digest.data()));
  assert(std::memcmp(digest.data(),abc,32)==0);
  finishFail=true;assert(!RiscCrypto::Sha256::digest(nullptr,0,digest.data()));finishFail=false;
  shortDigest=true;assert(!RiscCrypto::Sha256::digest(nullptr,0,digest.data()));shortDigest=false;
  assert(!RiscCrypto::Sha256::digest(nullptr,1,digest.data()));
  assert(!RiscCrypto::Sha256::digest(nullptr,0,nullptr));
  assert(h.begin() && h.begin() && live.size()==1); // abandoned operation released
  abortFail=true;unsigned before=setups;assert(!h.begin() && setups==before && live.size()==1);
  abortFail=false;assert(h.begin() && live.size()==1);assert(h.reset() && live.empty());
  initFail=true;assert(!h.begin() && live.empty());initFail=false;
  setupFail=true;assert(!h.begin() && live.empty());setupFail=false;
  assert(h.begin());updateFail=true;assert(!h.add(nullptr,0) && live.empty());updateFail=false;
  assert(!h.add(nullptr,0));
  assert(h.begin());finishFail=true;assert(!h.finish(digest.data()) && live.empty());finishFail=false;
  assert(h.begin());abortFail=true;assert(!h.finish(digest.data()));abortFail=false;assert(h.reset());
  assert(h.begin());shortDigest=true;assert(!h.finish(digest.data()));shortDigest=false;
  assert(h.begin());assert(!h.finish(nullptr));assert(h.reset() && live.empty());
  assert(h.begin());abortFail=true;assert(!h.reset() && live.size()==1);abortFail=false;
  assert(h.reset() && live.empty() && aborts>setups);
  std::puts("Native PSA SHA256: known digest, split updates, restart/abort custody and every failure path PASS");
}
