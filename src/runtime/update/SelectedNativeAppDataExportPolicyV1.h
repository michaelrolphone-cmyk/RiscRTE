#pragma once
#include "NativeAppDataExportPolicyV1.h"
// Only the native build selects this header. It must define
// constexpr RiscUpdate::selectedNativeAppDataExportPolicyV1(), returning immutable policy
// records and strings that remain alive for the whole boot session. Neither
// boot JSON nor an app/provider package can select or populate this authority.
#ifdef RISC_NATIVE_APP_DATA_EXPORT_POLICY_HEADER
#include RISC_NATIVE_APP_DATA_EXPORT_POLICY_HEADER
#else
namespace RiscUpdate {
inline constexpr NativeAppDataExportPolicyV1 selectedNativeAppDataExportPolicyV1() { return {}; }
}
#endif
