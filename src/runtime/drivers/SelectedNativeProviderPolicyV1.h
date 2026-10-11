#pragma once
#include "NativeProviderPolicyV1.h"
// Optional build-owned header must define
// RuntimeProviders::selectedNativeProviderPoliciesV1(). Its const policy and
// metadata must remain alive for the boot session. No package/boot file chooses
// this header; ordinary builds contain no selected native provider policy.
#ifdef RISC_NATIVE_PROVIDER_POLICY_HEADER
#include RISC_NATIVE_PROVIDER_POLICY_HEADER
#else
namespace RuntimeProviders {
inline NativeProviderPolicySetV1 selectedNativeProviderPoliciesV1() { return {}; }
}
#endif
