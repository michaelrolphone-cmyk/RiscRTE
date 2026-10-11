#pragma once
#include "bootstrap/Runtime.h"
#include "SleepDiagnostics.h"
namespace RiscDiagnostics {
// Shared by live boot and metadata-only provisioning admission. Looking up the
// optional table never reads a record, touches storage or starts a provider.
inline bool bindNativeSource(RiscBoot::Runtime& runtime){
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
  if(const auto* source=nativeSource())
    return runtime.registerPlatform(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,RISC_DIAGNOSTIC_SOURCE_API_V1,
                                    RiscBoot::Runtime::Scope::Global,0,source);
#else
  (void)runtime;
#endif
  return true;
}
}
