#include <RiscProviderV2.h>
extern void runtime_fixture_dependency(unsigned);
struct Dep { unsigned magic; int (*probe)(void); };
static int probe(void){return 0x7341;}
static struct Dep api={0x7341,probe};
static bool start(const risc_provider_dependency_v1* d,size_t n){(void)d;if(n)return false;runtime_fixture_dependency(1);return true;}
static bool quiet(void){runtime_fixture_dependency(2);return true;}
static void stop(void){runtime_fixture_dependency(3);}
static const risc_driver_v2 driver={2,sizeof(driver),"fixture-dependency","test.dep",1,&api,start,stop,quiet};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(unsigned abi){return abi==2?&driver:0;}
