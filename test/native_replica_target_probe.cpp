#include "experimental/native_replica/NvsReplicaMetadata.h"
#include "experimental/native_replica/NativeReplicaJob.h"
#include "experimental/native_replica/CheckedSdReplicaCheckpoint.h"
#include "experimental/native_replica/MbedTlsReplicaDigest.h"
void replica_target_probe(RiscExperimental::CheckedNativeReplicaStore&store,
 RiscExperimental::NativeReplicaCore&core,RiscExperimental::NvsReplicaMetadata&metadata,
 RiscExperimental::NativeReplicaJob&job,RiscExperimental::CheckedSdReplicaCheckpoint::Guard guard,
 const risc_storage_volume_api_v1*volume) {
 auto b=store.backend();auto m=metadata.api();(void)b;(void)m;
 (void)core.load();(void)store.exitSafe();(void)job.workerStep();
 RiscExperimental::MbedTlsReplicaDigest hash;
 RiscExperimental::CheckedSdReplicaCheckpoint transport(hash.api(),guard);
 (void)transport.start(core,volume);transport.step();(void)transport.acknowledgeNative();
 transport.cancel();(void)transport.exitSafe();
}

#include "experimental/native_replica/CheckedSdReplicaReader.h"
void replica_reader_target_probe(RiscExperimental::NativeReplicaCore&core,
 RiscExperimental::CheckedSdReplicaReader::Guard guard,const risc_storage_volume_api_v1*volume){
 RiscExperimental::MbedTlsReplicaDigest hash;
 RiscExperimental::CheckedSdReplicaReader reader(core,hash.api(),guard);
 RiscExperimental::CheckedSdReplicaReader::Candidate candidate;
 (void)reader.start(volume);reader.step();(void)reader.take(&candidate);
 reader.cancel();(void)reader.exitSafe();(void)candidate.mapSize();
}

#include "experimental/native_replica/CheckedNativeExportRecovery.h"
void durable_replica_target_probe(RiscExperimental::NativeReplicaCore&core,
 RiscExperimental::CheckedSdReplicaCheckpoint::Guard guard,
 RiscExperimental::CheckedNativeExportRecovery::Quiescence quiet,
 const risc_storage_volume_api_v1*volume,const uint8_t*fresh){
 RiscExperimental::MbedTlsReplicaDigest hash;
 RiscExperimental::CheckedSdReplicaCheckpoint writer(hash.api(),guard);
 (void)writer.startDurable(core,volume,fresh);writer.step();
 (void)writer.acknowledgeNative();writer.cancel();(void)writer.exitSafe();
 RiscExperimental::CheckedNativeExportRecovery recovery(core,hash.api(),guard,quiet);
 (void)recovery.start(volume);recovery.step();(void)recovery.decision();
 (void)recovery.completeNative();recovery.cancel();(void)recovery.exitSafe();
}

#include "experimental/native_replica/CheckedLegacySettingsReader.h"
void legacy_reader_target_probe(RiscExperimental::NativeReplicaCore&core,
 RiscExperimental::CheckedLegacySettingsReader::Guard guard,const risc_storage_volume_api_v1*volume){
 RiscExperimental::MbedTlsReplicaDigest hash;
 RiscExperimental::CheckedLegacySettingsReader reader(core,hash.api(),guard);
 RiscExperimental::CheckedLegacySettingsReader::Candidate candidate;
 (void)reader.start(volume,RiscExperimental::CheckedLegacySettingsReader::Source::PagedV2);
 reader.step();(void)reader.take(&candidate);reader.cancel();(void)reader.exitSafe();
 (void)candidate.sourceDigest();(void)candidate.mapSize();
}

#include "experimental/native_replica/CheckedSdSettingsCatalog.h"
void settings_catalog_target_probe(RiscExperimental::NativeReplicaCore&core,
 RiscExperimental::CheckedSdSettingsCatalog::Guard guard,const risc_storage_volume_api_v1*volume){
 RiscExperimental::MbedTlsReplicaDigest hash;
 RiscExperimental::CheckedSdSettingsCatalog catalog(core,hash.api(),guard);
 RiscExperimental::CheckedSdSettingsCatalog::Report report;
 (void)catalog.start(volume);catalog.step();(void)catalog.take(&report);
 catalog.cancel();(void)catalog.exitSafe();(void)report.vacant();report.reset();
}

