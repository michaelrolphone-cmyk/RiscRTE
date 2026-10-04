#pragma once
#include <RiscBankStoreV1.h>
namespace RiscBoot { class Runtime; }
namespace RiscBankStore {
// Paired target only. Boot verifies exact deployed table and selected pair before
// any mount/driver load. False never formats storage; pending failures rollback.
bool prepareBoot(bool (*owner)(),bool (*restartSafe)(),bool (*operationSafe)());
const char* bootLabel();
bool bind(RiscBoot::Runtime&);
bool confirmBoot();
void rejectBoot();
bool exitSafe();
}
