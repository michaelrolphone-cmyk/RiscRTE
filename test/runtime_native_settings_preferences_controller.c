/* Actual pinned System Settings controller. Display/input/RTC/native clock
 * providers are fixtures; EVERY namespace-1 storage grant is the real Runtime
 * admitted facade backed by NativeSettingsCoordinator and checked storage. */
#define main unused_source_settings_main
#define risc_runtime_get_api settings_fixture_get_api
#define app_main settings_fixture_app_main
#define app_module_init settings_fixture_module_init
#define app_module_fini settings_fixture_module_fini
#include SYSTEM_NATIVE_SETTINGS_SOURCE
#undef main
#undef risc_runtime_get_api
#undef app_main
#undef app_module_init
#undef app_module_fini
#include "runtime_native_settings_preferences_fixture.h"
static const risc_runtime_api_v1 *broker_runtime;
static risc_runtime_capability_v1 broker_grants[16];
static unsigned broker_live,broker_acquires,broker_releases;
static unsigned action; /* 1: Save, 2: queued Cancel, 3: stale draft, 4: retained close, 5: in-flight Cancel. */
static bool resumed,checked,intervened,released;
static bool controller_acquire(const char *name,uint32_t version,uint64_t instance,risc_runtime_capability_v1 *out) {
 if(!strcmp(name,RISC_KEY_VALUE_ATOMIC_CAPABILITY)||!strcmp(name,RISC_KEY_VALUE_CAPABILITY)) {
  assert(instance==1&&version==1);unsigned n=0;while(n<16&&broker_grants[n].api)++n;assert(n<16);
  if(!broker_runtime->acquire(name,version,instance,out))return false;
  broker_grants[n]=*out;++broker_live;++broker_acquires;return true;
 }
 return acquire(name,version,instance,out);
}
static bool controller_release(risc_runtime_capability_v1 *out) {
 for(unsigned i=0;i<16;++i)if(broker_grants[i].api&&broker_grants[i].api==out->api&&
  broker_grants[i].slot==out->slot&&broker_grants[i].generation==out->generation) {
  assert(broker_live);if(!broker_runtime->release(out))return false;
  broker_grants[i]=(risc_runtime_capability_v1){0};--broker_live;++broker_releases;return true;
 }
 return release(out);
}
static bool controller_retain(void) {
 assert(broker_runtime->retain_invocation);bool fake=retain();return fake&&broker_runtime->retain_invocation();
}
static void controller_yield(uint32_t ms) {
 yielding(ms);
 if(settings_atomic_edit.token&&!resumed) {
  if(!checked&&getenv("NATIVE_SETTINGS_TRACE"))fprintf(stderr,"accepted token=%llu action=%u poll=%u\n",(unsigned long long)settings_atomic_edit.token,action,polls);
  if(!checked){native_settings_pending_checks(settings_atomic_api,settings_atomic_edit.token);checked=true;}
  if(action!=2||settings_atomic_edit.cancelled) {
   if(action==4)native_settings_arm_close_failure();
   if(action==5)native_settings_arm_write_pause();
   native_settings_resume_worker();resumed=true;
   if(action==5)native_settings_wait_write_pause();
  }
 }
 if(action==5&&resumed&&settings_atomic_edit.cancelled&&!released){native_settings_release_write_pause();released=true;}
 native_settings_yield();
}
static bool controller_show(void *ctx,risc_display_frame_v1 frame,const risc_display_rect_v1 *damage,size_t count,
 const risc_display_present_options_v1 *options,risc_display_present_token_v1 *token) {
 if(action==3&&settings_atomic_edit.valid&&!intervened) {
  intervened=true;native_settings_intervening_preference();
 }
 return frame_show(ctx,frame,damage,count,options,token);
}
static bool controller_acquire_ui(const char *name,uint32_t version,uint64_t instance,risc_runtime_capability_v1 *out) {
 if(!controller_acquire(name,version,instance,out))return false;
 if(!strcmp(name,"display.output")) {
  static risc_display_output_api_v1 display;display=display_api;display.submit=controller_show;out->api=&display;
 }
 return true;
}
static void inputs(void){memset(nav_buttons,0,sizeof(nav_buttons));contact_count=0;next_at=polls+10;}
static const t5_app_api_v1 *controller_start(const risc_runtime_api_v1 *rt) {
 assert(!broker_live&&!live);broker_runtime=rt;test_name="runtime-settings";zone_id="UTC";native_epoch=1768505696;
 polls=0;next_at=10;action=0;resumed=checked=intervened=false;
 runtime.acquire=controller_acquire_ui;runtime.release=controller_release;runtime.yield_ms=controller_yield;runtime.retain_invocation=controller_retain;
 assert(!settings_fixture_module_init());in_main=true;
 const t5_app_api_v1 *api=t5_app_get_api(1);assert(api&&settings_atomic_api&&broker_live);
 return api;
}
static uint8_t choose(const t5_app_api_v1 *api,unsigned row,unsigned kind) {
 if(getenv("NATIVE_SETTINGS_TRACE"))fprintf(stderr,"choose row=%u kind=%u polls=%u\n",row,kind,polls);
 action=0;api->settings_render(0,(int32_t)row+1);inputs();action=kind;checked=resumed=intervened=released=false;
 if(kind!=3)native_settings_pause_worker();
 if(row==1){next_at+=20;nav(RISC_NAV_DOWN);next_at+=20;nav(RISC_NAV_CONFIRM);next_at+=20;nav(RISC_NAV_CONFIRM);}
 else {next_at+=20;nav(RISC_NAV_RIGHT);next_at+=20;nav(RISC_NAV_CONFIRM);}
 if(kind==2||kind==5)nav(RISC_NAV_BACK);
 if(kind==3){next_at+=20;nav(RISC_NAV_BACK);}
 uint8_t result=api->settings_activate(0,row);
 if(getenv("NATIVE_SETTINGS_TRACE"))fprintf(stderr,"returned row=%u result=%u status=%d poll=%u\n",row,result,settings_atomic_status,polls);
 assert(!other_puts&&!basis_puts&&!zone_puts);
 if(kind==1||kind==5)assert(result==T5_APP_SETTING_UPDATED&&checked&&resumed&&!settings_atomic_edit.token);
 if(kind==5)assert(released&&settings_atomic_edit.cancelled&&settings_atomic_edit.receipt.operation_id==1);
 if(kind==2)assert(result==T5_APP_SETTING_NO_CHANGE&&checked&&resumed&&settings_atomic_status==RISC_ATOMIC_CANCELLED);
 if(kind==3)assert(result==T5_APP_SETTING_NO_CHANGE&&intervened&&settings_atomic_status==RISC_ATOMIC_STALE);
 if(kind==4)assert(retained&&native_custody_retained&&settings_atomic_edit.token&&broker_live);
 action=0;return result;
}
void runtime_native_settings_preferences(const risc_key_value_v1 *kv,unsigned count) {
 assert(count==8);unsigned value=99;uint32_t timer=0;char zone[PORTABLE_TIMEZONE_ID_BYTES];
 assert(portable_time_format_load(kv,&value)==PORTABLE_TIME_FORMAT_LOADED&&value==1);
 assert(portable_timezone_preference_load(kv,zone)==PORTABLE_TIMEZONE_LOADED&&!strcmp(zone,portable_timezone_get(1)->id));
 assert(portable_desk_face_load(kv,&value)==PORTABLE_DESK_FACE_LOADED&&value==0);
 assert(portable_desk_direction_load(kv,&value)==PORTABLE_DESK_FACE_LOADED&&value==1);
 assert(portable_reader_preference_load(kv,false,&value)==PORTABLE_READER_LOADED&&value==1);
 assert(portable_reader_preference_load(kv,true,&value)==PORTABLE_READER_LOADED&&value==2);
 assert(portable_sleep_timer_load(kv,false,&timer)==PORTABLE_SLEEP_LOADED&&timer==65000);
}
void runtime_native_settings_controller(const risc_runtime_api_v1 *rt,unsigned phase,const char *scenario) {
 const t5_app_api_v1 *api=controller_start(rt);
 if(!phase) {
  if(!strcmp(scenario,"retained")){choose(api,2,4);return;}
  choose(api,2,5);choose(api,1,1);choose(api,SETTINGS_FACE_ROW,1);
  choose(api,SETTINGS_DESK_DIRECTION_ROW,1);choose(api,SETTINGS_FLIP_ROW,1);
  choose(api,SETTINGS_LANGUAGE_ROW,1);choose(api,SETTINGS_TIMER_ROW,1);
  choose(api,2,3);native_settings_snapshot_io();choose(api,2,2);native_settings_assert_no_io();
 }else {
  t5_app_setting_t setting;assert(api->settings_get(0,2,&setting)&&!strcmp(setting.value,"24-hour"));
  assert(api->settings_get(0,SETTINGS_TIMER_ROW,&setting)&&!strcmp(setting.value,"65 seconds"));
 }
 runtime_native_settings_preferences(settings_store,8);native_settings_verify_legacy(rt,8);native_settings_idle_checks(settings_atomic_api);
 assert(!rtc_writes&&!seeds&&!basis_puts&&!zone_puts&&!other_puts&&!retained);
 in_main=false;in_fini=true;settings_fixture_module_fini();in_fini=false;
 assert(!live&&!subscriptions&&!frames&&!broker_live&&broker_acquires==broker_releases);
 puts(phase?"Actual Settings controller: seven preferences reloaded through fresh checked native store and Runtime facade PASS":
  "Actual Settings controller: seven explicit Saves, stale draft rejection, queued and in-flight cancellation through checked native worker PASS");
}
