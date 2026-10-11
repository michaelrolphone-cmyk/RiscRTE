/* Unmodified production adapter with a diagnostic-only test accessor. */
#ifndef CAPACITY_SYSTEM_SOURCE
#error "The fixture runner must select CAPACITY_SYSTEM_SOURCE"
#endif
#include CAPACITY_SYSTEM_SOURCE
void capacity_home_adapter_debug(void){fprintf(stderr,"adapter phase=%u failed=%u touch_api=%p sub=%llu api_size=%u need=%zu nav=%u retained=%u\n",desk_phase,failed,(void*)touch.api,(unsigned long long)touch.subscription,touch.api?touch.api->struct_size:0,sizeof(*touch.api),navigation_ready,native_custody_retained);}
