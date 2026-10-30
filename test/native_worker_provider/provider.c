#include <RiscProviderV2.h>
#include <RiscResidentWorkerV1.h>
#include <RiscHardwareConfigV1.h>
#include <string.h>
#include "api.h"
static const risc_resident_worker_api_v1* worker;
static uint64_t token;
static bool retained;
static void entry(void* argument,const risc_worker_control_v1* control){
 worker_probe_result* out=argument;out->entered=1;
 while(retained||!control->cancelled(control->context))control->yield_ms(control->context,1);
 out->cancelled=1;out->completed=1;
}
static bool hold(bool keep,worker_probe_result* out){
 if(!worker||token||!out||out->struct_size!=sizeof(*out))return false;
 retained=keep;out->entered=out->cancelled=out->completed=0;
 return worker->start(worker->context,entry,out,4096,1,-1,&token)==0;
}
static bool finish(void){
 if(!token)return true;
 if(worker->cancel(worker->context,token)||worker->join(worker->context,token,100)||worker->release(worker->context,token))return false;
 token=0;return true;
}
static const worker_probe_api api={1,sizeof(api),hold,finish};
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 const risc_hardware_device_v1* hardware=0;
 for(size_t i=0;i<count;++i){
  if(!strcmp(deps[i].capability_id,"hardware.device"))hardware=deps[i].api;
  if(!strcmp(deps[i].capability_id,RISC_RESIDENT_WORKER_CAPABILITY))worker=deps[i].api;
 }
 return hardware&&worker&&hardware->struct_size>=sizeof(*hardware)&&!strcmp(hardware->config_type,"gpio.bank")&&worker->api_version==1&&worker->struct_size>=sizeof(*worker)&&worker->is_owner&&worker->start&&worker->cancel&&worker->join&&worker->release&&worker->is_owner(worker->context);
}
static void stop(void){worker=0;}
static const risc_driver_v2 driver={2,sizeof(driver),"native-worker-probe","test.worker-native",1,&api,start,stop,finish};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:0;}
