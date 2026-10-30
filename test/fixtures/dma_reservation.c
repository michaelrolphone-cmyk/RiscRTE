#include <RiscProviderV2.h>
#include <RiscDmaReservationV1.h>
#include <RiscHardwareConfigV1.h>
#include "dma_reservation_api.h"
#include <string.h>
extern void test_dma_trace(const char*);
extern const char* test_dma_mode(void);
static const risc_dma_reservation_api_v1* worker;
static uint64_t token;
static void handler(void*argument){(void)argument;}
static bool hold(void){
 risc_dma_channel_v1 channel={.struct_size=sizeof(channel)};
 return worker&&!token&&worker->reserve_tx(worker->context,RISC_DMA_LCD0,&token,&channel)==RISC_DMA_OK&&worker->irq_claim(worker->context,token,handler,NULL)==RISC_DMA_OK;
}
static bool release(void){
 if(!token)return true;
 if(worker->irq_enable(worker->context,token,false)||worker->irq_retire(worker->context,token)||worker->release(worker->context,token))return false;
 token=0;return true;
}
static const test_dma_api api={1,sizeof(api),hold,release};
__attribute__((destructor)) static void unloaded(void){test_dma_trace("provider-unloaded");}
static bool start(const risc_provider_dependency_v1*deps,size_t n){
 const risc_hardware_device_v1*hardware=NULL;
 for(size_t i=0;i<n;++i){
  if(!strcmp(deps[i].capability_id,"hardware.device"))hardware=deps[i].api;
  if(!strcmp(deps[i].capability_id,RISC_DMA_RESERVATION_CAPABILITY))worker=deps[i].api;
 }
 return hardware&&worker&&!strcmp(hardware->config_type,"gpio.bank")&&worker->api_version==1&&worker->struct_size>=sizeof(*worker)&&worker->is_owner(worker->context);
}
static bool quiesce(void){return release();}
static void stop(void){worker=NULL;test_dma_trace("provider-stopped");}
static const risc_driver_v2 driver={2,sizeof(driver),"worker-probe","test.dma",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
