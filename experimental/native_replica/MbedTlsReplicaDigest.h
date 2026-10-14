#pragma once
#include "CheckedSdReplicaCheckpoint.h"
#include <mbedtls/sha256.h>
namespace RiscExperimental {
// Optional native SHA-256 provider, compiled against the pinned IDF headers.
// One instance belongs exclusively to one transport, until exitSafe/reset.
class MbedTlsReplicaDigest {
 public:
 MbedTlsReplicaDigest(){mbedtls_sha256_init(&context_);}
 ~MbedTlsReplicaDigest(){mbedtls_sha256_free(&context_);NativeReplicaCore::wipe(&context_,sizeof context_);}
 MbedTlsReplicaDigest(const MbedTlsReplicaDigest&)=delete;
 MbedTlsReplicaDigest&operator=(const MbedTlsReplicaDigest&)=delete;
 CheckedSdReplicaCheckpoint::Digest api(){return {this,
  [](void*p){return mbedtls_sha256_starts_ret(&static_cast<MbedTlsReplicaDigest*>(p)->context_,0)==0;},
  [](void*p,const void*bytes,uint32_t n){return n<=4096&&mbedtls_sha256_update_ret(&static_cast<MbedTlsReplicaDigest*>(p)->context_,static_cast<const unsigned char*>(bytes),n)==0;},
  [](void*p,uint8_t out[32]){return mbedtls_sha256_finish_ret(&static_cast<MbedTlsReplicaDigest*>(p)->context_,out)==0;}};}
 private:
 mbedtls_sha256_context context_{};
};
}
