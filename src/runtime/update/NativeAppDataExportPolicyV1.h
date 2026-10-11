#pragma once
#include "bootstrap/Runtime.h"
#include <cstddef>
namespace RiscUpdate {
// Build-owned cohort coordinates. Firmware hashes are verified by the paired
// bank transaction, not embedded here (which would self-reference the image).
struct NativeAppDataExportCohortV1 {
  const char* product=nullptr;
  const char* version=nullptr;
  const char* sourceRepo=nullptr;
  const char* sourceRevision=nullptr;
  const char* runtimeVersion=nullptr;
};
struct NativeAppDataExportPolicyV1 {
  NativeAppDataExportCohortV1 from{},to{};
  const RiscBoot::Runtime::AppDataExportDelegation* delegations=nullptr;
  size_t count=0;
};
}
