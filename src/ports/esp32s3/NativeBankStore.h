#pragma once
#include <RiscBankStoreV1.h>
namespace RiscBoot { class Runtime; struct KeyValueBackend; }
namespace RiscCpu { struct Hardware; }
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
bool provisionAvailable(); // verified confirmed pair, owner, before Runtime binding
// Read-only inactive-journal history. SameAttempt/Unavailable keep the installed
// boot without erasing history. All-FF legacy trailer means no tracked attempt.
enum class ProvisionHistory {Clear,SameAttempt,Unavailable};
ProvisionHistory provisionHistory(const uint8_t (&digest)[32]);
// Private boot-owner API, deliberately absent from the provider capability.
// Requires a verified/confirmed paired deployment, before bind()/app startup.
// Uses a fresh metadata-only CPU/Runtime instance and production ELF admission.
// Hardware tables are copied; profile and KV backend remain immutable/alive
// through abort success or terminal restart. No caller-supplied admission hook.
int32_t provisionBegin(const RiscProvision::Profile&,const uint8_t (&digest)[32],const RiscCpu::Hardware&,
                       const RiscBoot::KeyValueBackend*,uint64_t*);
int32_t provisionStep(uint64_t,risc_bank_status_v1*);
bool provisionStatus(uint64_t,risc_bank_status_v1*); // read-only, including unsafe/ambiguous terminal state
int32_t provisionWrite(uint64_t,size_t,const void*,uint32_t);
int32_t provisionFinish(uint64_t);
int32_t provisionActivate(uint64_t);
int32_t provisionAbort(uint64_t);
bool provisionRestart(uint64_t);
}
