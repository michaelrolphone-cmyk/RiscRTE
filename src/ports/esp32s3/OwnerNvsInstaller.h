#pragma once
#include "runtime/provisioning/Installer.h"
namespace RiscBootstrap {
// Explicit maintenance caller only. Not called from setup, exported to ELF apps,
// or reachable over a network. Caller must keep ordinary Runtime quiescent.
RiscProvision::InstallResult installOwnerNvs(const void*,uint32_t,const void*,uint32_t,
                                            void*,uint32_t,bool (*ownerSafe)());
}
