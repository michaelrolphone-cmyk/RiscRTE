#include <RiscProviderV2.h>
#include <assert.h>
extern const void* x4_serial_power(void);
static bool start(const risc_provider_dependency_v1*d,size_t n){assert(n==1&&d);return true;}
static void stop(void){}
static bool quiesce(void){return true;}
static risc_driver_v2 driver={2,sizeof(driver),"serial-test-power","board.power.ready",1,0,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){if(abi!=2)return 0;driver.capability=x4_serial_power();return &driver;}
