#include "bootstrap/Runtime.h"
extern "C" {
char measure_Port[sizeof(RiscBoot::Port)];
char measure_Runtime[sizeof(RiscBoot::Runtime)];
char measure_Graph[sizeof(RuntimeProviders::GraphV2)];
char measure_Grant[sizeof(RuntimeProviders::GrantV2)];
char measure_Providers[RuntimeProviders::GraphV2::kMaxModules];
char measure_Grants[RuntimeProviders::GraphV2::kMaxGrants];
#if RISC_NATIVE_SERIAL_FILES
char measure_BindingPrepareVolumeOffsetPlus1[__builtin_offsetof(RiscBoot::Runtime::NativeFilesBinding,prepareVolume)+1];
char measure_BindingSleepOffsetPlus1[__builtin_offsetof(RiscBoot::Runtime::NativeFilesBinding,sleep)+1];
#if QUALIFY_CANDIDATE
char measure_BindingLifetime[sizeof(RiscBoot::Runtime::NativeBindingLifetime)];
char measure_BindingLifetimeOffsetPlus1[__builtin_offsetof(RiscBoot::Runtime::NativeFilesBinding,lifetime)+1];
char measure_NativeState[sizeof(RiscBoot::Runtime::NativeFilesState)];
char measure_PortStopOffsetPlus1[__builtin_offsetof(RiscBoot::Port,serialFilesStop)+1];
#endif
char measure_NativeFilesBinding[sizeof(RiscBoot::Runtime::NativeFilesBinding)];
char measure_NativeFilesBindings[sizeof(RiscBoot::Runtime::nativeFiles_)];
#endif
}
