/* Reuse the pinned System controller's existing synthetic UI/radio adapters.
 * The storage grants below are all issued by a real Runtime invocation. */
#define RUNTIME_ATOMIC_WIFI_CONTROLLER_HELPERS_ONLY
#include "runtime_atomic_wifi_controller.c"
#include "runtime_native_settings_fixture.h"

static void integrated_drain(void) {
 for(unsigned n=0;profile_work||remember_pending;++n){
  assert(n<10000000);wifi_profiles_step();native_settings_yield();
 }
 assert(!portable_wifi_atomic_pending(&wifi_atomic_edit));
}
static void integrated_submit(void) {
 for(unsigned n=0;!portable_wifi_atomic_pending(&wifi_atomic_edit);++n){
  assert(n<400&&profile_work);wifi_profiles_step();
 }
 /* The actual app adapter clears every borrowed mutation/intent after submit. */
 const unsigned char *p=(const unsigned char*)wifi_atomic_edit.mutations;
 for(unsigned i=0;i<sizeof(wifi_atomic_edit.mutations);++i)assert(!p[i]);
 for(unsigned i=0;i<sizeof(wifi_atomic_edit.intent);++i)assert(!wifi_atomic_edit.intent[i]);
 assert(!wifi_atomic_edit.desired.ssid[0]&&!wifi_atomic_edit.desired.password[0]);
 risc_runtime_capability_v1 release_attempt=wk;
 assert(!broker_runtime->release(&release_attempt)&&release_attempt.slot==wk.slot&&broker_live==1);
}
void runtime_native_settings_profiles(const risc_key_value_v1*kv,unsigned count) {
 uint32_t actual=0;int rc=portable_wifi_profile_count(kv,&actual);if(rc||actual!=count)fprintf(stderr,"legacy profile count: API=%u rc=%d actual=%u expected=%u\n",kv->api_version,rc,actual,count);assert(!rc&&actual==count);
 portable_wifi_credentials row={{0},{0}};
 assert(!portable_wifi_profile_load(kv,0,&row));
 assert(!strcmp(row.ssid,"Fixture saved A")&&!strcmp(row.password,"fixture-password-a"));
 if(count==2){
  assert(!portable_wifi_profile_load(kv,1,&row));
  assert(!strcmp(row.ssid,"Fixture saved B")&&!strcmp(row.password,"fixture-password-b"));
 }
 portable_wifi_credentials_clear(&row);
}
void runtime_native_settings_controller(const risc_runtime_api_v1*rt,unsigned phase,const char*scenario) {
 (void)workflow_start;(void)text_done;(void)seed;(void)search_done;
 assert(phase<=1);controller_start(rt);
 if(!phase){
  native_settings_pause_worker();
  controller_draft("Fixture saved A","fixture-password-a");wifi_activate(6);integrated_submit();
  native_settings_pending_checks(wifi_atomic_api,wifi_atomic_edit.token);
  /* Storage must own a copy even after the visible draft changes as well. */
  strcpy(credentials.ssid,"Unsaved changed draft");strcpy(credentials.password,"unsaved-changed-password");
  if(!strcmp(scenario,"pending-exit"))return;
  if(!strcmp(scenario,"retained"))native_settings_arm_close_failure();
  native_settings_resume_worker();
  if(!strcmp(scenario,"retained")){
   for(unsigned n=0;!cleanup_pending;++n){assert(n<10000000);wifi_profiles_step();native_settings_yield();}
   assert(cleanup_pending&&wifi_atomic_edit.status==RISC_ATOMIC_RETAINED&&wifi_atomic_edit.token&&broker_live==1);
   assert(!portable_wifi_suspend()&&broker_live==1);
   return;
  }
  integrated_drain();
  assert(saved_state==PORTABLE_WIFI_CREDENTIALS_SAVED&&draft_dirty);
  assert(!wifi_atomic_edit.receipt.status&&wifi_atomic_edit.receipt.operation_id==1);
  controller_record(0,"Fixture saved A","fixture-password-a");
  native_settings_verify_legacy(rt,1);

  native_settings_snapshot_io();
  controller_draft("Fixture saved B","fixture-password-b");wifi_connect();
  for(unsigned n=0;profile_work;++n){assert(n<400);wifi_profiles_step();}
  assert(wifi_operation&&joining&&remember_join&&!portable_wifi_atomic_pending(&wifi_atomic_edit));
  native_settings_assert_no_io();
  native_settings_pause_worker();native_settings_arm_write_pause();
  progress.phase=RISC_RADIO_CONNECTED;progress.station_state=2;tick(250);
  assert(remember_pending);wifi_profiles_step();integrated_submit();
  native_settings_pending_checks(wifi_atomic_api,wifi_atomic_edit.token);
  native_settings_resume_worker();native_settings_wait_write_pause();
  /* Back races an actual checked file write already executing on the worker. */
  assert(!wifi_back());native_settings_pending_checks(wifi_atomic_api,wifi_atomic_edit.token);
  native_settings_release_write_pause();integrated_drain();
  assert(saved_state==PORTABLE_WIFI_CREDENTIALS_SAVED);
  assert(!wifi_atomic_edit.receipt.status&&wifi_atomic_edit.receipt.operation_id==2);
  controller_record(1,"Fixture saved B","fixture-password-b");
  assert(!wifi_disconnect());stopped();assert(!wifi_operation&&!native_active);

  native_settings_pause_worker();
  native_settings_snapshot_io();
  controller_draft("Fixture cancelled C","fixture-password-c");wifi_activate(6);integrated_submit();
  assert(!wifi_back());native_settings_pending_checks(wifi_atomic_api,wifi_atomic_edit.token);
  native_settings_resume_worker();integrated_drain();
  assert(wifi_atomic_edit.receipt.status==RISC_ATOMIC_CANCELLED&&!wifi_atomic_edit.receipt.operation_id);
  native_settings_assert_no_io();
  runtime_native_settings_profiles(wifi_store,2);
  puts("integrated controller: copied Save, connected Remember, in-flight committed cancellation and queued revocation PASS");
 }else{
  runtime_native_settings_profiles(wifi_store,2);
  wifi_saved_open(0);
  for(unsigned n=0;saved_open_pending||saved_loading_active;++n){assert(n<100);wifi_profiles_step();}
  assert(saved_rows==2&&!strcmp(saved_names[0],"Fixture saved A")&&!strcmp(saved_names[1],"Fixture saved B"));
  wifi_activate(1);assert(!strcmp(credentials.ssid,"Fixture saved B")&&!draft_dirty);
  puts("integrated controller: both saved profiles reload through a new checked store, coordinator and Runtime PASS");
 }
 native_settings_verify_legacy(rt,2);native_settings_idle_checks(wifi_atomic_api);
 assert(!legacy_fallbacks&&!writes&&!profile_work&&!remember_pending&&!remember_join);
 finish();assert(!broker_live&&broker_acquires==broker_releases&&!legacy_fallbacks&&!writes);
}
