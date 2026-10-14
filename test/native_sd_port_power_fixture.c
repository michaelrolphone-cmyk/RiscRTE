/* Board power-ready precondition only; no storage/custody behavior. */
#include <RiscProviderV2.h>
#include X4_POWER_READY_HEADER
static bool ready(void*c){(void)c;return true;}
static const x4_power_ready_api_v1 api={1,sizeof(api),NULL,ready};
static bool start(const risc_provider_dependency_v1*d,size_t n){return n==1&&d&&d[0].api;}
static void stop(void){}
static bool quiesce(void){return true;}
static const risc_driver_v2 driver={2,sizeof(driver),"test-power","board.power.ready",1,&api,start,stop,quiesce};
const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
