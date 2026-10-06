#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <TWatchPlatformV1.h>
#include "hci_test_api.h"
#include <string.h>
extern void test_hci_trace(const char*);
static const twatch_hci_controller_v1* raw;
static uint64_t token;
static bool enable(void){return raw && !token && raw->open(raw->context,0,&token);}
static bool disable(void){if(!token)return true;if(!raw->close(raw->context,token))return false;token=0;return true;}
static bool poll(void){uint8_t type,data[1028];size_t count;return raw && token && raw->receive(raw->context,token,&type,data,sizeof(data),&count,0);}
static const test_hci_api_v1 api={1,sizeof(api),enable,disable,poll};
__attribute__((constructor)) static void loaded(void){test_hci_trace("PROVIDER loaded");}
__attribute__((destructor)) static void unloaded(void){test_hci_trace("PROVIDER unloaded");}
static bool start(const risc_provider_dependency_v1* deps,size_t n){
 const risc_hardware_device_v1* hw=NULL;
 for(size_t i=0;i<n;++i){if(!strcmp(deps[i].capability_id,"hardware.device"))hw=deps[i].api;if(!strcmp(deps[i].capability_id,"platform.hci.controller"))raw=deps[i].api;}
 if(!hw || !raw || strcmp(hw->config_type,"radio.integrated") || raw->struct_size<sizeof(*raw))return false;
 const risc_hw_radio_v1* config=hw->config;if(config->unit || config->features!=1)return false;
 test_hci_trace("PROVIDER started");return true;
}
static bool quiesce(void){if(!disable())return false;test_hci_trace("PROVIDER quiesced");return true;}
static void stop(void){raw=NULL;test_hci_trace("PROVIDER stopped");}
static const risc_driver_v2 driver={2,sizeof(driver),"hci-probe","test.hci",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
