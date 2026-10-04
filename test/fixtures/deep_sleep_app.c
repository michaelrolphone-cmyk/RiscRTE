#include <RiscRuntimeV1.h>
#include <stdint.h>
static unsigned runs;
static const risc_runtime_api_v1 *rt;
__attribute__((visibility("default"))) int app_module_init(void){rt=risc_runtime_get_api(1);if(rt)rt->diagnostic("APP init");return rt?0:-1;}
__attribute__((visibility("default"))) void app_module_fini(void){if(rt)rt->diagnostic("APP fini");}
__attribute__((visibility("default"))) void app_main(void){
 if(!rt || ++runs!=1){if(rt)rt->diagnostic("APP stale-state");return;}
 risc_runtime_health_v1 health={.struct_size=sizeof(health)};
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
 if(!rt->health(&health) || !rt->acquire("test.deep",1,7,&grant)){rt->diagnostic("APP denied");return;}
 const struct {uint32_t version,size;int32_t(*enter)(void);} *api=grant.api;
 if(health.uptime_ms==0){rt->diagnostic("APP fresh cold");(void)api->enter();rt->diagnostic("APP unexpected-return");}
 else rt->diagnostic("APP fresh wake");
 if(!rt->release(&grant))rt->diagnostic("APP release-failed");
}
