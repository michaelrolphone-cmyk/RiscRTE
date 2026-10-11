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
