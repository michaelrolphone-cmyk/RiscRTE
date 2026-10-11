#pragma once
#include "ProviderGraphV2.h"
#include "NativeProviderPolicyV1.h"

namespace RuntimePackages {
// Trusted compiled-in caller only, absent from all ELF symbol tables. The
// separately supplied policy is native authority, never package metadata.
class DeviceProviderExecutorV2 final {
 public:
  static bool registerNativePolicy(RuntimeProviders::GraphV2& graph,
      const RuntimeProviders::SpecV2& candidate,
      const RuntimeProviders::NativeProviderPolicyV1& policy) {
    return graph.addManagerValidatedPrivileged(candidate,policy);
  }
};
}
