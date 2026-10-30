#include <RiscRuntimeV1.h>
#include "api.h"
#ifndef WORKER_RETAINED
#define WORKER_RETAINED 0
#endif
static worker_probe_result result={sizeof(result),0,0,0};
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* runtime=risc_runtime_get_api(1);
 if(!runtime||runtime->api_version!=1||runtime->struct_size<RISC_RUNTIME_CAPABILITIES_V1_SIZE||!runtime->diagnostic||!runtime->acquire||!runtime->release)return;
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
 if(!runtime->acquire("test.worker-native",1,1,&grant)){runtime->diagnostic("WORKER_GRAPH app=unqualified reason=acquire");return;}
 const worker_probe_api* api=grant.api;
 if(!api||api->api_version!=1||api->struct_size<sizeof(*api)||!api->hold||!api->finish||!api->hold(WORKER_RETAINED,&result)){runtime->diagnostic("WORKER_GRAPH app=unqualified reason=hold");return;}
 if(WORKER_RETAINED){runtime->diagnostic("WORKER_GRAPH app=retained-prepared");return;}
 if(!api->finish()||result.entered!=1||result.cancelled!=1||result.completed!=1){runtime->diagnostic("WORKER_GRAPH app=unqualified reason=join");return;}
 if(!runtime->release(&grant)){runtime->diagnostic("WORKER_GRAPH app=unqualified reason=release");return;}
 runtime->diagnostic("WORKER_GRAPH app=clean-passed");
}
