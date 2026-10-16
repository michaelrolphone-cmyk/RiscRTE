#include "runtime/crypto/Sha256.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(){
 const unsigned char bytes[1024]{};unsigned char digest[32]{},expected[32]{};
 SHA256(bytes,sizeof(bytes),expected);
 // A successful free must release capacity, including before C ABI zeroization.
 mbedtls_sha256_context legacy{};
 assert(mbedtls_sha256_update_ret(&legacy,bytes,sizeof(bytes))==0);
 assert(legacy.bytes.capacity()>=sizeof(bytes));
 mbedtls_sha256_free(&legacy);assert(legacy.bytes.empty() && legacy.bytes.capacity()==0);
 mbedtls_sha256_free(&legacy);assert(legacy.bytes.capacity()==0);
#if RISC_NATIVE_ESP_IDF
 assert(nativePsaHashLive==0);
 {
  psa_hash_operation_t op=PSA_HASH_OPERATION_INIT;
  assert(psa_hash_setup(&op,PSA_ALG_SHA_256)==0 && nativePsaHashLive==1);
  assert(psa_hash_update(&op,bytes,sizeof(bytes))==0);
  assert(psa_hash_abort(&op)==0 && !op.context && nativePsaHashLive==0);
  assert(psa_hash_abort(&op)==0 && nativePsaHashLive==0);
 }
 {
  RiscCrypto::Sha256 hash;assert(hash.begin() && hash.add(bytes,sizeof(bytes)));
  assert(nativePsaHashLive==1); // Destructor must abort an unfinished operation.
 }
 assert(nativePsaHashLive==0);
#endif
 {
  RiscCrypto::Sha256 hash;hash.initialize();
  assert(hash.begin() && hash.add(bytes,sizeof(bytes)));
  assert(hash.begin() && hash.add(bytes,sizeof(bytes))); // Restart an unfinished hash.
  assert(hash.finish(digest) && !memcmp(digest,expected,sizeof(digest)));
  assert(hash.reset());
#if RISC_NATIVE_ESP_IDF
  assert(nativePsaHashLive==0);
#endif
 }
 assert(RiscCrypto::Sha256::digest(bytes,sizeof(bytes),digest));
 assert(!memcmp(digest,expected,sizeof(digest)));
#if RISC_NATIVE_ESP_IDF
 size_t used=0;
 assert(psa_hash_compute(PSA_ALG_SHA_256,bytes,sizeof(bytes),digest,1,&used)!=0);
 assert(nativePsaHashLive==0); // Failed one-shot finalization releases its handle.
 assert(psa_hash_compute(0,bytes,sizeof(bytes),digest,sizeof(digest),&used)!=0);
 assert(nativePsaHashLive==0);
#endif
 std::puts("Native-bank crypto fixtures: free capacity, abort/restart/destructor and one-shot cleanup PASS");
}
