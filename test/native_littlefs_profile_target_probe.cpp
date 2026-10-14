#include "experimental/native_replica/CheckedNativeReplicaStore.h"
using I=RiscExperimental::CheckedLittleFsFreshInitArtifacts;
extern "C" int checked_littlefs_profile_target(I&i,const uint8_t*p){return i.inspectAbsent()+i.inspectAttempt(p,484,1);}
extern "C" int checked_littlefs_import_target(I&i,const uint8_t*p,unsigned n){return i.inspectImportedAttempt(p,n,1);}
extern "C" int checked_littlefs_sd_target(I&i,const uint8_t*p,unsigned n){return i.inspectSdAttempt(p,n,1);}
extern "C" const unsigned littlefs_profile_sizes[]={sizeof(I),sizeof(RiscExperimental::FreshInitArtifactInspector),sizeof(RiscExperimental::CheckedFreshInitArtifacts)};
