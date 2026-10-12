/* Real System Wi-Fi controller inside a real Runtime app invocation. Only UI
 * and radio fixtures are synthetic. All atomic handles come from the broker. */
#define PORTABLE_WIFI_ATOMIC_EXPERIMENTAL
#define PORTABLE_WIFI_REMEMBER_CONNECTED
#define TEST_WIFI_ATOMIC_FIXTURE
#include "RiscKeyValueAtomicV1.h"
static risc_key_value_atomic_v1 atomic_fixture_api; /* deliberately never used */
#define WIFI_WORKFLOW_LIBRARY
#define risc_runtime_get_api controller_fixture_get_api
#define app_main controller_fixture_app_main
#define app_module_init controller_fixture_module_init
#define app_module_fini controller_fixture_module_fini
#include SYSTEM_WIFI_WORKFLOW_SOURCE
#undef risc_runtime_get_api
#undef app_main
#undef app_module_init
#undef app_module_fini

#ifndef RUNTIME_ATOMIC_WIFI_CONTROLLER_HELPERS_ONLY
extern int runtime_atomic_worker_step(void);
#endif
static const risc_runtime_api_v1 *broker_runtime;
static const void *broker_atomic_table;
static unsigned broker_live,broker_acquires,broker_releases,legacy_fallbacks;

