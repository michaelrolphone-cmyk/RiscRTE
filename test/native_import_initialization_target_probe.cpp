#include "experimental/native_replica/CheckedNativeReplicaStore.h"
#include "experimental/native_replica/CheckedNvsLegacySources.h"
using C=RiscExperimental::NativeReplicaCore;
using A=RiscExperimental::NativeBaselineAdmission;
using S=RiscExperimental::CheckedNativeReplicaStore;
using L=RiscExperimental::CheckedNvsLegacySources;
extern "C" {
char native_import_core_size[sizeof(C)];
char native_import_admission_size[sizeof(C::Initialization)];
char native_import_receipt_size[sizeof(C::ImportInitReceipt)];
char native_import_store_size[sizeof(S)];
char native_import_sources_size[sizeof(L)];
int native_import_initialization_probe(C&core,S&store,A&admission,L&sources,const A::Selection&selection){
 C::Initialization attempt;C::ImportInitReceipt receipt;auto backend=store.backend();
 int rc=backend.readImportedInitialization(backend.context,&receipt);
 if(rc==C::Missing)rc=admission.admitImported(selection,&attempt);
 else if(!rc)rc=admission.admitImportedResume(&attempt);
 if(!rc)rc=core.initializeImportedBaseline(attempt);
 A::Witness witness{};auto api=sources.sources();
 if(!rc)rc=api.recheckImported(api.context,core.namespaceId(),receipt,witness);
 return rc;
}
}
