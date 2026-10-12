#include "experimental/native_replica/CheckedNvsLegacySources.h"
using Sources=RiscExperimental::CheckedNvsLegacySources;
using A=RiscExperimental::NativeBaselineAdmission;
extern "C" {
char native_nvs_legacy_sources_size[sizeof(Sources)];
int native_nvs_legacy_sources_probe(Sources&s,uint32_t ns,const A::Selection&selection,const A::Rows&rows,A::Witness*out){
 auto api=s.sources();int rc=s.inventory(out);
 if(!rc)rc=api.materialize(api.context,ns,selection,rows,out);
 return rc?rc:api.recheck(api.context,ns,*out);
}
}
