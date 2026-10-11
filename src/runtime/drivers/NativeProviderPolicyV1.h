#pragma once

#include "runtime/packages/PackageIdentity.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_PROVIDER_ADMISSION_TEST)
extern "C" bool esp_elf_privileged_selected_import_supported_with_diagnostics_v1(const char*, uint32_t);
#endif

namespace RuntimeProviders {
// Private firmware policy. None of this API is an ELF export or capability.
// The trusted native caller/compiled selection supplies authority; packages,
// boot JSON, sidecars, capability names and self-declared hashes cannot do so.
struct NativeProviderRequirementV1 {
  const char* capability;
  uint32_t api;
};
struct NativeProviderPolicyV1 {
  const char* relativeElfPath = nullptr;
  const char* driverId = nullptr;
  const char* version = nullptr;
  const char* capability = nullptr;
  uint32_t api = 0;
  uint32_t osCpuAbi = 0;
  size_t elfLength = 0;
  uint8_t sha256[32]{};
  const char* const* imports = nullptr;
  size_t importCount = 0;
  const NativeProviderRequirementV1* requirements = nullptr;
  size_t requirementCount = 0;
  // Explicitly selected private native diagnostic ABI; zero denies console imports.
  uint32_t diagnosticAbi = 0;
};
struct NativeProviderPolicySetV1 {
  const NativeProviderPolicyV1* entries = nullptr;
  size_t count = 0;
};
inline bool nativePolicyName(const char* s, size_t capacity) {
  if (!s || !*s) return false;
  size_t i=0; while (i<capacity && s[i]) ++i;
  return i<capacity;
}
inline bool nativePolicyPath(const char* s) {
  if (!nativePolicyName(s,193) || *s=='/' || std::strchr(s,'\\') || std::strchr(s,':')) return false;
  const char* at=s;
  for (const char* p=s;;++p) {
    if (*p && *p!='/') continue;
    const size_t n=size_t(p-at);
    if (!n || (n==1 && at[0]=='.') || (n==2 && at[0]=='.' && at[1]=='.')) return false;
    if (!*p) return RuntimePackages::safeArtifact(at);
    at=p+1;
  }
}
inline bool nativeProviderPolicyValid(const NativeProviderPolicyV1& p) {
  if (!nativePolicyPath(p.relativeElfPath) || !RuntimePackages::safeId(p.driverId) ||
      !RuntimePackages::safeVersion(p.version) || !nativePolicyName(p.capability,96) ||
      !p.api || p.osCpuAbi!=1 || p.diagnosticAbi>1 || !p.elfLength || p.elfLength>8u*1024u*1024u ||
      !p.imports || p.importCount>128 || p.requirementCount>16 ||
      (p.requirementCount && !p.requirements)) return false;
  bool digest=false; for (uint8_t byte:p.sha256) digest=digest || byte;
  if (!digest) return false;
  bool hasDiagnostic=false;
  for (size_t i=0;i<p.importCount;++i) {
    if (!nativePolicyName(p.imports[i],128) ||
        (i && std::strcmp(p.imports[i-1],p.imports[i])>=0) ||
        (!p.diagnosticAbi && (!std::strcmp(p.imports[i],"printf") ||
         !std::strcmp(p.imports[i],"puts") || !std::strcmp(p.imports[i],"putchar")))) return false;
    hasDiagnostic=hasDiagnostic || !std::strcmp(p.imports[i],"printf") ||
      !std::strcmp(p.imports[i],"puts") || !std::strcmp(p.imports[i],"putchar");
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_PROVIDER_ADMISSION_TEST)
    if (!esp_elf_privileged_selected_import_supported_with_diagnostics_v1(p.imports[i], p.diagnosticAbi)) return false;
#endif
  }
  if(p.diagnosticAbi && !hasDiagnostic)return false;
  for (size_t i=0;i<p.requirementCount;++i) {
    if (!nativePolicyName(p.requirements[i].capability,96) || !p.requirements[i].api) return false;
    for (size_t j=0;j<i;++j) if (!std::strcmp(p.requirements[i].capability,p.requirements[j].capability)) return false;
  }
  return true;
}
inline bool nativeProviderPolicySetValid(const NativeProviderPolicySetV1& set) {
  if (set.count>16 || (set.count && !set.entries) || (!set.count && set.entries)) return false;
  for (size_t i=0;i<set.count;++i) {
    if (!nativeProviderPolicyValid(set.entries[i])) return false;
    for (size_t j=0;j<i;++j)
      if (!std::strcmp(set.entries[i].relativeElfPath,set.entries[j].relativeElfPath) ||
          !std::strcmp(set.entries[i].driverId,set.entries[j].driverId)) return false;
  }
  return true;
}
inline bool nativePolicyMatchesModule(const NativeProviderPolicyV1& p,
    const uint8_t* bytes,size_t length,const uint8_t digest[32],
    const char* const* imports,size_t importCount,const char* id,
    const char* capability,uint32_t api) {
  if (!nativeProviderPolicyValid(p) || !bytes || length!=p.elfLength || !digest ||
      std::memcmp(digest,p.sha256,32) || !id || std::strcmp(id,p.driverId) ||
      !capability || std::strcmp(capability,p.capability) || api!=p.api ||
      !imports || importCount!=p.importCount) return false;
  for (size_t i=0;i<importCount;++i)
    if (!imports[i] || std::strcmp(imports[i],p.imports[i])) return false;
  return true;
}
} // namespace RuntimeProviders
