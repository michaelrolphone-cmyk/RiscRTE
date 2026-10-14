// Compile-only layout and reachable-call probe. Never link into firmware.
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "experimental/native_replica/NativeSdSynchronization.h"
#include "experimental/native_replica/CheckedFirstSdCheckpoint.h"
#include "experimental/native_replica/CheckedSdNativeRestore.h"
#include "experimental/native_replica/CheckedNvsLegacySources.h"
#include "experimental/native_replica/NvsReplicaMetadata.h"
#include "experimental/native_replica/MbedTlsReplicaDigest.h"
using namespace RiscExperimental;
#define SIZE(type, label) unsigned char resource_##label[sizeof(type)]
extern "C" {
SIZE(NativeReplicaCore, core); SIZE(CheckedNativeReplicaStore, store); SIZE(RiscStorage::AppDataFiles, files);
SIZE(NvsReplicaMetadata, metadata); SIZE(NativeSettingsCoordinator, coordinator); SIZE(NativeAtomicKeyValue, atomic);
SIZE(NativeSdSynchronization, synchronization); SIZE(CheckedSdSettingsCatalog, catalog); SIZE(CheckedSdSettingsCatalog::Entry, catalog_entry);
SIZE(CheckedFirstSdCheckpoint, first); SIZE(CheckedSdReplicaCheckpoint, checkpoint); SIZE(CheckedNativeExportRecovery, recovery);
SIZE(CheckedSdNativeRestore, restore); SIZE(NativeBaselineAdmission, admission); SIZE(NativeReplicaCore::Initialization, initialization);
SIZE(CheckedNvsLegacySources, import_sources); SIZE(CheckedFreshInitArtifacts, artifacts);
SIZE(NativeSdPortAttempt, port_attempt); SIZE(NativeSdAttempt, attempt); SIZE(NativeRuntimeStoragePin, runtime_pin);
SIZE(NativeReplicaCore::Snapshot, snapshot); SIZE(CheckedSdReplicaReader, reader); SIZE(CheckedLegacySettingsReader, legacy_reader);
SIZE(MbedTlsReplicaDigest, digest);
SIZE(RiscBoot::Runtime, runtime); SIZE(RiscCpu::Port, cpu_port);
unsigned char resource_catalog_default_scan_entries[2*128*sizeof(CheckedSdSettingsCatalog::Entry)];
unsigned char resource_catalog_default_restore_entries[(2*128+CheckedSdSettingsCatalog::HistoryWindow)*sizeof(CheckedSdSettingsCatalog::Entry)];
unsigned char resource_core_image_limit[CheckedNativeReplicaStore::ImageMax];
unsigned char resource_map_max[CheckedSdReplicaCheckpoint::MapMax];
}
int resource_atomic_submit(NativeAtomicKeyValue&a,uint64_t revision,const risc_key_value_atomic_mutation_v1*m,uint32_t count,const void*i,uint32_t bytes,uint64_t*token){return a.submit(revision,m,count,i,bytes,token);}
bool resource_worker_step(NativeSettingsCoordinator&c){return c.workerStep();}
int resource_sync_poll(NativeSdSynchronization&s){return s.poll();}
