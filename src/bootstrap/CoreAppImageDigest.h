#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(ESP_PLATFORM)
#include <mbedtls/sha256.h>
#elif defined(RISC_REMOVABLE_APP_TEST)
#include <openssl/evp.h>
#endif

namespace RiscBoot {
// Boot-owned mirrors use an exact content-addressed leaf. External-only app
// paths retain their existing contract and never call this check.
inline bool coreAppImageDigest(const char* path,uint8_t digest[32]) {
  if(!path || !digest)return false;
  const char* leaf=std::strrchr(path,'/');leaf=leaf?leaf+1:path;
  if(std::strlen(leaf)!=74 || std::strncmp(leaf,"image-",6) || std::strcmp(leaf+70,".elf"))return false;
  auto hex=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
  for(size_t i=0;i<32;++i){const int a=hex(leaf[6+2*i]),b=hex(leaf[7+2*i]);if(a<0||b<0)return false;digest[i]=uint8_t(a*16+b);}
  return true;
}
inline bool coreAppImageMatches(const char* path,const uint8_t* bytes,size_t size) {
  uint8_t expected[32]{},actual[32]{};
  if(!bytes || !coreAppImageDigest(path,expected))return false;
#if defined(ESP_PLATFORM)
  return mbedtls_sha256_ret(bytes,size,actual,0)==0 && !std::memcmp(expected,actual,32);
#elif defined(RISC_REMOVABLE_APP_TEST)
  unsigned n=0;
  return EVP_Digest(bytes,size,actual,&n,EVP_sha256(),nullptr)==1 && n==32 && !std::memcmp(expected,actual,32);
#else
  (void)size;(void)actual;return false;
#endif
}
}
