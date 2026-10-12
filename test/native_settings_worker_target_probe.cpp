#include "ports/esp32s3/NativeSettingsWorker.h"
static_assert(RISC_NATIVE_SETTINGS_WORKER==1,"probe must select native worker");
extern "C" unsigned char native_settings_coordinator_size[sizeof(RiscExperimental::NativeSettingsCoordinator)];
unsigned char native_settings_coordinator_size[sizeof(RiscExperimental::NativeSettingsCoordinator)];
bool native_settings_worker_probe(RiscExperimental::NativeReplicaCore&core,RiscExperimental::CheckedNativeReplicaStore&store,bool(*owner)(),RiscExperimental::NativeSettingsCoordinator::NativeClient client){
 using namespace RiscCpu::NativeSettingsWorker;
 if(!prepare(core,store,owner))return false;
 auto*c=coordinator();if(!c)return false;
 uint32_t slot=0;if(c->registerNative(client,&slot)!=0||c->activate()!=0)return false;
 auto&endpoint=c->endpoint();auto api=endpoint.api();(void)api;
 RiscExperimental::NativeSettingsCoordinator::Lease lease;
 (void)c->acquireNative(slot,&lease);(void)c->releaseNative(lease);
 return exitSafe()&&!isWorker()&&!workerWithLease();
}

bool native_settings_worker_two_namespace_probe(RiscExperimental::NativeReplicaCore&preferences,RiscExperimental::CheckedNativeReplicaStore&preferencesStore,RiscExperimental::NativeReplicaCore&wifi,RiscExperimental::CheckedNativeReplicaStore&wifiStore,bool(*owner)()){
 using namespace RiscCpu::NativeSettingsWorker;
 if(!prepare(1,preferences,preferencesStore,owner)||!prepare(6,wifi,wifiStore,owner))return false;
 auto*first=coordinator(1);auto*second=coordinator(6);
 return first&&second&&first!=second&&!coordinator()&&!workerWithLease()&&!isWorker()&&!workerWithLease(1)&&!workerWithLease(6)&&exitSafe();
}
