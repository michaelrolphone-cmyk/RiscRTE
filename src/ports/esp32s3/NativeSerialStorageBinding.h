#pragma once
#include "NativeSerialStorage.h"
#if RISC_NATIVE_SERIAL_SD
#include "bootstrap/Runtime.h"
namespace RiscCpu { namespace NativeSerialStorage {
// Shared actual startup path; complete Runtime graph admission still validates
// provider, instance, storage.volume@1 and unique selection before activation.
// No lookup/fallback based on board identity, filesystem or app authority.
// This permanent service explicitly survives clean run/session reset. Final
// Runtime stop closes the endpoint before retiring its exact graph grant.
inline bool bindSd(RiscBoot::Runtime& runtime,NativeSerialFiles::Check owner,NativeSerialFiles::Check safe){
  return admission() && runtime.bindNativeFiles(Detail::sdProvider,RISC_NATIVE_SERIAL_SD_INSTANCE,
                                               sdFiles(),owner,safe,true,RiscBoot::Runtime::NativeBindingLifetime::Firmware);
}
} }
#endif
