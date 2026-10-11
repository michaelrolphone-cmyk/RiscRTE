#include <RiscProviderV2.h>
#include <string.h>
extern bool test_startup_admit(void);
static bool start(const risc_provider_dependency_v1*d,size_t n){(void)d;(void)n;return test_startup_admit();}
extern bool test_startup_cleanup(void);
static bool quiesce(void){return test_startup_cleanup();}
static void stop(void){}
#ifdef STARTUP_EXTERNAL_DETAIL
extern const char* test_startup_detail(void);
#endif
static bool detail(char*out,size_t n){
#ifdef STARTUP_EXTERNAL_DETAIL
 const char*s=test_startup_detail();
#else
 const char*s="primary probe pullup denied";
#endif
 if(!out||!n)return false;
 size_t i=0;while(s[i]&&i+1<n){out[i]=s[i];++i;}out[i]=0;return true;
}
static const unsigned api[]={1,8};
static const risc_driver_diagnostics_v2 driver={{2,sizeof(driver),"startup-failure","test.startup",1,api,start,stop,quiesce},detail};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver.base:0;}
