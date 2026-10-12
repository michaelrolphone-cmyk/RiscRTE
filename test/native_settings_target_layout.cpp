#include "src/ports/esp32s3/NativeAppDataMount.h"
#pragma push_macro("B110")
#pragma push_macro("B1000000")
#undef B110
#undef B1000000
#include "src/ports/esp32s3/NativeSettingsAssembly.cpp"
#include "src/ports/esp32s3/NativeSettingsWorker.cpp"
#include "src/runtime/storage/AsyncAppDataReplace.h"
using namespace RiscExperimental;
using namespace RiscCpu;
#define SIZE(name,type) extern "C" {char resource_##name[sizeof(type)];}
SIZE(task_control,StaticTask_t)
SIZE(appdata_async,RiscStorage::AsyncAppDataReplace)
SIZE(assembly,NativeSettingsAssembly::State)
SIZE(maintenance,NativeSettingsMaintenance)
SIZE(bootstrap,NativeSettingsBootstrap)
SIZE(catalog,CheckedSdSettingsCatalog)
SIZE(reader,CheckedSdReplicaReader)
SIZE(legacy,CheckedLegacySettingsReader)
SIZE(checkpoint,CheckedSdReplicaCheckpoint)
SIZE(first,CheckedFirstSdCheckpoint)
SIZE(recovery,CheckedNativeExportRecovery)
SIZE(sync,NativeSdSynchronization)
SIZE(package_request,risc_storage_package_request_v1)
SIZE(async_result,risc_storage_async_result_v1)
SIZE(preparation,NativeSdPreparation)
SIZE(completion,NativeSdCompletion)
SIZE(worker_binding,NativeSettingsWorker::Binding)
SIZE(worker_registry,NativeSettingsWorker::Registry)
SIZE(coordinator,NativeSettingsCoordinator)
SIZE(atomic,NativeAtomicKeyValue)
SIZE(runtime,RiscBoot::Runtime)
SIZE(cpu_port,RiscCpu::Port)
SIZE(catalog_child,decltype(CheckedSdSettingsCatalog::childBytes_))
SIZE(bootstrap_storage,decltype(NativeSettingsBootstrap::storage_))
SIZE(sync_storage,decltype(NativeSdSynchronization::storage_))

#pragma pop_macro("B1000000")
#pragma pop_macro("B110")
