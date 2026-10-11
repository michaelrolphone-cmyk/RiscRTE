#pragma once
#include "NativeAppDataExportPolicyV2.h"
#ifdef RISC_NATIVE_APP_DATA_EXPORT_POLICY_V2_HEADER
#include RISC_NATIVE_APP_DATA_EXPORT_POLICY_V2_HEADER
#else
namespace RiscUpdate {
inline constexpr NativeAppDataExportPolicyV2 selectedNativeAppDataExportPolicyV2() { return {}; }
}
#endif
