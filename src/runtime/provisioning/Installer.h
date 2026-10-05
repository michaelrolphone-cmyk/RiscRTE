#pragma once
#include "BootstrapInput.h"
namespace RiscProvision {
// Trusted owner-invoked transport only; never exposed as an app capability.
// set/commit may have taken effect on failure. Reader must see committed state.
struct InstallTransport {Input input;bool (*set)(void*,const char*,const void*,uint32_t);bool (*commit)(void*);};
enum class InstallResult {Installed,Unchanged,InvalidInput,InvalidExisting,Unavailable,StageFailed,SelectionUnknown};
// Side-effect-free preflight, including allocation/size/schema checks.
bool validInstallInput(const void*,uint32_t,const void*,uint32_t);
// Caller supplies a private 16KiB scratch buffer; cleared on every return.
InstallResult install(InstallTransport,const void* profile,uint32_t profileSize,
                      const void* time,uint32_t timeSize,void* scratch,uint32_t capacity);
}