// Compile-only layout evidence. These functions/arrays are never installed.
using NativeCatalogProbe=RiscExperimental::CheckedSdSettingsCatalog;
extern "C" {
extern const unsigned char catalog_layout_object[sizeof(NativeCatalogProbe)];
extern const unsigned char catalog_layout_report[sizeof(NativeCatalogProbe::Report)];
extern const unsigned char catalog_layout_entry[NativeCatalogProbe::entryStorageBytes()];
extern const unsigned char catalog_layout_child[NativeCatalogProbe::childStorageBytes()];
const unsigned char catalog_layout_object[sizeof(NativeCatalogProbe)]={};
const unsigned char catalog_layout_report[sizeof(NativeCatalogProbe::Report)]={};
const unsigned char catalog_layout_entry[NativeCatalogProbe::entryStorageBytes()]={};
const unsigned char catalog_layout_child[NativeCatalogProbe::childStorageBytes()]={};
}

#include "experimental/native_replica/NativeBaselineAdmission.h"
void native_baseline_target_probe(void*owner,RiscExperimental::NativeReplicaCore&core,
 RiscExperimental::NativeBaselineAdmission::Guard guard,
 RiscExperimental::NativeBaselineAdmission::Sources sources,
 const RiscExperimental::NativeBaselineAdmission::Selection&selection){
 using C=RiscExperimental::NativeReplicaCore;using A=RiscExperimental::NativeBaselineAdmission;
 static_assert(C::HeaderV3==480&&C::ProvenanceBytes==96,"v3 envelope");
 RiscExperimental::MbedTlsReplicaDigest hash;auto h=hash.api();
 A factory(owner,core,guard,{h.context,h.begin,h.update,h.finish},sources);
 C::Initialization fresh,imported;
 (void)factory.admitFreshEmpty(&fresh);(void)core.initializeFreshEmpty(fresh);
 (void)factory.admitImported(selection,&imported);(void)core.initializeImportedBaseline(imported);
 (void)fresh.valid();(void)imported.mapSize();(void)core.baseline().archiveObligation();
 (void)core.pendingOnlyAdoptionAllowed();(void)factory.exitSafe();
}

void fresh_initialization_target_probe(void*owner,RiscExperimental::NativeReplicaCore&core,
 RiscExperimental::NativeBaselineAdmission::Guard guard,
 RiscExperimental::NativeBaselineAdmission::Sources sources,
 RiscExperimental::CheckedFreshInitArtifacts&inspector){
 using C=RiscExperimental::NativeReplicaCore;using A=RiscExperimental::NativeBaselineAdmission;
 A factory(owner,core,guard,{},sources);C::Initialization resume;
 (void)factory.admitFreshResume(&resume);(void)core.initializeFreshEmpty(resume);
 (void)inspector.inspectAbsent();(void)inspector.exitSafe();
}
using FreshCoreProbe=RiscExperimental::NativeReplicaCore;
using FreshFactoryProbe=RiscExperimental::NativeBaselineAdmission;
extern "C" {
extern const unsigned char fresh_layout_core[sizeof(FreshCoreProbe)];
extern const unsigned char fresh_layout_initialization[sizeof(FreshCoreProbe::Initialization)];
extern const unsigned char fresh_layout_receipt[sizeof(FreshCoreProbe::FreshInitReceipt)];
extern const unsigned char fresh_layout_factory[sizeof(FreshFactoryProbe)];
extern const unsigned char fresh_layout_store[sizeof(RiscExperimental::CheckedNativeReplicaStore)];
extern const unsigned char fresh_layout_inspector[sizeof(RiscExperimental::CheckedFreshInitArtifacts)];
const unsigned char fresh_layout_core[sizeof(FreshCoreProbe)]={};
const unsigned char fresh_layout_initialization[sizeof(FreshCoreProbe::Initialization)]={};
const unsigned char fresh_layout_receipt[sizeof(FreshCoreProbe::FreshInitReceipt)]={};
const unsigned char fresh_layout_factory[sizeof(FreshFactoryProbe)]={};
const unsigned char fresh_layout_store[sizeof(RiscExperimental::CheckedNativeReplicaStore)]={};
const unsigned char fresh_layout_inspector[sizeof(RiscExperimental::CheckedFreshInitArtifacts)]={};
}
