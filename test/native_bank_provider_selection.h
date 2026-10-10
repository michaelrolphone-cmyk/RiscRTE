#pragma once
#include "runtime/drivers/NativeProviderPolicyV1.h"
// Immutable host fixture selection. The digest pins the synthetic Xtensa ELF
// produced by elf("xTaskGetTickCount", "t5_driver_get") in native_bank_test.cpp.
namespace RuntimeProviders {
inline const NativeProviderPolicyV1& nativeBankFixturePolicy() {
  static const char* const imports[]={"xTaskGetTickCount"};
  static const NativeProviderPolicyV1 policy{
    "driver.elf","native-bank-provider","1.0.0","test.native-bank",1,1,1024,
    {0xc2,0x16,0xee,0xe7,0x30,0x74,0x3c,0x87,0x69,0x41,0xae,0x16,0x70,0x76,0x38,0x22,
     0x7b,0xf0,0x7f,0x7d,0x82,0xd7,0xac,0x32,0x0e,0xd4,0xb3,0xae,0xa4,0xb0,0xb7,0xe5},
    imports,1,nullptr,0};
  return policy;
}
#ifdef RISC_NATIVE_BANK_TEST_SELECT_POLICY
inline NativeProviderPolicySetV1 selectedNativeProviderPoliciesV1() {
  return {&nativeBankFixturePolicy(),1};
}
#endif
}
