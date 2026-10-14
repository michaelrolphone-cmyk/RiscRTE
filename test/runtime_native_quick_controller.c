/* Actual pinned Home Quick staging/session/radio controllers. Original fixture
 * storage doubles are compiled but forbidden; every storage grant is brokered
 * by a real Runtime app invocation. Display/input/radio hooks are synthetic. */
#define main unused_quick_fixture_main
#include SYSTEM_QUICK_SOURCE
#undef main
#include "runtime_native_extra_fixture.h"
static const risc_runtime_api_v1 *broker;
static risc_runtime_capability_v1 live_grants[16];
static unsigned live_count,real_acquires,real_releases;
static bool broker_acquire(const char *name,uint32_t version,uint64_t id,risc_runtime_capability_v1 *out){
 if(!strcmp(name,RISC_KEY_VALUE_ATOMIC_CAPABILITY)||!strcmp(name,RISC_KEY_VALUE_CAPABILITY)){
  assert(version==1&&id==1);if(!broker->acquire(name,version,id,out))return false;
  assert(out->api!=&atomic_api&&out->api!=&kv);unsigned i=0;while(i<16&&live_grants[i].api)++i;assert(i<16);live_grants[i]=*out;++live_count;++real_acquires;return true;
 }
 return acquire(name,version,id,out);
}
static bool broker_release(risc_runtime_capability_v1 *grant){
 for(unsigned i=0;i<16;++i)if(live_grants[i].api==grant->api&&live_grants[i].slot==grant->slot&&live_grants[i].generation==grant->generation){
  assert(live_count);if(!broker->release(grant))return false;live_grants[i]=(risc_runtime_capability_v1){0};--live_count;++real_releases;return true;
 }
 return release(grant);
}
static void broker_yield(uint32_t ms){yield_ms(ms);native_settings_yield();}
static risc_runtime_api_v1 admitted;
const char *native_extra_key(unsigned i){return i<6?keys[i]:NULL;}
unsigned native_extra_intents(void){return 7;}
void native_extra_intent(unsigned index,const void *bytes,unsigned size){
 static const char *const expected[]={"brightness","brightness","frontlight_tone","alert_dnd","contexts_on","quick_radio","quick_radio"};
 const uint8_t *intent=bytes;assert(index<7&&size==176&&!memcmp(intent,"PFI0001",8)&&intent[8]==PORTABLE_PREFERENCE_USER);
 assert(!strcmp((const char*)intent+12,expected[index])&&word(intent+172)==portable_preference_crc(intent,172));
}
void runtime_native_settings_preferences(const risc_key_value_v1 *view,unsigned ignored){
 (void)ignored;unsigned value=99;bool enabled=false;uint8_t flags=0;
 assert(pqa_preference_load(view,PQA_BRIGHTNESS_KEY,40,0,&value)&&value==0);
 assert(pqa_preference_load(view,PQA_RESTORE_BRIGHTNESS_KEY,40,10,&value)&&value==70);
 assert(pqa_preference_load(view,PQA_TONE_KEY,50,0,&value)&&value==80);
 assert(pqa_dnd_load(view,&enabled)&&enabled);
 assert(portable_context_enabled_load(view,&enabled)&&enabled);
 assert(portable_radio_load(view,&flags)&&flags==28);
}
static void display_fixture(void){
 display_api.snapshot.metrics.power.history.base=(risc_display_output_api_v1){.api_version=1,.struct_size=sizeof(display_api),.set_brightness=light};
 display_api.snapshot.metrics.power.history.extension_tag=RISC_DISPLAY_HISTORY_TAG;display_api.snapshot.metrics.power.history.extension_version=1;display_api.snapshot.metrics.power.history.seed_previous=seed;
 display_api.snapshot.metrics.power.power_tag=RISC_DISPLAY_POWER_TAG;display_api.snapshot.metrics.power.power_version=1;display_api.snapshot.metrics.power.prepare=power;display_api.snapshot.metrics.power.resume=power;
 display_api.snapshot.metrics.metrics_tag=RISC_DISPLAY_METRICS_TAG;display_api.snapshot.metrics.metrics_version=RISC_DISPLAY_METRICS_VERSION;display_api.snapshot.metrics.snapshot=metrics;
 display_api.snapshot.snapshot_tag=RISC_DISPLAY_SNAPSHOT_TAG;display_api.snapshot.snapshot_version=RISC_DISPLAY_SNAPSHOT_VERSION;display_api.snapshot.copy_completed=copy;
 display_api.frontlight_tag=RISC_DISPLAY_FRONTLIGHT_TAG;display_api.frontlight_version=1;display_api.get_tone=tone_get;display_api.set_tone=tone_set;
}
void runtime_native_settings_controller(const risc_runtime_api_v1 *runtime,unsigned phase,const char *scenario){
 (void)scenario;broker=runtime;assert(!live_count);test="runtime-quick";
 admitted=*broker;admitted.acquire=broker_acquire;admitted.release=broker_release;admitted.yield_ms=broker_yield;admitted.diagnostic=diagnostic;rt=&admitted;
 pqa_session_init(&quick);quick.ui.paper=true;quick.ui.contexts_controls=true;quick.save_user=quick_atomic_save;display_fixture();
 assert(pqa_session_load(&quick,rt)&&pqa_radios_load(&quick_radios,&quick.ui,rt));
 if(!phase){
  assert(quick_atomic_modal_begin());
  ++quick_atomic_contact;quick.ui.action_brightness=70;assert(apply(PQA_BRIGHTNESS_PREVIEW)&&apply(PQA_BRIGHTNESS_COMMIT));
  ++quick_atomic_contact;quick.ui.action_brightness=0;assert(apply(PQA_BRIGHTNESS_PREVIEW)&&apply(PQA_BRIGHTNESS_COMMIT));
  ++quick_atomic_contact;quick.ui.action_tone=80;assert(apply(PQA_TONE_PREVIEW)&&apply(PQA_TONE_COMMIT));
  ++quick_atomic_contact;quick.ui.action_dnd=true;assert(apply(PQA_DND));
  ++quick_atomic_contact;quick.ui.action_contexts=true;assert(apply(PQA_CONTEXTS));
  ++quick_atomic_contact;assert(apply(PQA_BLUETOOTH));
  ++quick_atomic_contact;assert(apply(PQA_AIRPLANE));
  assert(quick_atomic_close());
 }else assert(quick.brightness==0&&quick.restore_brightness==70&&quick.tone==80&&quick.dnd_enabled&&quick.ui.contexts_enabled&&quick_radios.flags==28);
 native_extra_verify_reads(broker);
 assert(!submits&&!collects&&!reads&&!legacy_puts&&!atomic_acquires&&!atomic_releases&&!live_count&&real_acquires==real_releases&&!quick.retained);
 puts(phase?"Actual Home Quick: fresh session/radio readers restore all six keys PASS":"Actual Home Quick: seven real-broker user intents, brightness OFF/restore grouping, tone, DND, Contexts and radio controls PASS");
}
