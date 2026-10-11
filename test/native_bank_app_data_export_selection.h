#pragma once
#include "runtime/update/NativeAppDataExportPolicyV1.h"
#include <RiscBuildIdentity.h>
namespace RiscUpdate {
  // Intentionally unrelated identities, namespaces and paths: the production
  // implementation has no application/product/file names compiled into it.
  static constexpr RiscStorage::AppDataExport::Entry nativeBankExportFiles[]={
    {"owner-a",41,"record.bin","/first/record.bin",true},
    {"owner-a",41,"view.bin","/first/view.bin",false},
    {"owner-b",52,"sample-a.bin","/second/sample-a.bin",true},
    {"owner-b",52,"sample-b.bin","/second/sample-b.bin",true},
    {"owner-b",52,"model.bin","/second/model.bin",true},
    {"owner-c",63,"sample-a.bin","/third/sample-a.bin",true},
    {"owner-c",63,"sample-b.bin","/third/sample-b.bin",true},
    {"owner-d",74,"index.bin","/fourth/index.bin",true},
    {"owner-d",74,"ledger.bin","/fourth/ledger.bin",true}};
  static constexpr RiscBoot::Runtime::AppDataExportDelegation nativeBankExportMaps[]={
    {"export-browser","Fixture files",nativeBankExportFiles,sizeof(nativeBankExportFiles)/sizeof(nativeBankExportFiles[0])}};
inline constexpr NativeAppDataExportPolicyV1 nativeBankExportFixturePolicy() {
  return {{"fixture-product","2.0.1","fixture/product","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",RISC_BUILD_VERSION},
          {"fixture-product","2.0.2","fixture/product","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",RISC_BUILD_VERSION},nativeBankExportMaps,1};
}
#ifdef RISC_NATIVE_BANK_TEST_SELECT_EXPORT_POLICY
inline constexpr NativeAppDataExportPolicyV1 selectedNativeAppDataExportPolicyV1() { return nativeBankExportFixturePolicy(); }
#endif
}
