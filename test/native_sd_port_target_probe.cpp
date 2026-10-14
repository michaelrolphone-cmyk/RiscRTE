#include "experimental/native_replica/NativeSdPortAttempt.h"
#include <cstddef>
using Pin=RiscExperimental::NativeRuntimeStoragePin;
using Attempt=RiscExperimental::NativeSdAttempt;
using Adapter=RiscExperimental::NativeSdPortAttempt;
static_assert(offsetof(risc_storage_volume_api_v1_admission,base)==0,"frozen download prefix");
static_assert(offsetof(risc_storage_volume_api_v1_admission,admission_tag)==sizeof(risc_storage_volume_api_v1_download),"append-only admission suffix");
extern "C" {
unsigned char native_sd_graph_pin_size[sizeof(Pin)];
unsigned char native_sd_port_attempt_size[sizeof(Adapter)];
unsigned char native_sd_attempt_size[sizeof(Attempt)];
}
void native_sd_port_probe(RiscBoot::Runtime&r,RiscCpu::Port&p,Attempt::Profile profile){
 Adapter a(r,p,3,55,profile);(void)a.acquire();
 if(auto*inner=a.attempt()){(void)inner->refresh();risc_storage_async_result_v1 result{};(void)inner->collect(&result);(void)inner->close();}
 (void)a.release();(void)a.exitSafe();
}
