#include <RiscProviderV2.h>
#include <RiscResidentWorkerV1.h>
#include <RiscHardwareConfigV1.h>
#include "resident_worker_api.h"
#include <string.h>
extern void test_worker_trace(const char*);
extern const char* test_worker_mode(void);
static const risc_resident_worker_api_v1* worker;
static uint64_t token;
static void entry(void*argument,const risc_worker_control_v1*c){
 (void)argument;
 const bool retained=!strcmp(test_worker_mode(),"retained");
 while(retained||!c->cancelled(c->context))c->yield_ms(c->context,1);
}
static bool hold(void){return worker&&!token&&worker->start(worker->context,entry,NULL,4096,1,-1,&token)==RISC_WORKER_OK;}
static bool release(void){
 if(!token)return true;
 if(worker->cancel(worker->context,token)||worker->join(worker->context,token,20)||worker->release(worker->context,token))return false;
 token=0;return true;
}
static const test_worker_api api={1,sizeof(api),hold,release};
__attribute__((destructor)) static void unloaded(void){test_worker_trace("provider-unloaded");}
static bool start(const risc_provider_dependency_v1*deps,size_t n){
 const risc_hardware_device_v1*hardware=NULL;
 for(size_t i=0;i<n;++i){
  if(!strcmp(deps[i].capability_id,"hardware.device"))hardware=deps[i].api;
  if(!strcmp(deps[i].capability_id,RISC_RESIDENT_WORKER_CAPABILITY))worker=deps[i].api;
 }
 return hardware&&worker&&!strcmp(hardware->config_type,"gpio.bank")&&worker->api_version==1&&worker->struct_size>=sizeof(*worker)&&worker->is_owner(worker->context);
}
static bool quiesce(void){return release();}
static void stop(void){worker=NULL;test_worker_trace("provider-stopped");}
static const risc_driver_v2 driver={2,sizeof(driver),"worker-probe","test.worker",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
