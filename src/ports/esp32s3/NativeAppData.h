#pragma once
#include "bootstrap/AppDataBackend.h"
namespace RiscAppData {
// Mount only the exact separately provisioned partition. No format or grow.
bool prepare(bool (*owner)(),bool (*operationSafe)());
const RiscBoot::AppDataBackend* backend();
// Copied async work owns its bytes and may coexist with provider acquisition.
bool custodySafe();
bool exitSafe();
}
