#pragma once
#include "NativeProviderPolicyV1.h"
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_PROVIDER_ADMISSION_TEST)
extern "C" {
#include <private/esp_privileged_elf.h>
#include <private/esp_privileged_manifest_imports.h>
bool esp_elf_validate_file(const uint8_t*, size_t);
bool esp_elf_privileged_selected_import_supported_v1(const char*);
}
#ifdef ESP_PLATFORM
#include <mbedtls/sha256.h>
#else
#include <openssl/evp.h>
#endif
#endif

namespace RuntimeProviders {
inline bool nativeProviderDigest(const uint8_t* bytes,size_t length,uint8_t digest[32]) {
#if defined(ESP_PLATFORM)
  return bytes && digest && mbedtls_sha256_ret(bytes,length,digest,0)==0;
#elif defined(RISC_NATIVE_PROVIDER_ADMISSION_TEST)
  unsigned int n=0;
  return bytes && digest && EVP_Digest(bytes,length,digest,&n,EVP_sha256(),nullptr)==1 && n==32;
#else
  (void)bytes;(void)length;(void)digest;return false;
#endif
}
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_PROVIDER_ADMISSION_TEST)
// Call only after the production structural validator. Match dlsym's actual
// dynamic export contract, not a static symbol with a convenient name.
inline bool nativeProviderEntryValid(const uint8_t* bytes) {
  const auto* h=reinterpret_cast<const elf32_hdr_t*>(bytes);
  const auto* sections=reinterpret_cast<const elf32_shdr_t*>(bytes+h->shoff);
  const char* sectionNames=reinterpret_cast<const char*>(bytes+sections[h->shstrndx].offset);
  unsigned entry=0;
  for(unsigned i=0;i<h->shnum;++i) {
    const auto& table=sections[i];
    if(table.type!=SHT_SYNSYM || std::strcmp(sectionNames+table.name,ELF_DYNSYM))continue;
    const char* strings=reinterpret_cast<const char*>(bytes+sections[table.link].offset);
    const auto* symbols=reinterpret_cast<const elf32_sym_t*>(bytes+table.offset);
    for(size_t j=0;j<table.size/sizeof(*symbols);++j) {
      const auto& sym=symbols[j];
      if(sym.shndx==SHN_UNDEF)continue;
      const char* name=strings+sym.name;
      if(!std::strcmp(name,"app_main") || !std::strcmp(name,"app_module_init") ||
         !std::strcmp(name,"app_module_fini"))return false;
      if(std::strcmp(name,"t5_driver_get"))continue;
      if(++entry!=1 || ELF_ST_TYPE(sym.info)!=STT_FUNC || ELF_ST_BIND(sym.info)!=STB_GLOBAL ||
         sym.shndx>=h->shnum)return false;
      const auto& text=sections[sym.shndx];
      if(std::strcmp(sectionNames+text.name,ELF_TEXT) || !(text.flags&SHF_EXECINSTR) ||
         sym.value<text.addr || uint64_t(sym.value)>=uint64_t(text.addr)+text.size)return false;
    }
  }
  return entry==1;
}
#endif
inline bool nativeProviderImageValid(const NativeProviderPolicyV1& p,const uint8_t* bytes,size_t length) {
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_PROVIDER_ADMISSION_TEST)
  uint8_t digest[32]{};
  return nativeProviderPolicyValid(p) && bytes && length==p.elfLength &&
      nativeProviderDigest(bytes,length,digest) && !std::memcmp(digest,p.sha256,32) &&
      esp_elf_validate_file(bytes,length) && nativeProviderEntryValid(bytes) &&
      esp_elf_privileged_manifest_imports_match_v1(bytes,length,p.imports,p.importCount);
#else
  (void)p;(void)bytes;(void)length;return false;
#endif
}
} // namespace RuntimeProviders
