#pragma once
#include <RiscBankStoreV1.h>
namespace RiscBoot { class Runtime; }
namespace RiscProvision { struct Profile; }
namespace RiscBankStore {
// Paired target only. Boot verifies exact deployed table and selected pair before
// any mount/driver load. False never formats storage; pending failures rollback.
bool prepareBoot(bool (*owner)(),bool (*restartSafe)(),bool (*operationSafe)());
const char* bootLabel();
bool bind(RiscBoot::Runtime&);
bool confirmBoot();
void rejectBoot();
bool exitSafe();
// Private boot-owner API, deliberately absent from the provider capability.
// Requires a verified/confirmed paired deployment, before bind()/app startup.
// Admission must validate the complete graph and every ELF without executing.
// Profile/admission lifetime extends through abort success or terminal restart.
using ProvisionAdmission=bool (*)(const char*,const RiscProvision::Profile&);
int32_t provisionBegin(const RiscProvision::Profile&,const uint8_t (&digest)[32],ProvisionAdmission,uint64_t*);
int32_t provisionStep(uint64_t,risc_bank_status_v1*);
int32_t provisionWrite(uint64_t,size_t,const void*,uint32_t);
int32_t provisionFinish(uint64_t);
int32_t provisionActivate(uint64_t);
int32_t provisionAbort(uint64_t);
bool provisionRestart(uint64_t);
}
