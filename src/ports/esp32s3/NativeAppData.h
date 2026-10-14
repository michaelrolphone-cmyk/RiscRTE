#pragma once
#include "bootstrap/AppDataBackend.h"
#ifndef RISC_NATIVE_PACKAGE_VOLUME
#define RISC_NATIVE_PACKAGE_VOLUME 0
#endif
namespace RiscAppData {
// Mount only the exact separately provisioned partition. No format or grow.
bool prepare(bool (*owner)(),bool (*operationSafe)());
const RiscBoot::AppDataBackend* backend();
#if RISC_NATIVE_PACKAGE_VOLUME
// Register one permanent native worker pump; no provider-owned callback.
bool registerPackageWorker(void (*pump)());
bool onWorker();
#endif
// Copied async work owns its bytes and may coexist with provider acquisition.
bool custodySafe();
bool exitSafe();
}
