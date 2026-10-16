#pragma once
#ifndef RISC_NATIVE_PROVISION_REQUEST
#define RISC_NATIVE_PROVISION_REQUEST 0
#endif
#if RISC_NATIVE_PROVISION_REQUEST
#include "bootstrap/Runtime.h"
#include "runtime/provisioning/BootstrapInput.h"
namespace RiscProvisionRequest {
bool bind(RiscBoot::Runtime&,bool(*owner)());
bool bindAdmission(RiscBoot::Runtime&);
// A nonempty invalid/unavailable request blocks owner-input fallback. It never
// blocks normal installed-store admission when the existing boot is safe.
RiscProvision::Input bootInput(RiscProvision::Input ownerInput,bool& preserving);
void releaseBootInput();
}
#endif
