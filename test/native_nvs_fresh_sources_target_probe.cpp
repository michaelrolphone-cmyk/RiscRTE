#include "experimental/native_replica/CheckedNvsFreshSources.h"
using Sources=RiscExperimental::CheckedNvsFreshSources;
using A=RiscExperimental::NativeBaselineAdmission;
using C=RiscExperimental::NativeReplicaCore;
extern "C" {
char native_nvs_fresh_sources_size[sizeof(Sources)];
int native_nvs_fresh_sources_probe(Sources&s,uint32_t ns,const C::FreshInitReceipt&r,A::Witness*out){
 auto a=s.sources();int rc=a.freshInventory(a.context,ns,out);
 if(rc)rc=a.resumeFreshInventory(a.context,ns,r,out);
 return rc?rc:a.recheck(a.context,ns,*out);
}
}
