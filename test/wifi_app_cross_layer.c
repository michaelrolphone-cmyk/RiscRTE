/* Compile the real selected controller + adapter against production radio
 * providers. UI/time/storage outer services remain deterministic fixtures. */
#define TEST_CORE_PAPER_MOTION
#include "native_system_apps_test.c"
extern void native_system_test_wifi_request(bool);
extern const char *cross_app_message(void);
static const void *production_wifi,*production_ble;
extern bool cross_layer_storage_safe(void);
extern bool cross_layer_activation_safe(void);
static bool wifi_pinned;
static bool cross_acquire(const char *name,uint32_t version,uint64_t instance,risc_runtime_capability_v1 *out){
 const void *api=NULL;
 if(!strcmp(name,"net.wifi")){
  if(!wifi_pinned && !cross_layer_activation_safe())return false;
  wifi_pinned=true;api=production_wifi;assert(instance==15);
 }else if(!strcmp(name,"bluetooth.hci")){api=production_ble;assert(instance==16);}
 if(!api)return app_acquire(name,version,instance,out);
 io();assert(version==1);++acquires;++live;
 *out=(risc_runtime_capability_v1){.struct_size=sizeof(*out),.slot=acquires,.generation=1,.api=api};return true;
}
static bool cross_release(risc_runtime_capability_v1 *g){
 if(g->api==production_wifi||g->api==production_ble)return fx_release(g);
 return app_release(g);
}
static bool cross_diag(const char *line){io();puts(line);return true;}
void cross_app_begin(const void *w,const void *b){
 production_wifi=w;production_ble=b;scenario="valid";
 source_kv.get=app_get;source_kv.put=fx_kv_put;
 fx_runtime.acquire=cross_acquire;fx_runtime.release=cross_release;fx_runtime.diagnostic=cross_diag;
 assert(app_module_init()==0);assert(native_system_test_open());
}
void cross_app_connect(void){native_system_test_wifi_request(false);}
void cross_app_scan(void){native_system_test_wifi_request(true);}
void cross_app_close(void){assert(portable_wifi_close());app_module_fini();assert(!live&&!retained);}
void cross_app_reopen(void){assert(app_module_init()==0);assert(native_system_test_open());}
bool cross_app_retained(void){return portable_adapter_retained();}
void cross_app_broadcast_fail(void){fixture_broadcast_pause_fail=true;}
void cross_app_clock_sample(void){twatch_rtc_time_v1 t={0};assert(portable_app_native_local_time(&t));}
/* The slice never enters physical sleep; fail if the fixture accidentally does. */
int portable_app_idle_sleep(const risc_runtime_api_v1 *r,const risc_display_output_api_v1 *d,
 const risc_battery_gauge_api_v1 *g,const alarm_service_v1 *a){(void)r;(void)d;(void)g;(void)a;assert(false);return -1;}

void cross_app_policy_error(unsigned mode){radio_case=mode==2?"radio-off":mode==1?"radio-corrupt":"radio-io";}
unsigned cross_app_policy_reads(void){return radio_reads;}
const char *cross_app_result_message(void){return cross_app_message();}