static bool controller_acquire(const char*name,uint32_t version,uint64_t instance,
                               risc_runtime_capability_v1*out){
 if(!strcmp(name,RISC_KEY_VALUE_ATOMIC_CAPABILITY)){
  assert(version==1&&instance==6&&!broker_live);
  bool ok=broker_runtime->acquire(name,version,instance,out);
  if(ok){assert(out->api&&out->api!=&atomic_fixture_api);broker_atomic_table=out->api;++broker_live;++broker_acquires;}
  return ok;
 }
 if(!strcmp(name,RISC_KEY_VALUE_CAPABILITY)&&instance==6){++legacy_fallbacks;return false;}
 return workflow_acquire(name,version,instance,out);
}
static bool controller_release(risc_runtime_capability_v1*grant){
 if(grant->api&&grant->api==broker_atomic_table){
  assert(broker_live==1);bool ok=broker_runtime->release(grant);
  if(ok){--broker_live;++broker_releases;broker_atomic_table=NULL;}
  return ok;
 }
 return workflow_release(grant);
}
static void controller_start(const risc_runtime_api_v1*rt){
 assert(!broker_live&&!grant_count&&!opened);broker_runtime=rt;
 runtime_api=(risc_runtime_api_v1){.api_version=1,.struct_size=sizeof(runtime_api),
  .health=fake_health,.yield_ms=fake_yield,.diagnostic=fake_diag,.request_launch=fake_launch,
  .acquire=controller_acquire,.release=controller_release};
 radio_api=(wifi_api_v1){.api_version=1,.struct_size=sizeof(radio_api),
  .connect=fake_connect,.disconnect=fake_disconnect_legacy,.status=fake_status,
  .rssi=fake_rssi,.addresses=fake_addresses,.scan_start=fake_scan_start,
  .scan_poll=fake_scan_poll,.scan_cancel=fake_scan_cancel,.disconnect_checked=fake_disconnect};
 fake_scan=(garden_radio_scan_result_v1){.struct_size=sizeof(fake_scan),.state=GARDEN_RADIO_SCAN_RUNNING};
 visual_alarm=(alarm_service_outputs_v1){.service=alarm_api,.output_modes=ALARM_MODE_VISUAL};
 visual_alarm.service.struct_size=sizeof(visual_alarm);
 radios=(wifi_async_v1){.base=radio_api,.async_tag=RISC_RADIO_ASYNC_TAG,.async_version=1,
  .begin=async_begin,.poll=async_poll,.cancel=async_cancel,.service_begin=service_begin,.service_end=service_end};
 radios.base.struct_size=sizeof(radios);use_async=true;
 assert(!controller_fixture_module_init());assert(wifi_open());
 assert(wifi_atomic_api&&wifi_store&&broker_live==1);
}
#ifndef RUNTIME_ATOMIC_WIFI_CONTROLLER_HELPERS_ONLY
static void controller_drain(void){
 for(unsigned step=0;profile_work||remember_pending;++step){
  assert(step<400);wifi_profiles_step();(void)runtime_atomic_worker_step();
 }
 assert(!portable_wifi_atomic_pending(&wifi_atomic_edit));
}
#endif
static void controller_draft(const char*ssid,const char*password){
 assert(!profile_work&&!remember_join&&!remember_pending);
 strcpy(credentials.ssid,ssid);strcpy(credentials.password,password);
 open_network=false;draft_dirty=true;wifi_page(WP_ROOT);
}
static void controller_record(unsigned slot,const char*ssid,const char*password){
 portable_wifi_credentials value={{0},{0}};
 assert(!portable_wifi_profile_load(wifi_store,slot,&value));
 assert(!strcmp(value.ssid,ssid)&&!strcmp(value.password,password));
 portable_wifi_credentials_clear(&value);
}
#ifndef RUNTIME_ATOMIC_WIFI_CONTROLLER_HELPERS_ONLY
void runtime_atomic_wifi_controller(const risc_runtime_api_v1*rt,unsigned phase){
 (void)workflow_start;(void)text_done;(void)seed;(void)search_done;
 assert(rt&&phase<=1);controller_start(rt);
 if(!phase){
  controller_draft("Fixture saved A","fixture-password-a");wifi_activate(6);controller_drain();
  assert(saved_state==PORTABLE_WIFI_CREDENTIALS_SAVED&&!draft_dirty);
  controller_record(0,"Fixture saved A","fixture-password-a");

  controller_draft("Fixture saved B","fixture-password-b");wifi_connect();
  for(unsigned n=0;profile_work;++n){assert(n<400);wifi_profiles_step();assert(!runtime_atomic_worker_step());}
  assert(wifi_operation&&joining&&remember_join);
  progress.phase=RISC_RADIO_CONNECTED;progress.station_state=2;tick(250);
  assert(remember_pending);controller_drain();
  assert(saved_state==PORTABLE_WIFI_CREDENTIALS_SAVED);
  controller_record(1,"Fixture saved B","fixture-password-b");
  assert(!wifi_disconnect());stopped();assert(!wifi_operation&&!native_active);

  controller_draft("Fixture cancelled C","fixture-password-c");wifi_activate(6);
  for(unsigned n=0;!portable_wifi_atomic_pending(&wifi_atomic_edit);++n){assert(n<400&&profile_work);wifi_profiles_step();}
  assert(!wifi_back());controller_drain();
  uint32_t count=0;assert(!portable_wifi_profile_count(wifi_store,&count)&&count==2);
 }else{
  uint32_t count=0;assert(!portable_wifi_profile_count(wifi_store,&count)&&count==2);
  controller_record(0,"Fixture saved A","fixture-password-a");
  controller_record(1,"Fixture saved B","fixture-password-b");
  wifi_saved_open(0);
  for(unsigned n=0;saved_open_pending||saved_loading_active;++n){assert(n<100);wifi_profiles_step();}
  assert(saved_rows==2&&!strcmp(saved_names[0],"Fixture saved A")&&!strcmp(saved_names[1],"Fixture saved B"));
  wifi_activate(1);assert(!strcmp(credentials.ssid,"Fixture saved B")&&!draft_dirty);
 }
 assert(!legacy_fallbacks&&!writes&&!profile_work&&!remember_pending&&!remember_join);
 assert(!runtime_atomic_worker_step());finish();
 assert(!broker_live&&broker_acquires==broker_releases&&!legacy_fallbacks&&!writes);
 puts(phase?"Actual Runtime broker + System controller: two profiles survive native reload PASS":
             "Actual Runtime broker + System controller: Save, connected Remember and queued cancellation PASS");
}

#endif
