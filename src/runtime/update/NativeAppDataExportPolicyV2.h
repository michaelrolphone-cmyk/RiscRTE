#pragma once
#include "NativeAppDataExportPolicyV1.h"
namespace RiscUpdate {
// Native-owned, versioned authority. All strings/arrays have static lifetime.
// Before/after are exact boot app_capabilities snapshots, not patches. They
// include operational grants and maps; source/target identities bind manifests.
// No storage bytes are rewritten. Bank selection remains the transaction.
struct NativeSharedKeyValueV2 {
  const char* consumer=nullptr;
  uint32_t api=0, nameSpace=0;
  const char* const* previousOwners=nullptr;
  size_t previousOwnerCount=0;
};
struct NativeAppDataExportPolicyV2 {
  uint32_t schemaVersion=2;
  const char* migrationId=nullptr;
  NativeAppDataExportCohortV1 from{},to{};
  const char* before=nullptr;
  const char* after=nullptr;
  const NativeSharedKeyValueV2* sharedKeyValue=nullptr;
  size_t sharedKeyValueCount=0;
};
}
