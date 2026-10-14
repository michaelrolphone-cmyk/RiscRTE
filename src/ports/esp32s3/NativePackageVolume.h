#pragma once
#ifndef RISC_NATIVE_PACKAGE_VOLUME
#define RISC_NATIVE_PACKAGE_VOLUME 0
#endif
#if RISC_NATIVE_PACKAGE_VOLUME
#include "bootstrap/Runtime.h"
namespace RiscPackageVolume {
bool bind(RiscBoot::Runtime&,bool(*owner)());
bool exitSafe();
// Side-effect-free capability shape for whole-store graph admission.
bool bindAdmission(RiscBoot::Runtime&);
}
#endif
