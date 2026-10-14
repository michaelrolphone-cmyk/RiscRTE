#include "experimental/native_replica/CheckedNativeExportRecovery.h"
#include "experimental/native_replica/CheckedLegacySettingsReader.h"
#include "experimental/native_replica/CheckedSdSettingsCatalog.h"
using C=RiscExperimental::NativeReplicaCore;
using A=RiscExperimental::NativeSdAttempt;
using T=RiscExperimental::CheckedSdReplicaCheckpoint;
using R=RiscExperimental::CheckedSdReplicaReader;
using L=RiscExperimental::CheckedLegacySettingsReader;
using K=RiscExperimental::CheckedSdSettingsCatalog;
using E=RiscExperimental::CheckedNativeExportRecovery;
extern "C" {
unsigned char native_sd_session_size[sizeof(RiscExperimental::NativeSdSession)];
unsigned char native_sd_preparation_size[sizeof(RiscExperimental::NativeSdPreparation)];
unsigned char native_sd_writer_size[sizeof(T)];
unsigned char native_sd_reader_size[sizeof(R)];
unsigned char native_sd_legacy_size[sizeof(L)];
unsigned char native_sd_catalog_size[sizeof(K)];
unsigned char native_sd_recovery_size[sizeof(E)];
}
template<class Job>void prepareProbe(Job&job){
 (void)job.preparationWorkerStep();(void)job.collectPreparation();job.step();job.cancel();(void)job.state();(void)job.exitSafe();
}
template<class Job>void completionProbe(Job&job){
 (void)job.issueNativeCompletion();(void)job.nativeCompletionWorkerStep();(void)job.collectNativeCompletion();job.cancel();
}
void native_sd_consumers_probe(C&core,A&attempt,T::Digest hash,T::Guard guard,E::Quiescence quiet,const uint8_t*incarnation){
 T writer(hash,guard);(void)writer.prepareNative(core,attempt,true,incarnation);prepareProbe(writer);completionProbe(writer);
 R reader(core,hash,guard);(void)reader.prepareNative(attempt);prepareProbe(reader);R::Candidate candidate;(void)reader.take(&candidate);
 L legacy(core,hash,guard);(void)legacy.prepareNative(attempt,L::Source::PagedV2);prepareProbe(legacy);L::Candidate old;(void)legacy.take(&old);
 K catalog(core,hash,guard);(void)catalog.prepareNative(attempt);prepareProbe(catalog);K::Report report;(void)catalog.take(&report);
 E recovery(core,hash,guard,quiet);(void)recovery.prepareNative(attempt);prepareProbe(recovery);completionProbe(recovery);
}
