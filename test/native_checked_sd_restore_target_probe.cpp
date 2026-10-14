#include "experimental/native_replica/CheckedSdNativeRestore.h"
using C=RiscExperimental::NativeReplicaCore;
using S=RiscExperimental::CheckedNativeReplicaStore;
using R=RiscExperimental::CheckedSdNativeRestore;
using K=RiscExperimental::CheckedSdSettingsCatalog;
extern "C" {
char native_sd_restore_core_size[sizeof(C)];
char native_sd_restore_admission_size[sizeof(C::Initialization)];
char native_sd_restore_receipt_size[sizeof(C::SdInitReceipt)];
char native_sd_restore_store_size[sizeof(S)];
char native_sd_restore_controller_size[sizeof(R)];
char native_sd_restore_catalog_size[sizeof(K)];
int native_sd_restore_probe(R&restore,K&catalog,RiscExperimental::NativeSdAttempt&attempt){
 RiscExperimental::CheckedSdReplicaReader::Candidate candidate;
 int rc=catalog.prepareRestoreNative(attempt);catalog.preparationWorkerStep();catalog.collectPreparation();catalog.step();
 if(!rc)rc=catalog.takeRestoreCandidate(&candidate);
 if(!rc)rc=restore.begin(attempt,candidate);
 restore.workerStep();RiscExperimental::NativeSdCompletion::Result result{};
 if(!rc)rc=restore.collect(&result);
 return rc;
}
}
